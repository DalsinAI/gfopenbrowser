/*
 * javascript.library engine: the system layer on AmigaOS. A script thread
 * is a pthread (WebKit's threads are, so JavaScriptCore knows it), and its
 * box is an Exec message port it owns. Callers may be any Exec task, not
 * only pthreads: a call is an Exec message to the box, answered on a reply
 * port the caller makes for it. Script tasks are started and ended on the
 * engine's own process (jsl_os_on_engine, jsl_engine.c), not on the calling
 * task, so threads and the C library's memory are only ever handled by the
 * engine's own processes.
 *
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include <pthread.h>
#include <string.h>
#include <exec/memory.h>
#include <exec/ports.h>
#include <proto/exec.h>

#include "jsl_os.h"
#include "jsl_engine_amiga.h"

struct jsl_box {
    struct MsgPort *port;
    struct Task *task;
    pthread_t thread;
    unsigned long stack;
    void (*entry)(void *, struct jsl_box *);
    void *arg;
    struct Task *creator;
    BYTE readySignal;
    BYTE started;
};

typedef char check_msg[sizeof(struct Message) <= sizeof(((struct jsl_call *)0)->os) ? 1 : -1];

void jsl_os_fpu_reset(void)
{
    unsigned int fpcr = 0;
    __asm__ __volatile__("fmove%.l %0,%!" : : "dm"(fpcr));
}

static void *trampoline(void *p)
{
    struct jsl_box *box = p;
    jsl_os_fpu_reset();
    box->task = FindTask(NULL);
    box->port = CreateMsgPort();
    Signal(box->creator, 1UL << box->readySignal);
    if (!box->port)
        return NULL;
    box->entry(box->arg, box);
    DeleteMsgPort(box->port);   /* by its owner: the signal bit is this task's */
    box->port = NULL;
    return NULL;
}

/* On the engine's process: starts the thread and waits for its port */
static void jsl_os_spawn_here(struct jsl_box *box)
{
    pthread_attr_t attr;
    box->creator = FindTask(NULL);
    box->readySignal = AllocSignal(-1);
    if (box->readySignal < 0)
        return;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, box->stack);
    if (pthread_create(&box->thread, &attr, trampoline, box) == 0) {
        Wait(1UL << box->readySignal);
        box->started = box->port != NULL;
    }
    pthread_attr_destroy(&attr);
    FreeSignal(box->readySignal);
}

void *jsl_os_alloc(unsigned long size)
{
    return AllocVec(size, MEMF_PUBLIC | MEMF_CLEAR);
}

void jsl_os_free(void *mem)
{
    FreeVec(mem);
}

struct jsl_box *jsl_os_spawn(unsigned long stack, const char *name,
                             void (*entry)(void *, struct jsl_box *), void *arg)
{
    struct jsl_box *box = AllocVec(sizeof *box, MEMF_ANY | MEMF_CLEAR);
    (void)name;
    if (!box)
        return NULL;
    box->stack = stack;
    box->entry = entry;
    box->arg = arg;
    jsl_os_spawn_here(box);
    if (!box->started) {
        FreeVec(box);
        return NULL;
    }
    return box;
}

void jsl_os_join(struct jsl_box *box)
{
    pthread_join(box->thread, NULL);
    FreeVec(box);
}

int jsl_os_is_owner(struct jsl_box *box)
{
    return FindTask(NULL) == box->task;
}

void jsl_os_send(struct jsl_box *box, struct jsl_call *call)
{
    struct Message *m = (struct Message *)call->os;
    struct MsgPort *reply = CreateMsgPort();
    if (!reply) {
        call->fn = NULL;    /* nothing ran: the core's results stay as set */
        return;
    }
    memset(m, 0, sizeof *m);
    m->mn_Node.ln_Type = NT_MESSAGE;
    m->mn_ReplyPort = reply;
    m->mn_Length = sizeof *call;
    PutMsg(box->port, m);
    WaitPort(reply);
    GetMsg(reply);
    DeleteMsgPort(reply);
}

void jsl_os_post(struct jsl_box *box, struct jsl_call *call)
{
    struct Message *m = (struct Message *)call->os;
    memset(m, 0, sizeof *m);
    m->mn_Node.ln_Type = NT_MESSAGE;
    m->mn_Length = sizeof *call;
    PutMsg(box->port, m);
}

struct jsl_call *jsl_os_receive(struct jsl_box *box)
{
    struct Message *m;
    while (!(m = GetMsg(box->port)))
        WaitPort(box->port);
    jsl_os_fpu_reset();
    return (struct jsl_call *)m;
}

void jsl_os_reply(struct jsl_box *box, struct jsl_call *call)
{
    (void)box;
    ReplyMsg((struct Message *)call->os);
}
