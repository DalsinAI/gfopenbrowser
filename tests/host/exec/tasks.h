/*
 * host tests: the Exec task signal the stove's allocator falls back on
 * (stove/compat/os32compat_malloc.c); struct Task is tests/host/exec_tasks.c's.
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#ifndef EXEC_TASKS_H
#define EXEC_TASKS_H
#include <exec/types.h>
struct Task;
#define SIGB_SINGLE 4
#define SIGF_SINGLE (1L << 4)
#endif
