/*
 * host tests: the exec.library calls the stove's allocator makes, on POSIX
 * threads (tests/host/exec_tasks.c): each thread is a task, Forbid() one
 * lock that Wait() lets go of while the task sleeps.
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#ifndef PROTO_EXEC_H
#define PROTO_EXEC_H
#include <exec/tasks.h>
struct Task *FindTask(CONST_STRPTR name);
void Forbid(void);
void Permit(void);
BYTE AllocSignal(BYTE signalNum);
void FreeSignal(BYTE signalNum);
ULONG SetSignal(ULONG newSignals, ULONG signalSet);
ULONG Wait(ULONG signalSet);
void Signal(struct Task *task, ULONG signalSet);
extern int exec_host_no_signals;   /* nonzero: AllocSignal() fails */
extern long exec_host_waits;       /* Wait() calls, all tasks */
#endif
