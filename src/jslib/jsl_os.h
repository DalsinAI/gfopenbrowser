/*
 * javascript.library engine: what the core needs from the system, so the
 * core (jsl_core.cpp) builds both for AmigaOS (jsl_os_exec.c) and for the
 * tests on Linux under qemu-m68k (jsl_os_posix.c).
 *
 * A box is a script task's mailbox. Only the script task reads it; any task
 * may send to it, and jsl_os_send waits until the script task has run the
 * call and replied.
 *
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#ifndef JSL_OS_H
#define JSL_OS_H

#ifdef __cplusplus
extern "C" {
#endif

struct jsl_call {
    unsigned char os[40];       /* the system's message, first */
    void (*fn)(void *arg);
    void *arg;
    int async;                  /* sent with jsl_os_post: freed, not replied to */
};

struct jsl_box;

/* Runs call->fn(call->arg) on the engine's own process and waits (AmigaOS:
 * starting and ending script tasks, which allocate and start threads,
 * happens there, never on a caller's task), or at once (Linux). */
void jsl_os_on_engine(struct jsl_call *call);

/* The FPU back to IEEE double, round to nearest (AmigaOS: FPCR 0; a library
 * such as mathieeesingbas may have set single precision, FPCR $40, in this
 * task). Done on each script thread's start, before each call it runs and
 * after each host function returns. Nothing on Linux. */
void jsl_os_fpu_reset(void);

/* Memory any task may take and any task may give back */
void *jsl_os_alloc(unsigned long size);
void jsl_os_free(void *mem);

/* On the engine's process: starts a script thread with a stack of `stack` bytes, makes its box on
 * it, and runs entry(arg, box) there; returns once the box is ready. NULL
 * if the thread could not be started. When entry returns, the box stops
 * taking calls. */
struct jsl_box *jsl_os_spawn(unsigned long stack, const char *name,
                             void (*entry)(void *arg, struct jsl_box *box), void *arg);
/* Waits until the box's thread has ended, and frees the box */
void jsl_os_join(struct jsl_box *box);

/* Is the calling task the box's script task? */
int jsl_os_is_owner(struct jsl_box *box);
/* From another task: runs call on the script task and waits for it */
void jsl_os_send(struct jsl_box *box, struct jsl_call *call);
/* From any task: queues call (allocated by the caller) and returns at once */
void jsl_os_post(struct jsl_box *box, struct jsl_call *call);
/* On the script task: the next call, waiting for one */
struct jsl_call *jsl_os_receive(struct jsl_box *box);
/* On the script task: the call has run */
void jsl_os_reply(struct jsl_box *box, struct jsl_call *call);

#ifdef __cplusplus
}
#endif

#endif
