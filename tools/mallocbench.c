/* GFOpenBrowser (DalsinAI/openamigabrowser). Copyright (c) 2026 AmigaChrome Team. MIT licence, see LICENSE. */
/* MallocBench: the engine's allocator (stove/compat/os32compat_malloc.c)
 * against the one it replaced (libnix's malloc behind a 12-byte header, kept
 * here) and libnix's malloc alone, on sizes as WebKit asks for them.
 *
 *   MallocBench [pairs] [live]       (default 200000 and 8192)
 *
 * pairs: malloc() then free() at once, the same sizes for each allocator.
 * churn: a working set of [live] blocks, one freed and one allocated for
 *        each pair, so the heap fragments as a page's does.
 * Times are timer.device's (GetSysTime), in whole microseconds; one line
 * for each allocator and test:
 *   MALLOCBENCH <allocator> <test> pairs=<n> us=<t> ns_per_pair=<t*1000/n>
 * Built by scripts/build-openbrowser.sh (TESTS=1), linked with the
 * toolchain's --wrap=malloc,free flags as the engine is. On the host
 * (tests/run-host-tests.sh) libnix's malloc is the host's. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void* __real_malloc(size_t);
void __real_free(void*);
void* __wrap_malloc(size_t);
void __wrap_free(void*);

#ifdef __amigaos__
#define __USE_NEW_TIMEVAL__
#include <devices/timer.h>
#include <exec/memory.h>
#include <proto/exec.h>
#include <proto/timer.h>

struct Device* TimerBase;
static struct MsgPort* timerPort;
static struct TimeRequest* timerRequest;

static int clock_open(void)
{
    timerPort = CreateMsgPort();
    timerRequest = timerPort ? (struct TimeRequest*)CreateIORequest(timerPort, sizeof *timerRequest) : NULL;
    if (!timerRequest || OpenDevice((CONST_STRPTR)TIMERNAME, UNIT_MICROHZ, &timerRequest->tr_node, 0))
        return 0;
    TimerBase = timerRequest->tr_node.io_Device;
    return 1;
}

static void clock_close(void)
{
    if (TimerBase)
        CloseDevice(&timerRequest->tr_node);
    DeleteIORequest(&timerRequest->tr_node);
    DeleteMsgPort(timerPort);
}

static unsigned long long now_us(void)
{
    struct TimeVal tv;
    GetSysTime(&tv);
    return (unsigned long long)tv.tv_secs * 1000000u + tv.tv_micro;
}
#else
#include <time.h>

void* __real_malloc(size_t n) { return malloc(n); }
void __real_free(void* p) { free(p); }

static int clock_open(void) { return 1; }
static void clock_close(void) {}

static unsigned long long now_us(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (unsigned long long)ts.tv_sec * 1000000u + (unsigned long long)ts.tv_nsec / 1000u;
}
#endif

/* the allocator before this one: libnix's malloc, 12-byte header, 8-byte aligned */
static void* old_malloc(size_t size)
{
    uintptr_t raw = (uintptr_t)__real_malloc(size + 8 + 3 * sizeof(uintptr_t)), user;
    if (!raw)
        return NULL;
    user = (raw + 3 * sizeof(uintptr_t) + 7) & ~(uintptr_t)7;
    ((uint32_t*)user)[-1] = 0xA11C0DEDu;
    ((uintptr_t*)user)[-2] = raw;
    ((size_t*)user)[-3] = size;
    return (void*)user;
}

static void old_free(void* p)
{
    if (p)
        __real_free((void*)((uintptr_t*)p)[-2]);
}

static const struct {
    const char* name;
    void* (*alloc)(size_t);
    void (*release)(void*);
} allocators[] = {
    { "new", __wrap_malloc, __wrap_free },
    { "old", old_malloc, old_free },
    { "libnix", __real_malloc, __real_free },
};

#define SIZES 4096
static size_t sizes[SIZES];

static unsigned rnd(unsigned* s)
{
    *s = *s * 1103515245u + 12345u;
    return *s >> 8;
}

/* most of WebKit's blocks are nodes, strings and small vectors; some are
 * growth buffers; a few are big */
static void make_sizes(void)
{
    unsigned s = 1, i;
    for (i = 0; i < SIZES; i++) {
        unsigned r = rnd(&s) % 100;
        if (r < 45)
            sizes[i] = 8 + rnd(&s) % 57;
        else if (r < 75)
            sizes[i] = 65 + rnd(&s) % 192;
        else if (r < 92)
            sizes[i] = 257 + rnd(&s) % 768;
        else if (r < 99)
            sizes[i] = 1025 + rnd(&s) % 3072;
        else
            sizes[i] = 4097 + rnd(&s) % 28672;
    }
}

static void report(const char* name, const char* test, unsigned long pairs, unsigned long long us)
{
    printf("MALLOCBENCH %s %s pairs=%lu us=%llu ns_per_pair=%llu\n", name, test, pairs, us,
        pairs ? us * 1000u / pairs : 0);
    fflush(stdout);
}

int main(int argc, char** argv)
{
    unsigned long pairs = argc > 1 ? strtoul(argv[1], NULL, 10) : 200000;
    unsigned long live = argc > 2 ? strtoul(argv[2], NULL, 10) : 8192;
    void** set;
    unsigned a;
    if (!pairs || !live) {
        fprintf(stderr, "MallocBench [pairs] [live]\n");
        return 20;
    }
    if (!clock_open()) {
        fprintf(stderr, "MallocBench: no timer.device\n");
        clock_close();
        return 20;
    }
    set = (void**)calloc(live, sizeof *set);
    if (!set) {
        clock_close();
        return 20;
    }
    make_sizes();
    for (a = 0; a < sizeof allocators / sizeof *allocators; a++) {
        void* (*alloc)(size_t) = allocators[a].alloc;
        void (*release)(void*) = allocators[a].release;
        unsigned long i, failed = 0;
        unsigned long long t;
        unsigned s = 7;

        t = now_us();
        for (i = 0; i < pairs; i++) {
            void* p = alloc(sizes[i & (SIZES - 1)]);
            if (p)
                *(volatile char*)p = 0;
            else
                failed++;
            release(p);
        }
        report(allocators[a].name, "pairs", pairs, now_us() - t);

        for (i = 0; i < live; i++)
            set[i] = alloc(sizes[rnd(&s) & (SIZES - 1)]);
        t = now_us();
        for (i = 0; i < pairs; i++) {
            unsigned long k = rnd(&s) % live;
            release(set[k]);
            set[k] = alloc(sizes[(i * 7) & (SIZES - 1)]);
            if (!set[k])
                failed++;
        }
        report(allocators[a].name, "churn", pairs, now_us() - t);
        for (i = 0; i < live; i++) {
            release(set[i]);
            set[i] = NULL;
        }
        if (failed)
            printf("MALLOCBENCH %s: %lu allocations failed\n", allocators[a].name, failed);
    }
    free(set);
    clock_close();
    return 0;
}
