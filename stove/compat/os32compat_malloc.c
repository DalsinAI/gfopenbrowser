/* OpenBrowser (DalsinAI/openamigabrowser). Copyright (c) 2026 AmigaChrome Team. MIT licence, see LICENSE. */
/* malloc, free, calloc, realloc and aligned_alloc for the engine, over libnix.
 *
 * Linked with -Wl,--wrap=malloc,--wrap=calloc,--wrap=realloc,--wrap=free
 * (and aligned_alloc/memalign/posix_memalign defined here). libnix's own
 * malloc() tries exec's Allocate() on each of its chunks in turn, under a
 * semaphore, and its free() walks the chunk list to find the block's chunk;
 * both slow down as the heap grows and fragments. WebKit makes vast numbers
 * of small allocations, so those are kept away from libnix:
 *
 * Small blocks (up to 4096 bytes with the header) come from 31 size classes,
 * 8 bytes apart up to 64 and four to each doubling after that (rounding up
 * by at most 7 bytes, or 25%). Arenas of eight 32 KB pages are taken from libnix; each page
 * serves one class at a time, from its own free list and then from a bump
 * pointer. Every block has a header in front of the pointer handed out:
 *   [ page ][ OS32_SMALL | offset ][ user data ... ]
 * so free() finds the page, and with it the class, without walking anything.
 * offset is how far the pointer is from the start of its slot (more than the
 * header only for aligned blocks).
 *
 * A page with nothing left in use goes back to a pool of empty pages, for
 * any class, unless it is the only page its class has room in (so that a
 * block allocated and freed over and over does not move pages about). An
 * arena with nothing left in use goes back to libnix, unless fewer than
 * eight empty pages would be left in the pool. With every block freed, the
 * pages kept are at most one for each class and 15 in the pool: 1.5 MB, in
 * at most five arenas. In use, the overhead is the header, the rounding
 * and the tail of a page (at most 6%); pooled pages wait for any
 * class, and, as with any allocator that never moves blocks, one block still
 * in use keeps its page and its arena.
 *
 * Large blocks keep libnix's malloc with the old 12-byte header:
 *   [ ... padding ... ][ size ][ raw pointer ][ OS32_LARGE ][ user data ... ]
 * Once the small blocks have gone, libnix's chunk list is short, and its
 * memory goes back to exec when the program ends, which AllocVec's would not.
 *
 * One lock covers the small heap: a bit set by TAS, atomic on one CPU and,
 * as a host atomic, across AmigaChrome's task cores too (until 1 Nov 2026 it
 * was BSET, atomic on one CPU only), and the fast path is that bit and a
 * clear: no Forbid, no library call. A task that finds the bit set queues
 * itself and Wait()s under Forbid, which lets the holder run whatever its
 * priority; unlocking clears the bit and signals the first waiter. Nothing
 * busy-waits, and the holder never waits for anything while holding the lock
 * (libnix is called with the lock released), so the lock cannot deadlock
 * under exec's priority scheduling.
 *
 * -DOS32_MALLOC_DEBUG fills freed blocks with 0xDB (a small block's fill is
 * checked when it is handed out again) and puts a canary after every block,
 * checked by free() and realloc(); a fault calls abort(). */
#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <exec/tasks.h>
#include <proto/exec.h>

void* __real_malloc(size_t);
void __real_free(void*);

#define OS32_LARGE 0xA11C0DEDu
#define OS32_SMALL 0x5A11C000u /* | the offset, in the low 12 bits */
#define OS32_FREED 0xF4EE0000u
#define OS32_PAGE_MAGIC 0x50A6E5u
#define OS32_HDR (2 * sizeof(uintptr_t))
#define OS32_LHDR (3 * sizeof(uintptr_t))
#define OS32_PAGE 32768u
#define OS32_PAGES 8u
#define OS32_SLOT_MAX 4096u
#define OS32_CLASSES 31u
#define OS32_IN_CLASS 1
#define OS32_IN_POOL 2

#ifdef OS32_MALLOC_DEBUG
#define OS32_EXTRA 8u /* the canary after the data; the size asked for in the slot's last word */
static const unsigned char os32_canary[4] = { 0xCA, 0xFE, 0xBA, 0xBE };
#else
#define OS32_EXTRA 0u
#endif

static const uint16_t os32_size[OS32_CLASSES] = {
    16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320,
    384, 448, 512, 640, 768, 896, 1024, 1280, 1536, 1792, 2048, 2560, 3072, 3584, 4096
};

struct os32_arena;

struct os32_page {
    struct os32_page *next, *prev; /* the class's pages with room, or the empty pages */
    char* base;
    void* free;                    /* freed slots */
    char *bump, *end;              /* slots never handed out */
    struct os32_arena* arena;
    uint32_t magic;
    uint16_t live;                 /* blocks in use */
    uint8_t cls;
    uint8_t listed;                /* OS32_IN_CLASS, OS32_IN_POOL or 0 */
};

/* at the start of what libnix's malloc() returned; the pages follow */
struct os32_arena {
    unsigned empty;                /* pages with nothing in use */
    unsigned pooled;               /* of those, in the pool */
    struct os32_page page[OS32_PAGES];
};

struct os32_waiter {
    struct os32_waiter* next;
    struct Task* task;
    ULONG mask;
    int queued;
};

static struct {
    unsigned char held;
    int waiting;                   /* tasks in the queue */
    struct os32_waiter *head, *tail;
    struct os32_page* cls[OS32_CLASSES]; /* pages with a slot to give */
    struct os32_page* empty;       /* the pool */
    unsigned nempty;
} os32;

/* The lock. On the host (the tests) the same code runs on real threads, so
 * the bit and the queue length are atomic there, and "release, then look for
 * waiters" against "queue, then try again" holds on any number of CPUs. */
#ifdef __amigaos__
static inline int os32_try(void)
{
    unsigned char was;
    /* TAS, not BSET (1 Nov 2026): BSET is atomic against task switches on
     * one CPU only; AmigaChrome's task cores run tasks on other host cores at
     * the same time, and there TAS (with CAS and CAS2) is a host atomic,
     * BSET is not. The heap is in fast RAM (the engine needs tens of MB), so
     * TAS's read-modify-write cycle never reaches the chip bus. Bit 7 is the
     * only bit used, so N after TAS says what BSET's Z said. */
    __asm__ __volatile__("tas %1\n\tsmi %0" : "=d"(was), "+m"(os32.held) : : "cc", "memory");
    return !was;
}
#define OS32_RELEASE() __asm__ __volatile__("clr.b %0" : "=m"(os32.held) : : "memory")
#define OS32_FENCE() __asm__ __volatile__("" : : : "memory")
#define OS32_BARRIER() OS32_FENCE()
#define OS32_WAITING() (*(volatile int*)&os32.waiting)
#define OS32_ADD_WAITING(n) (os32.waiting += (n))
#else
static inline int os32_try(void) { return !__atomic_test_and_set(&os32.held, __ATOMIC_SEQ_CST); }
#define OS32_RELEASE() __atomic_clear(&os32.held, __ATOMIC_SEQ_CST)
#define OS32_FENCE() __atomic_thread_fence(__ATOMIC_SEQ_CST)
#define OS32_BARRIER() ((void)0) /* the atomics are sequentially consistent */
#define OS32_WAITING() __atomic_load_n(&os32.waiting, __ATOMIC_SEQ_CST)
#define OS32_ADD_WAITING(n) __atomic_add_fetch(&os32.waiting, (n), __ATOMIC_SEQ_CST)
#endif

/* contended: queue and sleep until an unlock signals. Forbid keeps the queue
 * whole; Wait() breaks it, so the holder runs. */
static void os32_lock_slow(void)
{
    struct os32_waiter w;
    BYTE bit;
    Forbid();
    bit = AllocSignal(-1);
    w.mask = bit >= 0 ? 1UL << bit : SIGF_SINGLE; /* as exec's semaphores, if none is free */
    w.task = FindTask(NULL);
    w.queued = 0;
    SetSignal(0, w.mask);
    for (;;) {
        if (os32_try())
            break;
        if (!w.queued) {
            w.next = NULL;
            if (os32.tail)
                os32.tail->next = &w;
            else
                os32.head = &w;
            os32.tail = &w;
            w.queued = 1;
            OS32_ADD_WAITING(1);
        }
        OS32_FENCE();
        if (os32_try()) /* released before the unlocker could see us queued */
            break;
        Wait(w.mask);
    }
    if (w.queued) {
        struct os32_waiter **q = &os32.head, *prev = NULL;
        while (*q != &w) {
            prev = *q;
            q = &(*q)->next;
        }
        *q = w.next;
        if (os32.tail == &w)
            os32.tail = prev;
        OS32_ADD_WAITING(-1);
    }
    SetSignal(0, w.mask);
    if (bit >= 0)
        FreeSignal(bit);
    Permit();
}

static void os32_wake(void)
{
    struct os32_waiter* w;
    Forbid();
    w = os32.head;
    if (w) {
        os32.head = w->next;
        if (!os32.head)
            os32.tail = NULL;
        w->queued = 0;
        OS32_ADD_WAITING(-1);
        Signal(w->task, w->mask);
    }
    Permit();
}

static inline void os32_lock(void)
{
    if (!os32_try())
        os32_lock_slow();
    OS32_BARRIER();
}

static inline void os32_unlock(void)
{
    OS32_BARRIER();
    OS32_RELEASE();
    OS32_BARRIER();
    if (OS32_WAITING())
        os32_wake();
}

/* the class for a slot of n bytes, 1 <= n <= OS32_SLOT_MAX */
static inline unsigned os32_class(size_t n)
{
    unsigned b;
    if (n <= 64)
        return n <= 16 ? 0 : (unsigned)(n - 9) >> 3;
    b = 31 - __builtin_clz((unsigned)n - 1);
    return 3 + 4 * (b - 6) + ((unsigned)(n - 1) >> (b - 2));
}

static void os32_unlist(struct os32_page** list, struct os32_page* pg)
{
    if (pg->prev)
        pg->prev->next = pg->next;
    else
        *list = pg->next;
    if (pg->next)
        pg->next->prev = pg->prev;
    if (pg->listed == OS32_IN_POOL) {
        pg->arena->pooled--;
        os32.nempty--;
    }
    pg->listed = 0;
}

static void os32_list(struct os32_page** list, struct os32_page* pg, uint8_t where)
{
    pg->prev = NULL;
    pg->next = *list;
    if (*list)
        (*list)->prev = pg;
    *list = pg;
    pg->listed = where;
    if (where == OS32_IN_POOL) {
        pg->arena->pooled++;
        os32.nempty++;
    }
}

#ifdef OS32_MALLOC_DEBUG
static void os32_mark(void* p, size_t size, char* slotEnd)
{
    memcpy((char*)p + size, os32_canary, 4);
    if (slotEnd)
        ((uint32_t*)slotEnd)[-1] = (uint32_t)size;
    else
        ((size_t*)p)[-3] = size;
}

static size_t os32_asked(void* p, char* slotEnd)
{
    size_t size = slotEnd ? ((uint32_t*)slotEnd)[-1] : ((size_t*)p)[-3];
    if (memcmp((char*)p + size, os32_canary, 4))
        abort(); /* written past its end */
    return size;
}

/* a freed slot still holds its fill */
static void os32_check_fill(char* slot, unsigned size)
{
    unsigned char* q;
    for (q = (unsigned char*)slot + OS32_HDR; q < (unsigned char*)slot + size; q++)
        if (*q != 0xDB)
            abort(); /* written after free() */
}
#endif

/* a slot of class c, with the lock held; NULL when no page is free */
static char* os32_take(unsigned c)
{
    struct os32_page* pg = os32.cls[c];
    char* slot;
    if (!pg) {
        unsigned size = os32_size[c];
        pg = os32.empty;
        if (!pg)
            return NULL;
        os32_unlist(&os32.empty, pg);
#ifdef OS32_MALLOC_DEBUG
        for (slot = pg->base; slot < pg->bump; slot += os32_size[pg->cls]) /* its last class's */
            os32_check_fill(slot, os32_size[pg->cls]);
#endif
        pg->cls = (uint8_t)c;
        pg->free = NULL;
        pg->bump = pg->base;
        pg->end = pg->base + OS32_PAGE / size * size;
        os32_list(&os32.cls[c], pg, OS32_IN_CLASS);
    }
    if (pg->free) {
        slot = (char*)pg->free;
        pg->free = *(void**)slot;
    } else {
        slot = pg->bump;
        pg->bump += os32_size[c];
#ifdef OS32_MALLOC_DEBUG
        ((uintptr_t*)slot)[OS32_HDR / sizeof(uintptr_t) - 1] = 0; /* not a freed slot's fill */
#endif
    }
    if (!pg->live++)
        pg->arena->empty--;
    if (!pg->free && pg->bump == pg->end)
        os32_unlist(&os32.cls[c], pg);
    *(struct os32_page**)slot = pg; /* for the caller's header */
    return slot;
}

/* a new arena's pages into the pool; 0 when libnix has no memory */
static int os32_grow(void)
{
    struct os32_arena* a = (struct os32_arena*)__real_malloc(sizeof *a + OS32_PAGES * OS32_PAGE + 8);
    char* base;
    unsigned i;
    if (!a)
        return 0;
    a->empty = OS32_PAGES;
    a->pooled = 0;
    base = (char*)(((uintptr_t)(a + 1) + 7) & ~(uintptr_t)7);
    os32_lock();
    for (i = 0; i < OS32_PAGES; i++) {
        struct os32_page* pg = &a->page[i];
        pg->base = base + i * OS32_PAGE;
        pg->arena = a;
        pg->magic = OS32_PAGE_MAGIC;
        pg->live = 0;
        pg->cls = 0;
        pg->bump = pg->base;
        os32_list(&os32.empty, pg, OS32_IN_POOL);
    }
    os32_unlock();
    return 1;
}

static void* os32_small(size_t alignment, size_t size)
{
    unsigned c = os32_class(OS32_HDR + (alignment - 8) + size + OS32_EXTRA);
    char *slot, *user;
    os32_lock();
    while (!(slot = os32_take(c))) {
        os32_unlock();
        if (!os32_grow())
            return NULL;
        os32_lock();
    }
    os32_unlock();
#ifdef OS32_MALLOC_DEBUG
    if (((uintptr_t*)slot)[OS32_HDR / sizeof(uintptr_t) - 1] == OS32_FREED)
        os32_check_fill(slot, os32_size[c]);
#endif
    user = (char*)(((uintptr_t)slot + OS32_HDR + alignment - 1) & ~(uintptr_t)(alignment - 1));
    ((uintptr_t*)user)[-2] = (uintptr_t) * (struct os32_page**)slot;
    ((uintptr_t*)user)[-1] = OS32_SMALL | (uintptr_t)(user - slot);
#ifdef OS32_MALLOC_DEBUG
    os32_mark(user, size, slot + os32_size[c]);
#endif
    return user;
}

static void* os32_large(size_t alignment, size_t size)
{
    uintptr_t raw, user;
    if (size > (size_t)-1 - alignment - OS32_LHDR - OS32_EXTRA)
        return NULL;
    raw = (uintptr_t)__real_malloc(size + alignment + OS32_LHDR + OS32_EXTRA);
    if (!raw)
        return NULL;
    user = (raw + OS32_LHDR + alignment - 1) & ~(uintptr_t)(alignment - 1);
    ((uintptr_t*)user)[-1] = OS32_LARGE;
    ((uintptr_t*)user)[-2] = raw;
    ((size_t*)user)[-3] = size;
#ifdef OS32_MALLOC_DEBUG
    os32_mark((void*)user, size, NULL);
#endif
    return (void*)user;
}

static void* os32_alloc(size_t alignment, size_t size)
{
    if (alignment < 8)
        alignment = 8;
    if (alignment & (alignment - 1))
        return NULL;
    if (alignment <= 2048 && size <= OS32_SLOT_MAX - OS32_HDR - OS32_EXTRA - (alignment - 8))
        return os32_small(alignment, size);
    return os32_large(alignment, size);
}

/* the page of a small block, or NULL when p is not one of ours */
static struct os32_page* os32_page_of(void* p, char** slot)
{
    uintptr_t tag = ((uintptr_t*)p)[-1];
    struct os32_page* pg;
    if ((tag & ~(uintptr_t)0xFFF) != OS32_SMALL)
        return NULL;
    pg = (struct os32_page*)((uintptr_t*)p)[-2];
    *slot = (char*)p - (tag & 0xFFF);
    if (!pg || pg->magic != OS32_PAGE_MAGIC || (uintptr_t)(*slot - pg->base) >= OS32_PAGE)
        return NULL;
    return pg;
}

void* __wrap_malloc(size_t size) { return os32_alloc(8, size); }

void __wrap_free(void* p)
{
    struct os32_page* pg;
    struct os32_arena* gone = NULL;
    char* slot;
    unsigned c;
    if (!p)
        return;
    if (((uintptr_t*)p)[-1] == OS32_LARGE) {
#ifdef OS32_MALLOC_DEBUG
        memset(p, 0xDB, os32_asked(p, NULL));
#endif
        ((uintptr_t*)p)[-1] = 0;
        __real_free((void*)((uintptr_t*)p)[-2]);
        return;
    }
    pg = os32_page_of(p, &slot);
    if (!pg)
        return; /* not ours: leak rather than corrupt libnix's heap */
    c = pg->cls;
    ((uintptr_t*)p)[-1] = OS32_FREED; /* so a second free() is not ours */
#ifdef OS32_MALLOC_DEBUG
    os32_asked(p, slot + os32_size[c]);
    memset(slot, 0xDB, os32_size[c]);
#endif
    ((uintptr_t*)slot)[OS32_HDR / sizeof(uintptr_t) - 1] = OS32_FREED;
    os32_lock();
    *(void**)slot = pg->free;
    pg->free = slot;
    if (!pg->listed)
        os32_list(&os32.cls[c], pg, OS32_IN_CLASS);
    if (!--pg->live) {
        struct os32_arena* a = pg->arena;
        if (pg->next || pg->prev) { /* not its class's only page with room */
            os32_unlist(&os32.cls[c], pg);
            os32_list(&os32.empty, pg, OS32_IN_POOL);
        }
        if (++a->empty == OS32_PAGES && os32.nempty - a->pooled >= OS32_PAGES) {
            unsigned i;
            for (i = 0; i < OS32_PAGES; i++) {
                struct os32_page* q = &a->page[i];
                q->magic = 0;
                os32_unlist(q->listed == OS32_IN_POOL ? &os32.empty : &os32.cls[q->cls], q);
            }
            gone = a;
        }
    }
    os32_unlock();
    if (gone)
        __real_free(gone);
}

void* __wrap_calloc(size_t n, size_t size)
{
    void* p;
    if (size && n > (size_t)-1 / size)
        return NULL;
    p = os32_alloc(8, n * size);
    if (p)
        memset(p, 0, n * size);
    return p;
}

void* __wrap_realloc(void* old, size_t size)
{
    struct os32_page* pg;
    char* slot;
    size_t oldSize;
    void* p;
    if (!old)
        return os32_alloc(8, size);
    if (!size) {
        __wrap_free(old);
        return NULL;
    }
    if (((uintptr_t*)old)[-1] == OS32_LARGE) {
#ifdef OS32_MALLOC_DEBUG
        oldSize = os32_asked(old, NULL);
        if (size <= oldSize && size >= oldSize / 2) {
            os32_mark(old, size, NULL); /* the canary moves in; the rest is lost */
            return old;
        }
#else
        oldSize = ((size_t*)old)[-3];
        if (size <= oldSize && size >= oldSize / 2)
            return old;
#endif
    } else if ((pg = os32_page_of(old, &slot))) {
        size_t need = (size_t)((char*)old - slot) + size + OS32_EXTRA;
        char* slotEnd = slot + os32_size[pg->cls];
#ifdef OS32_MALLOC_DEBUG
        oldSize = os32_asked(old, slotEnd);
#else
        oldSize = (size_t)(slotEnd - (char*)old);
#endif
        if (need <= OS32_SLOT_MAX && os32_class(need) == pg->cls) { /* the same class */
#ifdef OS32_MALLOC_DEBUG
            os32_mark(old, size, slotEnd);
#endif
            return old;
        }
    } else {
        return NULL; /* not ours: its size is unknown */
    }
    p = os32_alloc(8, size);
    if (!p)
        return NULL;
    memcpy(p, old, oldSize < size ? oldSize : size);
    __wrap_free(old);
    return p;
}

void* aligned_alloc(size_t alignment, size_t size) { return os32_alloc(alignment, size); }
void* memalign(size_t alignment, size_t size) { return os32_alloc(alignment, size); }

int posix_memalign(void** out, size_t alignment, size_t size)
{
    void* p;
    if (alignment < sizeof(void*) || (alignment & (alignment - 1)))
        return EINVAL;
    p = os32_alloc(alignment, size);
    if (!p)
        return ENOMEM;
    *out = p;
    return 0;
}

/* what the block can hold: at least what was asked for */
size_t os32_malloc_usable_size(void* p)
{
    struct os32_page* pg;
    char* slot;
    if (!p)
        return 0;
    if (((uintptr_t*)p)[-1] == OS32_LARGE)
        return ((size_t*)p)[-3];
    pg = os32_page_of(p, &slot);
    if (!pg)
        return 0;
#ifdef OS32_MALLOC_DEBUG
    return ((uint32_t*)(slot + os32_size[pg->cls]))[-1];
#else
    return (size_t)(slot + os32_size[pg->cls] - (char*)p);
#endif
}
