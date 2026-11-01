/*
 * host tests: exec's task signals and Forbid()/Permit() on POSIX threads
 * (see proto/exec.h), for the stove's allocator's lock.
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include <proto/exec.h>

#include <pthread.h>

struct Task {
    pthread_cond_t cond;
    ULONG recvd;
    ULONG alloc;
    int ready;
};

int exec_host_no_signals;
long exec_host_waits;

static __thread struct Task self;
static pthread_mutex_t forbid = PTHREAD_MUTEX_INITIALIZER;
static struct Task *owner;          /* the task in Forbid() */
static int nest;

struct Task *FindTask(CONST_STRPTR name)
{
    (void)name;
    if (!self.ready) {
        pthread_cond_init(&self.cond, NULL);
        self.ready = 1;
    }
    return &self;
}

void Forbid(void)
{
    struct Task *me = FindTask(NULL);
    if (__atomic_load_n(&owner, __ATOMIC_SEQ_CST) == me) {
        nest++;
        return;
    }
    pthread_mutex_lock(&forbid);
    __atomic_store_n(&owner, me, __ATOMIC_SEQ_CST);
    nest = 1;
}

void Permit(void)
{
    if (--nest)
        return;
    __atomic_store_n(&owner, NULL, __ATOMIC_SEQ_CST);
    pthread_mutex_unlock(&forbid);
}

BYTE AllocSignal(BYTE signalNum)
{
    struct Task *me = FindTask(NULL);
    int b;
    (void)signalNum;
    if (exec_host_no_signals)
        return -1;
    for (b = 16; b < 32; b++)
        if (!(me->alloc & (1UL << b))) {
            me->alloc |= 1UL << b;
            return (BYTE)b;
        }
    return -1;
}

void FreeSignal(BYTE signalNum)
{
    if (signalNum >= 0)
        FindTask(NULL)->alloc &= ~(1UL << signalNum);
}

ULONG SetSignal(ULONG newSignals, ULONG signalSet)
{
    struct Task *me = FindTask(NULL);
    ULONG old;
    Forbid();
    old = me->recvd;
    me->recvd = (old & ~signalSet) | (newSignals & signalSet);
    Permit();
    return old;
}

/* as exec's: a Forbid() is let go of while the task sleeps */
ULONG Wait(ULONG signalSet)
{
    struct Task *me = FindTask(NULL);
    ULONG got;
    int saved;
    Forbid();
    saved = nest;
    __atomic_add_fetch(&exec_host_waits, 1, __ATOMIC_RELAXED);
    while (!(me->recvd & signalSet)) {
        __atomic_store_n(&owner, NULL, __ATOMIC_SEQ_CST);
        pthread_cond_wait(&me->cond, &forbid);
        __atomic_store_n(&owner, me, __ATOMIC_SEQ_CST);
    }
    nest = saved;
    got = me->recvd & signalSet;
    me->recvd &= ~got;
    Permit();
    return got;
}

void Signal(struct Task *task, ULONG signalSet)
{
    Forbid();
    task->recvd |= signalSet;
    pthread_cond_signal(&task->cond);
    Permit();
}
