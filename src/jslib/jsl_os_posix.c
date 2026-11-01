/*
 * javascript.library engine: the system layer for the tests on Linux
 * (tests/jslib/run.sh, under qemu-m68k). Script threads are pthreads; a box
 * is a queue under a mutex.
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#include "jsl_os.h"

struct posix_msg {
    struct jsl_call *next;
    int done;
};

struct jsl_box {
    pthread_t thread;
    pthread_mutex_t lock;
    pthread_cond_t wake;        /* the script thread: a call came */
    pthread_cond_t done;        /* the senders: a call was run */
    struct jsl_call *head, *tail;
    void (*entry)(void *, struct jsl_box *);
    void *arg;
};

typedef char check_msg[sizeof(struct posix_msg) <= sizeof(((struct jsl_call *)0)->os) ? 1 : -1];

void jsl_os_on_engine(struct jsl_call *call)
{
    call->fn(call->arg);
}

void jsl_os_fpu_reset(void)
{
}

void *jsl_os_alloc(unsigned long size)
{
    return calloc(1, size);
}

void jsl_os_free(void *mem)
{
    free(mem);
}

static void *trampoline(void *p)
{
    struct jsl_box *box = p;
    box->entry(box->arg, box);
    return NULL;
}

struct jsl_box *jsl_os_spawn(unsigned long stack, const char *name,
                             void (*entry)(void *, struct jsl_box *), void *arg)
{
    struct jsl_box *box = calloc(1, sizeof *box);
    pthread_attr_t attr;
    (void)name;
    if (!box)
        return NULL;
    pthread_mutex_init(&box->lock, NULL);
    pthread_cond_init(&box->wake, NULL);
    pthread_cond_init(&box->done, NULL);
    box->entry = entry;
    box->arg = arg;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, stack);
    if (pthread_create(&box->thread, &attr, trampoline, box)) {
        pthread_attr_destroy(&attr);
        free(box);
        return NULL;
    }
    pthread_attr_destroy(&attr);
    return box;
}

void jsl_os_join(struct jsl_box *box)
{
    pthread_join(box->thread, NULL);
    pthread_cond_destroy(&box->done);
    pthread_cond_destroy(&box->wake);
    pthread_mutex_destroy(&box->lock);
    free(box);
}

int jsl_os_is_owner(struct jsl_box *box)
{
    return pthread_equal(pthread_self(), box->thread);
}

static void enqueue(struct jsl_box *box, struct jsl_call *call)
{
    struct posix_msg *m = (struct posix_msg *)call->os;
    m->next = NULL;
    m->done = 0;
    if (box->tail)
        ((struct posix_msg *)box->tail->os)->next = call;
    else
        box->head = call;
    box->tail = call;
    pthread_cond_signal(&box->wake);
}

void jsl_os_send(struct jsl_box *box, struct jsl_call *call)
{
    pthread_mutex_lock(&box->lock);
    enqueue(box, call);
    while (!((struct posix_msg *)call->os)->done)
        pthread_cond_wait(&box->done, &box->lock);
    pthread_mutex_unlock(&box->lock);
}

void jsl_os_post(struct jsl_box *box, struct jsl_call *call)
{
    pthread_mutex_lock(&box->lock);
    enqueue(box, call);
    pthread_mutex_unlock(&box->lock);
}

struct jsl_call *jsl_os_receive(struct jsl_box *box)
{
    struct jsl_call *call;
    pthread_mutex_lock(&box->lock);
    while (!box->head)
        pthread_cond_wait(&box->wake, &box->lock);
    call = box->head;
    box->head = ((struct posix_msg *)call->os)->next;
    if (!box->head)
        box->tail = NULL;
    pthread_mutex_unlock(&box->lock);
    return call;
}

void jsl_os_reply(struct jsl_box *box, struct jsl_call *call)
{
    pthread_mutex_lock(&box->lock);
    ((struct posix_msg *)call->os)->done = 1;
    pthread_cond_broadcast(&box->done);
    pthread_mutex_unlock(&box->lock);
}
