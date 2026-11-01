/* unicode.library: what ICU needs from a C library, safe to share.
 * Copyright (c) 2026 AmigaChrome Team. MIT.
 *
 * ICU is built as it is for programs (scripts/build-icu.sh) and links
 * libnix, libm and libstdc++ for the pure parts: strings, numbers, maths,
 * qsort, snprintf, localtime_r. Whatever in those keeps state for one
 * program is replaced here, because the library is called by many
 * programs at once and outlives each of them:
 *
 *  - memory: malloc and its kin take blocks from one Exec pool owned by
 *    the library, under a semaphore (ICU's new and delete come here too);
 *  - threads: the pthread calls libstdc++ makes for std::mutex,
 *    std::condition_variable and std::call_once, on Exec semaphores and
 *    task signals, with per-task keys;
 *  - atomics: the few that m68k gcc leaves to libatomic, under Forbid;
 *  - files: fopen fails at once. All of ICU's data is in the common block
 *    the library loads (unicode_lib.c), so ICU never needs a file, and
 *    looking for one would cost a disk access per lookup;
 *  - getenv reads DOS variables into the library's own buffers;
 *  - setlocale answers "C" (ICU then takes the root/English default it
 *    took inside the engine), abort stops the task, and nothing is ever
 *    registered to run at exit.
 *
 * The libnix startup code is not linked, so the libraries libnix's maths
 * and locale code call are opened here, and libstdc++'s constructors are
 * run here (ul_runtime_init).
 */
#include <exec/types.h>
#include <exec/memory.h>
#include <exec/semaphores.h>
#include <exec/tasks.h>
#include <exec/lists.h>
#include <dos/var.h>
#include <proto/exec.h>
#include <proto/dos.h>

#include <errno.h>
/* pthread.h's mutexes and conditions in Exec's own types */
#define __PTHREAD_SEMAPHORE_TYPE struct SignalSemaphore
#define __PTHREAD_MINLIST_TYPE struct MinList
#include <pthread.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "ul_runtime.h"

extern struct ExecBase *SysBase;
extern struct DosLibrary *DOSBase;

/* libnix's maths and locale code calls these through their bases. */
extern struct Library *LocaleBase, *MathIeeeDoubBasBase, *MathIeeeDoubTransBase,
    *MathIeeeSingTransBase, *UtilityBase;
extern void tzset(void);

/* NewList without amiga.lib */
static void initList(struct MinList *l)
{
    l->mlh_Head = (struct MinNode *)&l->mlh_Tail;
    l->mlh_Tail = NULL;
    l->mlh_TailPred = (struct MinNode *)&l->mlh_Head;
}

/* ---- Memory ----------------------------------------------------------------- */

static APTR pool;
static struct SignalSemaphore poolLock;

/* Each block starts with its size, which keeps the block 8-byte aligned. */
#define HEADER 8

void *malloc(size_t size)
{
    ULONG *block;

    if (size > 0x7fffffffUL - HEADER)
        return NULL;
    ObtainSemaphore(&poolLock);
    block = AllocPooled(pool, size + HEADER);
    ReleaseSemaphore(&poolLock);
    if (!block) {
        errno = ENOMEM;
        return NULL;
    }
    block[0] = size + HEADER;
    return (UBYTE *)block + HEADER;
}

void free(void *mem)
{
    ULONG *block;

    if (!mem)
        return;
    block = (ULONG *)((UBYTE *)mem - HEADER);
    ObtainSemaphore(&poolLock);
    FreePooled(pool, block, block[0]);
    ReleaseSemaphore(&poolLock);
}

void *calloc(size_t n, size_t size)
{
    void *mem;

    if (size && n > (size_t)-1 / size)
        return NULL;
    if ((mem = malloc(n * size)))
        memset(mem, 0, n * size);
    return mem;
}

void *realloc(void *mem, size_t size)
{
    void *bigger;
    size_t old;

    if (!mem)
        return malloc(size);
    if (size == 0) {
        free(mem);
        return NULL;
    }
    old = ((ULONG *)((UBYTE *)mem - HEADER))[0] - HEADER;
    if (size <= old && size >= old / 2)
        return mem;
    if (!(bigger = malloc(size)))
        return NULL;
    memcpy(bigger, mem, old < size ? old : size);
    free(mem);
    return bigger;
}

/* ---- Threads ------------------------------------------------------------------ */

/* A mutex is an Exec semaphore, made on first use: PTHREAD_MUTEX_INITIALIZER
 * and C++'s constant-initialised std::mutex leave it all zero. */
static void mutexReady(pthread_mutex_t *m)
{
    if (m->semaphore.ss_Link.ln_Type != NT_SIGNALSEM) {
        Forbid();
        if (m->semaphore.ss_Link.ln_Type != NT_SIGNALSEM)
            InitSemaphore(&m->semaphore);
        Permit();
    }
}

int pthread_mutex_init(pthread_mutex_t *m, const pthread_mutexattr_t *attr)
{
    memset(m, 0, sizeof *m);
    if (attr)
        m->kind = attr->kind;
    InitSemaphore(&m->semaphore);
    return 0;
}

int pthread_mutex_destroy(pthread_mutex_t *m)
{
    (void)m;
    return 0;
}

int pthread_mutex_lock(pthread_mutex_t *m)
{
    mutexReady(m);
    ObtainSemaphore(&m->semaphore);
    return 0;
}

int pthread_mutex_trylock(pthread_mutex_t *m)
{
    mutexReady(m);
    return AttemptSemaphore(&m->semaphore) ? 0 : EBUSY;
}

int pthread_mutex_unlock(pthread_mutex_t *m)
{
    ReleaseSemaphore(&m->semaphore);
    return 0;
}

/* A condition's waiters are kept in its list, under Forbid. A waiter
 * sleeps on a signal of its own until a signal or broadcast marks it. */
struct Waiter {
    struct MinNode node;
    struct Task *task;
    ULONG sigMask;
    volatile LONG woken;
};

static void condReady(pthread_cond_t *c)
{
    if (!c->waiters.mlh_Head) {
        Forbid();
        if (!c->waiters.mlh_Head)
            initList(&c->waiters);
        Permit();
    }
}

int pthread_cond_init(pthread_cond_t *c, const pthread_condattr_t *attr)
{
    (void)attr;
    memset(c, 0, sizeof *c);
    initList(&c->waiters);
    return 0;
}

int pthread_cond_destroy(pthread_cond_t *c)
{
    (void)c;
    return 0;
}

int pthread_cond_wait(pthread_cond_t *c, pthread_mutex_t *m)
{
    struct Waiter w;
    BYTE bit = AllocSignal(-1);

    condReady(c);
    w.task = FindTask(NULL);
    /* No signal free: the one Exec's semaphores use, which is never
     * pending while the task is not in ObtainSemaphore. */
    w.sigMask = bit >= 0 ? 1UL << bit : SIGF_SINGLE;
    w.woken = 0;
    SetSignal(0, w.sigMask);
    Forbid();
    AddTail((struct List *)&c->waiters, (struct Node *)&w.node);
    Permit();
    pthread_mutex_unlock(m);
    while (!w.woken)
        Wait(w.sigMask);
    pthread_mutex_lock(m);
    if (bit >= 0)
        FreeSignal(bit);
    return 0;
}

static void wake(pthread_cond_t *c, int all)
{
    struct Waiter *w;

    condReady(c);
    Forbid();
    while ((w = (struct Waiter *)RemHead((struct List *)&c->waiters))) {
        w->woken = 1;
        Signal(w->task, w->sigMask);
        if (!all)
            break;
    }
    Permit();
}

int pthread_cond_signal(pthread_cond_t *c)
{
    wake(c, 0);
    return 0;
}

int pthread_cond_broadcast(pthread_cond_t *c)
{
    wake(c, 1);
    return 0;
}

/* pthread_once: under one semaphore, which a task may take again, so an
 * init routine may itself run another once. */
static struct SignalSemaphore onceLock;

int pthread_once(pthread_once_t *once, void (*init)(void))
{
    if (once->done)
        return 0;
    ObtainSemaphore(&onceLock);
    if (!once->done) {
        init();
        once->done = 1;
    }
    ReleaseSemaphore(&onceLock);
    return 0;
}

/* Keys: each task's values in a record of its own, found by its task
 * pointer. Records are taken from the pool and go with it. */
#define KEYS 8

struct TaskValues {
    struct MinNode node;
    struct Task *task;
    void *value[KEYS];
};

static struct MinList taskValues;
static struct SignalSemaphore keyLock;
static ULONG keysUsed;
static void (*keyDestructor[KEYS])(void *);

int pthread_key_create(pthread_key_t *key, void (*destructor)(void *))
{
    int k;

    ObtainSemaphore(&keyLock);
    for (k = 0; k < KEYS && (keysUsed & (1UL << k)); k++)
        ;
    if (k < KEYS) {
        keysUsed |= 1UL << k;
        keyDestructor[k] = destructor;
    }
    ReleaseSemaphore(&keyLock);
    if (k == KEYS)
        return EAGAIN;
    *key = (pthread_key_t)k;
    return 0;
}

int pthread_key_delete(pthread_key_t key)
{
    struct TaskValues *tv;

    if ((ULONG)key >= KEYS)
        return EINVAL;
    ObtainSemaphore(&keyLock);
    keysUsed &= ~(1UL << key);
    for (tv = (struct TaskValues *)taskValues.mlh_Head; tv->node.mln_Succ;
         tv = (struct TaskValues *)tv->node.mln_Succ)
        tv->value[key] = NULL;
    ReleaseSemaphore(&keyLock);
    return 0;
}

static struct TaskValues *valuesOf(struct Task *me, int make)
{
    struct TaskValues *tv;

    for (tv = (struct TaskValues *)taskValues.mlh_Head; tv->node.mln_Succ;
         tv = (struct TaskValues *)tv->node.mln_Succ)
        if (tv->task == me)
            return tv;
    if (!make || !(tv = calloc(1, sizeof *tv)))
        return NULL;
    tv->task = me;
    AddHead((struct List *)&taskValues, (struct Node *)&tv->node);
    return tv;
}

void *pthread_getspecific(pthread_key_t key)
{
    struct TaskValues *tv;
    void *value = NULL;

    if ((ULONG)key >= KEYS)
        return NULL;
    ObtainSemaphore(&keyLock);
    if ((tv = valuesOf(FindTask(NULL), 0)))
        value = tv->value[key];
    ReleaseSemaphore(&keyLock);
    return value;
}

int pthread_setspecific(pthread_key_t key, const void *value)
{
    struct TaskValues *tv;

    if ((ULONG)key >= KEYS)
        return EINVAL;
    ObtainSemaphore(&keyLock);
    if ((tv = valuesOf(FindTask(NULL), value != NULL)))
        tv->value[key] = (void *)value;
    ReleaseSemaphore(&keyLock);
    return tv || !value ? 0 : ENOMEM;
}

/* libstdc++ takes threads to be on when this exists; nothing cancels. */
int pthread_cancel(pthread_t thread)
{
    (void)thread;
    return ENOSYS;
}

/* ---- Atomics ------------------------------------------------------------------- */

/* What m68k gcc leaves to libatomic for 4-byte objects. Forbid is enough:
 * no interrupt code touches ICU's counters. */
unsigned int __atomic_fetch_add_4(volatile void *p, unsigned int v, int order)
{
    unsigned int old;

    (void)order;
    Forbid();
    old = *(volatile unsigned int *)p;
    *(volatile unsigned int *)p = old + v;
    Permit();
    return old;
}

unsigned int __atomic_fetch_sub_4(volatile void *p, unsigned int v, int order)
{
    unsigned int old;

    (void)order;
    Forbid();
    old = *(volatile unsigned int *)p;
    *(volatile unsigned int *)p = old - v;
    Permit();
    return old;
}

unsigned int __atomic_exchange_4(volatile void *p, unsigned int v, int order)
{
    unsigned int old;

    (void)order;
    Forbid();
    old = *(volatile unsigned int *)p;
    *(volatile unsigned int *)p = v;
    Permit();
    return old;
}

_Bool __atomic_compare_exchange_4(volatile void *p, void *expected, unsigned int desired,
                                  _Bool weak, int success, int failure)
{
    unsigned int old;
    _Bool same;

    (void)weak, (void)success, (void)failure;
    Forbid();
    old = *(volatile unsigned int *)p;
    if ((same = old == *(unsigned int *)expected))
        *(volatile unsigned int *)p = desired;
    else
        *(unsigned int *)expected = old;
    Permit();
    return same;
}

/* ---- Files, environment, locale, ending --------------------------------------- */

FILE *fopen(const char *name, const char *mode)
{
    (void)name, (void)mode;
    errno = ENOENT;
    return NULL;
}

/* Never reached: no FILE is ever opened. */
int fclose(FILE *f) { (void)f; return EOF; }
size_t fread(void *buf, size_t size, size_t n, FILE *f) { (void)buf, (void)size, (void)n, (void)f; return 0; }
int fseek(FILE *f, long offset, int whence) { (void)f, (void)offset, (void)whence; return -1; }
long ftell(FILE *f) { (void)f; return -1; }
/* libm's string printf and scanf share libnix's stream code, which reads
 * and writes descriptors only for real files: there are none. */
int read(int fd, void *buf, unsigned int n) { (void)fd, (void)buf, (void)n; return -1; }
int write(int fd, const void *buf, unsigned int n) { (void)fd, (void)buf, (void)n; return -1; }
/* libnix's list of open files, which its stream code looks at: empty. */
struct MinList ___filelist = { (struct MinNode *)&___filelist.mlh_Tail, NULL, (struct MinNode *)&___filelist.mlh_Head };

/* Each answer in a buffer of its own, used round in turn: ICU copies what
 * it keeps. */
#define ENV_BUFFERS 8
#define ENV_SIZE 128
static char envBuffer[ENV_BUFFERS][ENV_SIZE];
static ULONG envNext;
static struct SignalSemaphore envLock;

char *getenv(const char *name)
{
    struct Task *me = FindTask(NULL);
    char *buf;
    LONG len;

    if (me->tc_Node.ln_Type != NT_PROCESS)
        return NULL;
    ObtainSemaphore(&envLock);
    buf = envBuffer[envNext++ % ENV_BUFFERS];
    len = GetVar((STRPTR)name, (STRPTR)buf, ENV_SIZE, GVF_BINARY_VAR);
    ReleaseSemaphore(&envLock);
    return len > 0 ? buf : NULL;  /* an empty variable counts as unset */
}

char *setlocale(int category, const char *locale)
{
    (void)category;
    return !locale || !*locale || !strcmp(locale, "C") ? (char *)"C" : NULL;
}

void abort(void)
{
    Alert(0x35000000UL); /* AN_Unknown, recoverable */
    Wait(0);
    for (;;)
        ;
}

void exit(int code)
{
    (void)code;
    abort();
}

/* libstdc++'s ways out, which would print to a program's stderr: they
 * stop the task instead, as abort does, and keep libnix's stdio out. */
void ul_terminate(void) __asm__("__ZN9__gnu_cxx27__verbose_terminate_handlerEv");
void ul_terminate(void) { abort(); }
void ul_throw_system_error(int e) __asm__("__ZSt20__throw_system_errori");
void ul_throw_system_error(int e) { (void)e; abort(); }
void __cxa_pure_virtual(void) { abort(); }

/* Function-local statics with destructors register them here; the
 * library's statics live until it is expunged and need none of them. */
void *__dso_handle;

int __cxa_atexit(void (*fn)(void *), void *arg, void *dso)
{
    (void)fn, (void)arg, (void)dso;
    return 0;
}

int atexit(void (*fn)(void))
{
    (void)fn;
    return 0;
}

/* ICU's built-in data: none. Its data is the file unicode_lib.c loads, so
 * this is an empty common-data block (as GFOpenBrowser's ob_icustub.c), which
 * ICU searches and finds nothing in. */
#include <unicode/udata.h>
#include <unicode/utypes.h>

struct EmptyData {
    uint16_t headerSize;
    uint8_t magic1, magic2;
    UDataInfo info;
    char padding[8];
    uint32_t count, reserved;
    const void *toc[2];
};

#define UL_ENTRY_POINT2(major) icudt##major##_dat
#define UL_ENTRY_POINT(major) UL_ENTRY_POINT2(major)

const struct EmptyData UL_ENTRY_POINT(U_ICU_VERSION_MAJOR_NUM) __attribute__((aligned(8))) = {
    32, 0xda, 0x27,
    {
        sizeof(UDataInfo), 0,
        U_IS_BIG_ENDIAN, U_CHARSET_FAMILY, U_SIZEOF_UCHAR, 0,
        { 0x43, 0x6d, 0x6e, 0x44 }, /* "CmnD" */
        { 1, 0, 0, 0 },
        { 0, 0, 0, 0 },
    },
    { 0 },
    0, 0,
    { 0, 0 },
};

/* ---- Start and end ------------------------------------------------------------ */

/* libstdc++'s constructors: the linker gathers them in sections named
 * .list___CTOR_LIST__ and sorts the .list_ sections by name, so these two
 * markers (each one empty entry) fall just before and just after them. */
typedef void (*ctor_fn)(void);
static const ctor_fn ctorsStart[1] __attribute__((used, section(".list___CTOR_LIST_"))) = { 0 };
static const ctor_fn ctorsEnd[1] __attribute__((used, section(".list___CTOR_LIST__~"))) = { 0 };

static struct Library **const bases[] = {
    &LocaleBase, &MathIeeeDoubBasBase, &MathIeeeDoubTransBase, &MathIeeeSingTransBase, &UtilityBase,
};
static const char *const baseNames[] = {
    "locale.library", "mathieeedoubbas.library", "mathieeedoubtrans.library",
    "mathieeesingtrans.library", "utility.library",
};

int ul_runtime_init(void)
{
    const ctor_fn *c;
    unsigned i;

    InitSemaphore(&poolLock);
    InitSemaphore(&onceLock);
    InitSemaphore(&keyLock);
    InitSemaphore(&envLock);
    initList(&taskValues);
    if (!(pool = CreatePool(MEMF_ANY, 32768, 8192)))
        return 0;
    for (i = 0; i < sizeof bases / sizeof bases[0]; i++)
        if (!(*bases[i] = OpenLibrary((STRPTR)baseNames[i], 37)))
            return 0;
    /* __timezone from Locale's GMT offset, as libnix's startup does. */
    tzset();
    for (c = ctorsStart; c < ctorsEnd; c++)
        if (*c)
            (*c)();
    return 1;
}

void ul_runtime_end(void)
{
    unsigned i;

    for (i = 0; i < sizeof bases / sizeof bases[0]; i++)
        if (*bases[i]) {
            CloseLibrary(*bases[i]);
            *bases[i] = NULL;
        }
    if (pool) {
        DeletePool(pool);
        pool = NULL;
    }
}
