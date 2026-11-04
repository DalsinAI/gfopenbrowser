/* The stove's allocator (stove/compat/os32compat_malloc.c) against the host's
 * C library, its exec calls on POSIX threads (tests/host/exec_tasks.c):
 * tests/run-host-tests.sh. The contract, then threads that allocate, resize
 * and free at random, each block checked against a shadow of what was
 * written into it (so two blocks in use that overlapped would show), some
 * blocks freed by another thread than the one that allocated them.
 *
 *   test_os32_malloc [rounds]       (default 200000 operations a thread)
 *
 * Built once as it ships and once with -DOS32_MALLOC_DEBUG.
 * MIT, Copyright (c) 2026 AmiCradle Team. */
#include "../stove/compat/os32compat_malloc.c"

#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

/* libnix's malloc, as the host's, counted. While growRaces is set, an
 * arena's call first makes another malloc() (into raced[]), as another task
 * can while this one is in libnix's malloc with the lock released */
static long realLive, realCalls;
static int growRaces;
static void* raced[4];
void* __real_malloc(size_t n)
{
    void* p;
    if (growRaces > 0 && n >= OS32_PAGES * OS32_PAGE) {
        growRaces--;
        raced[growRaces] = __wrap_malloc(24);
    }
    p = malloc(n);
    if (p) {
        __atomic_add_fetch(&realLive, 1, __ATOMIC_SEQ_CST);
        __atomic_add_fetch(&realCalls, 1, __ATOMIC_SEQ_CST);
    }
    return p;
}
void __real_free(void* p)
{
    if (p)
        __atomic_sub_fetch(&realLive, 1, __ATOMIC_SEQ_CST);
    free(p);
}

static int failed, checks;

#define CHECK(cond) do { checks++; if (!(cond)) { printf("OS32MALLOC FAIL line %d: %s\n", __LINE__, #cond); failed = 1; } } while (0)

static unsigned rnd(unsigned* s)
{
    *s = *s * 1103515245u + 12345u;
    return *s >> 8;
}

/* sizes as WebKit asks for them: mostly small, some growth buffers, a few big */
static size_t some_size(unsigned* s)
{
    unsigned r = rnd(s) % 100;
    if (r < 70)
        return rnd(s) % 129;
    if (r < 90)
        return 129 + rnd(s) % 896;
    if (r < 98)
        return 1025 + rnd(s) % 3100;
    return 4000 + rnd(s) % 70000;
}

static void check_classes(void)
{
    size_t n;
    unsigned c;
    for (c = 0; c < OS32_CLASSES; c++)
        CHECK(os32_size[c] % 8 == 0 && (!c || os32_size[c] > os32_size[c - 1]));
    for (n = 1; n <= OS32_SLOT_MAX; n++) {
        c = os32_class(n);
        if (c >= OS32_CLASSES || os32_size[c] < n || (c && os32_size[c - 1] >= n)) {
            CHECK(!"the class for a slot");
            printf("  n=%zu class=%u\n", n, c);
            return;
        }
    }
}

/* the pages kept by the classes, with every block freed */
static unsigned class_pages(void)
{
    unsigned c, kept = 0;
    struct os32_page* pg;
    for (c = 0; c < OS32_CLASSES; c++)
        for (pg = os32.cls[c]; pg; pg = pg->next) {
            CHECK(pg->cls == c && !pg->live && !pg->next);
            kept++;
        }
    return kept;
}

/* Tasks that find the pool empty at the same time each take an arena from
 * libnix. On one thread, in a set order: each of four malloc()s makes the
 * next from inside libnix's malloc, so four arenas come in, the innermost
 * first, and all four blocks share a page of that one; the other three
 * arenas' pages wait in the pool. With the four blocks freed, no more than
 * one spare arena may be kept (before 1 Nov 2026 all three were: 24 pages
 * in the pool). On an empty heap: run first. */
static void check_racing_arenas(void)
{
    void* first;
    unsigned kept, i;
    long arenas;
    CHECK(!os32.empty && !realLive);
    growRaces = 3;
    first = __wrap_malloc(24);
    CHECK(first && !growRaces && raced[0] && raced[1] && raced[2]);
    __wrap_free(first);
    for (i = 0; i < 3; i++) {
        __wrap_free(raced[i]);
        raced[i] = NULL;
    }
    kept = class_pages();
    arenas = __atomic_load_n(&realLive, __ATOMIC_SEQ_CST);
    printf("os32 malloc: arenas taken by racing tasks, all freed: %ld arenas kept, %u pages kept by a class, %u in the pool\n",
        arenas, kept, os32.nempty);
    CHECK(os32.nempty < 2 * OS32_PAGES);
    CHECK(arenas <= 2 && (unsigned long)arenas * OS32_PAGES == kept + os32.nempty);
}

static void check_contract(void)
{
    size_t n, a;
    unsigned char* p;
    void* q;
    char* r;
    unsigned char* foreign;

    __wrap_free(NULL);
    for (n = 0; n < 9000; n += n < 300 ? 1 : 37) {
        p = (unsigned char*)__wrap_malloc(n);
        CHECK(p && ((uintptr_t)p & 7) == 0);
        CHECK(os32_malloc_usable_size(p) >= n);
        if (p && n)
            memset(p, 0xA5, os32_malloc_usable_size(p));
        __wrap_free(p);
    }
    CHECK(os32_malloc_usable_size(NULL) == 0);

    /* every power of two, small and large */
    for (a = 1; a <= 65536; a *= 2) {
        size_t sizes[] = { 0, 1, 24, 100, 1000, 2000, 4000, 5000, 70000 };
        unsigned i;
        for (i = 0; i < sizeof sizes / sizeof *sizes; i++) {
            void* b = os32_aligned_alloc(a, sizes[i]);
            void* m = os32_memalign(a, sizes[i]);
            CHECK(b && ((uintptr_t)b & (a - 1)) == 0 && ((uintptr_t)b & 7) == 0);
            CHECK(m && ((uintptr_t)m & (a - 1)) == 0);
            CHECK(os32_malloc_usable_size(b) >= sizes[i]);
            if (b)
                memset(b, 1, sizes[i]);
            q = NULL;
            if (a >= sizeof(void*)) {
                CHECK(os32_posix_memalign(&q, a, sizes[i]) == 0 && q && ((uintptr_t)q & (a - 1)) == 0);
            } else {
                CHECK(os32_posix_memalign(&q, a, sizes[i]) == EINVAL);
            }
            /* realloc of an aligned block keeps what it held */
            b = __wrap_realloc(b, sizes[i] + 3000);
            CHECK(b && (!sizes[i] || ((unsigned char*)b)[sizes[i] - 1] == 1));
            __wrap_free(b);
            __wrap_free(m);
            __wrap_free(q);
        }
    }
    CHECK(os32_aligned_alloc(24, 10) == NULL);
    CHECK(os32_posix_memalign(&q, 12, 10) == EINVAL);
    CHECK(os32_posix_memalign(&q, 2, 10) == EINVAL);

    /* calloc: zeroed, and no wrap-around */
    CHECK(__wrap_calloc((size_t)-1 / 2, 3) == NULL);
    CHECK(__wrap_calloc(3, (size_t)-1 / 2) == NULL);
    p = (unsigned char*)__wrap_malloc(500);
    memset(p, 0xEE, 500);
    __wrap_free(p);
    p = (unsigned char*)__wrap_calloc(50, 10);
    for (n = 0; p && n < 500; n++)
        if (p[n])
            break;
    CHECK(p && n == 500);
    __wrap_free(p);
    p = (unsigned char*)__wrap_calloc(0, 10);
    CHECK(p != NULL);
    __wrap_free(p);

    /* realloc: in place within the class, moved past it */
    r = (char*)__wrap_realloc(NULL, 50);
    CHECK(r != NULL);
    memcpy(r, "fifty", 6);
    CHECK(__wrap_realloc(r, 52) == r);
    CHECK(__wrap_realloc(r, 49) == r);
    q = __wrap_realloc(r, 600);
    CHECK(q && q != r && !strcmp((char*)q, "fifty"));
    r = (char*)__wrap_realloc(q, 6);
    CHECK(r && !strcmp(r, "fifty"));
    q = __wrap_realloc(r, 20000);
    CHECK(q && !strcmp((char*)q, "fifty"));
    CHECK(__wrap_realloc(q, 15000) == q);
    r = (char*)__wrap_realloc(q, 100);
    CHECK(r && !strcmp(r, "fifty"));
    CHECK(__wrap_realloc(r, 0) == NULL);

    /* not ours: left alone */
    foreign = (unsigned char*)malloc(64);
    memset(foreign, 0, 64);
    __wrap_free(foreign + 32);
    CHECK(__wrap_realloc(foreign + 32, 100) == NULL);
    CHECK(os32_malloc_usable_size(foreign + 32) == 0);
    ((uintptr_t*)(foreign + 32))[-1] = OS32_SMALL | 16;
    ((uintptr_t*)(foreign + 32))[-2] = (uintptr_t)foreign;     /* no page there */
    __wrap_free(foreign + 32);
    free(foreign);

    /* a second free() of the same block is not ours either */
    p = (unsigned char*)__wrap_malloc(40);
    __wrap_free(p);
    __wrap_free(p);
    q = __wrap_malloc(40);
    r = (char*)__wrap_malloc(40);
    CHECK(q != r);
    __wrap_free(q);
    __wrap_free(r);
}

/* GC blocks (aligned_alloc(16384, 16384)): aligned, apart, kept whole, freed
 * back to libnix but for one empty arena, and told apart from other blocks
 * that happen to sit on a 16 KB boundary */
static void check_blocks(void)
{
    enum { N = 3 * OS32_BLOCKS + 5 };
    unsigned char* b[N];
    unsigned i, k;
    unsigned kept = os32.nbarenas; /* check_contract's empty arena, if any */
    long calls = realCalls;
    CHECK(kept <= 1);
    for (i = 0; i < N; i++) {
        b[i] = (unsigned char*)os32_aligned_alloc(OS32_BLOCK, OS32_BLOCK);
        CHECK(b[i] && ((uintptr_t)b[i] & (OS32_BLOCK - 1)) == 0);
        CHECK(os32_malloc_usable_size(b[i]) == OS32_BLOCK);
        if (b[i])
            memset(b[i], (int)(i + 1), OS32_BLOCK);
    }
    /* 53 blocks: 4 arenas, one libnix call for each new one */
    CHECK(os32.nbarenas == 4 && realCalls - calls == 4 - (long)kept);
    for (i = 0; i < N; i++)
        for (k = 0; k < N; k++)
            if (i != k && b[i] && b[k])
                CHECK((uintptr_t)(b[i] - b[k]) >= OS32_BLOCK || (uintptr_t)(b[k] - b[i]) >= OS32_BLOCK);
    for (i = 0; i < N; i++)
        for (k = 0; b[i] && k < OS32_BLOCK; k += 509)
            CHECK(b[i][k] == (unsigned char)(i + 1));
    /* a smaller block at the same alignment is one too; realloc keeps it
     * while it fits and moves it, contents and all, when it does not */
    {
        unsigned char* s = (unsigned char*)os32_aligned_alloc(OS32_BLOCK, 100);
        unsigned char* r;
        CHECK(s && ((uintptr_t)s & (OS32_BLOCK - 1)) == 0);
        memset(s, 7, OS32_BLOCK);
        CHECK(__wrap_realloc(s, OS32_BLOCK) == s);
        r = (unsigned char*)__wrap_realloc(s, OS32_BLOCK + 1);
        CHECK(r && r != s && r[0] == 7 && r[OS32_BLOCK - 1] == 7);
        __wrap_free(r);
    }
    /* other alignments and sizes keep their paths */
    {
        void* big = os32_aligned_alloc(OS32_BLOCK, OS32_BLOCK + 8);
        void* half = os32_aligned_alloc(OS32_BLOCK / 2, OS32_BLOCK / 2);
        CHECK(big && !os32_barena_of(big) && os32_malloc_usable_size(big) >= OS32_BLOCK + 8);
        CHECK(half && !os32_barena_of(half));
        __wrap_free(big);
        __wrap_free(half);
    }
    /* a small or large block on a 16 KB boundary is not taken for a GC block */
    {
        void* q[4096];
        unsigned n = 0, onBoundary = 0;
        for (i = 0; i < 4096; i++) {
            q[n] = __wrap_malloc(8 + (i % 40) * 8);
            if (q[n] && !((uintptr_t)q[n] & (OS32_BLOCK - 1)))
                onBoundary++;
            n++;
        }
        for (i = 0; i < n; i++) {
            CHECK(os32_malloc_usable_size(q[i]) >= 8);
            __wrap_free(q[i]);
        }
        printf("os32 malloc: %u small blocks on a 16 KB boundary freed as small blocks\n", onBoundary);
    }
    for (i = 0; i < N; i++)
        __wrap_free(b[i]);
    /* every block free: one arena kept */
    CHECK(os32.nbarenas == 1 && os32.bempty == 1 && os32.broom && !os32.broom->next);
    /* the kept arena serves the next block without libnix */
    {
        void* again;
        calls = realCalls;
        again = os32_aligned_alloc(OS32_BLOCK, OS32_BLOCK);
        CHECK(again && realCalls == calls && os32.bempty == 0);
        __wrap_free(again);
        CHECK(os32.bempty == 1);
    }
}

#ifdef OS32_MALLOC_DEBUG
/* a write past the end, or after free(), stops the program */
static int dies(int how)
{
    pid_t pid = fork();
    int status;
    if (!pid) {
        char* p = (char*)__wrap_malloc(how == 2 ? 9000 : 30);
        if (how == 0 || how == 2) {
            p[how == 2 ? 9000 : 30] = 1;
            __wrap_free(p);
        } else {
            __wrap_free(p);
            p[20] = 1;
            for (;;) /* handed out again */
                if (__wrap_malloc(30) == p)
                    break;
        }
        _exit(0);
    }
    waitpid(pid, &status, 0);
    return WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT;
}
#endif

/* the threads */

#define THREADS 8
#define SLOTS 1500
#define MAILBOX 64

struct block {
    unsigned char* p;
    size_t size;
    unsigned seed;
};

static struct block mailbox[MAILBOX]; /* blocks one thread leaves for another to free */
static pthread_mutex_t mailLock = PTHREAD_MUTEX_INITIALIZER;
static long rounds = 200000;
static long totalOps;

static void fill(struct block* b)
{
    size_t i;
    for (i = 0; i < b->size; i++)
        b->p[i] = (unsigned char)(b->seed + i * 7);
}

static int intact(const struct block* b)
{
    size_t i;
    for (i = 0; i < b->size; i++)
        if (b->p[i] != (unsigned char)(b->seed + i * 7))
            return 0;
    return 1;
}

static int bad; /* set by any thread */

static void* worker(void* arg)
{
    struct block live[SLOTS];
    unsigned s = (unsigned)(uintptr_t)arg * 2654435761u + 1;
    long i;
    memset(live, 0, sizeof live);
    for (i = 0; i < rounds; i++) {
        struct block* b = &live[rnd(&s) % SLOTS];
        unsigned op = rnd(&s) % 16;
        if (b->p && !intact(b)) {
            __atomic_store_n(&bad, 1, __ATOMIC_SEQ_CST);
            printf("OS32MALLOC FAIL: a block of %zu bytes changed\n", b->size);
            break;
        }
        if (!b->p) {
            size_t size = some_size(&s);
            if (op < 2) {
                size_t a = (size_t)1 << (rnd(&s) % 15); /* to 16 KB: GC blocks too */
                if (a == OS32_BLOCK && size > OS32_BLOCK)
                    size = OS32_BLOCK - size % 64;
                b->p = (unsigned char*)os32_aligned_alloc(a, size);
                if (b->p && ((uintptr_t)b->p & (a - 1)))
                    __atomic_store_n(&bad, 1, __ATOMIC_SEQ_CST);
            } else if (op < 4) {
                size_t k;
                b->p = (unsigned char*)__wrap_calloc(1, size);
                for (k = 0; b->p && k < size; k++)
                    if (b->p[k])
                        __atomic_store_n(&bad, 1, __ATOMIC_SEQ_CST);
            } else {
                b->p = (unsigned char*)__wrap_malloc(size);
            }
            if (!b->p || ((uintptr_t)b->p & 7) || os32_malloc_usable_size(b->p) < size) {
                __atomic_store_n(&bad, 1, __ATOMIC_SEQ_CST);
                printf("OS32MALLOC FAIL: a block of %zu bytes\n", size);
                break;
            }
            b->size = size;
            b->seed = rnd(&s);
            fill(b);
        } else if (op < 5) {
            /* resize: what it held stays */
            size_t size = op & 1 ? b->size + rnd(&s) % 64 : some_size(&s);
            unsigned char* p = (unsigned char*)__wrap_realloc(b->p, size ? size : 1);
            if (!p) {
                __atomic_store_n(&bad, 1, __ATOMIC_SEQ_CST);
                break;
            }
            b->p = p;
            if (size < b->size)
                b->size = size;
            if (!intact(b)) {
                __atomic_store_n(&bad, 1, __ATOMIC_SEQ_CST);
                printf("OS32MALLOC FAIL: realloc lost what a block held\n");
                break;
            }
            b->size = size;
            fill(b);
        } else if (op < 7) {
            /* swap with the mailbox: another thread frees it */
            unsigned m = rnd(&s) % MAILBOX;
            struct block t;
            pthread_mutex_lock(&mailLock);
            t = mailbox[m];
            mailbox[m] = *b;
            pthread_mutex_unlock(&mailLock);
            *b = t;
            if (b->p && !intact(b)) {
                __atomic_store_n(&bad, 1, __ATOMIC_SEQ_CST);
                printf("OS32MALLOC FAIL: a block changed in the mailbox\n");
                break;
            }
            __wrap_free(b->p);
            b->p = NULL;
        } else {
            __wrap_free(b->p);
            b->p = NULL;
        }
    }
    for (i = 0; i < SLOTS; i++)
        if (live[i].p && !intact(&live[i]))
            __atomic_store_n(&bad, 1, __ATOMIC_SEQ_CST);
    for (i = 0; i < SLOTS; i++) {
        __wrap_free(live[i].p);
        live[i].p = NULL;
    }
    __atomic_add_fetch(&totalOps, rounds, __ATOMIC_SEQ_CST);
    return NULL;
}

static void stress(const char* what)
{
    pthread_t t[THREADS];
    unsigned i;
    long waits = exec_host_waits;
    for (i = 0; i < THREADS; i++)
        CHECK(pthread_create(&t[i], NULL, worker, (void*)(uintptr_t)(i + 1)) == 0);
    for (i = 0; i < THREADS; i++)
        pthread_join(t[i], NULL);
    for (i = 0; i < MAILBOX; i++) {
        if (mailbox[i].p)
            CHECK(intact(&mailbox[i]));
        __wrap_free(mailbox[i].p);
        mailbox[i].p = NULL;
    }
    CHECK(!bad);
    CHECK(os32.head == NULL && os32.waiting == 0 && !os32.held);
    printf("os32 malloc: %s: %d threads, %ld operations, %ld waits on the lock\n", what, THREADS,
        rounds * THREADS, exec_host_waits - waits);
}

/* everything is free: the pages left are the classes' last ones and the pool */
static void check_returned(void)
{
    unsigned kept = class_pages();
    long arenas = __atomic_load_n(&realLive, __ATOMIC_SEQ_CST) - (long)os32.nbarenas;
    CHECK(os32.nbarenas <= 1 && os32.bempty == os32.nbarenas);
    CHECK(os32.nempty < 2 * OS32_PAGES);
    CHECK(arenas <= 5 && (unsigned long)arenas * OS32_PAGES == kept + os32.nempty);
    printf("os32 malloc: all freed: %ld arenas kept, %u pages kept by a class, %u in the pool; %ld calls to libnix\n",
        arenas, kept, os32.nempty, realCalls);
}

int main(int argc, char** argv)
{
    if (argc > 1)
        rounds = atol(argv[1]);
    check_classes();
    check_racing_arenas();
    check_contract();
    check_blocks();
#ifdef OS32_MALLOC_DEBUG
    CHECK(dies(0));
    CHECK(dies(1));
    CHECK(dies(2));
#endif
    stress("stress");
    exec_host_no_signals = 1; /* the lock falls back on SIGF_SINGLE */
    stress("stress without a free signal");
    exec_host_no_signals = 0;
    check_returned();
    printf("os32 malloc: %d checks%s, %s\n", checks,
#ifdef OS32_MALLOC_DEBUG
        " (debug build)",
#else
        "",
#endif
        failed ? "FAILED" : "all passed");
    return failed;
}
