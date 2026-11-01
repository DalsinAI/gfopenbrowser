/*
 * javascript.library: what the library and the engine share. Private to
 * the two (programs use libraries/javascript.h and the library's vectors).
 *
 * The library is small and has no engine in it. The engine (JavaScript
 * Engine, JavaScriptCore with the 68k JIT) is a program the library starts
 * the first time a program needs it; it opens the public port
 * JAVASCRIPT_ENGINE_PORT, a struct JSEnginePort that carries its table.
 * The library calls the table's functions directly, on the caller's task:
 * each one hands the work to the script task and waits for it, or does it
 * at once when the caller is the script task.
 *
 * Every JSTask starts with a pointer to the table, so a call that takes a
 * task needs nothing else from the library.
 *
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#ifndef JSL_ENGINE_H
#define JSL_ENGINE_H

#include <libraries/javascript.h>

#ifdef __cplusplus
extern "C" {
#endif

#define JSL_TABLE_VERSION 1

struct JSEngineTable {
    ULONG jt_Version;           /* JSL_TABLE_VERSION */
    ULONG jt_Size;              /* sizeof(struct JSEngineTable) */
    JSTask (*NewTask)(struct TagItem *tags);
    VOID (*EndTask)(JSTask task);
    LONG (*Evaluate)(JSTask task, CONST_STRPTR source, LONG length, CONST_STRPTR name, JSValue *result);
    LONG (*Post)(JSTask task, JSJobFunc func, APTR data, ULONG wait);
    APTR (*UserData)(JSTask task);
    JSValue (*Global)(JSTask task);
    VOID (*Free)(JSTask task, JSValue value);
    JSValue (*Dup)(JSTask task, JSValue value);
    LONG (*TypeOf)(JSTask task, JSValue value);
    JSValue (*NewLong)(JSTask task, LONG value);
    JSValue (*NewDouble)(JSTask task, CONST DOUBLE *value);
    JSValue (*NewBool)(JSTask task, LONG value);
    JSValue (*NewNull)(JSTask task);
    JSValue (*NewString)(JSTask task, CONST_STRPTR utf8, LONG length);
    JSValue (*NewObject)(JSTask task);
    JSValue (*NewFunction)(JSTask task, CONST_STRPTR name, JSMethodFunc func, APTR data);
    LONG (*ToLong)(JSTask task, JSValue value, LONG *result);
    LONG (*ToDouble)(JSTask task, JSValue value, DOUBLE *result);
    LONG (*ToBool)(JSTask task, JSValue value);
    LONG (*ToString)(JSTask task, JSValue value, STRPTR buffer, LONG size);
    JSValue (*GetProp)(JSTask task, JSValue object, CONST_STRPTR name);
    LONG (*SetProp)(JSTask task, JSValue object, CONST_STRPTR name, JSValue value);
    JSValue (*GetIndex)(JSTask task, JSValue object, ULONG index);
    LONG (*SetIndex)(JSTask task, JSValue object, ULONG index, JSValue value);
    LONG (*Call)(JSTask task, JSValue func, JSValue self, LONG argc, JSValue *argv, JSValue *result);
    JSClass (*RegisterClass)(JSTask task, CONST struct JSHostClass *hostClass);
    JSValue (*Wrap)(JSTask task, JSClass cls, APTR object);
    APTR (*Unwrap)(JSTask task, JSValue value, JSClass cls);
    JSValue (*Throw)(JSTask task, CONST_STRPTR message);
    LONG (*ErrorText)(JSTask task, STRPTR buffer, LONG size);
};

/* The head of every JSTask */
struct JSTaskHead {
    CONST struct JSEngineTable *th_Table;
};

/* The engine's own table, for the engine program and for tests */
CONST struct JSEngineTable *jsl_engine_table(void);
/* Script tasks started and not yet ended */
LONG jsl_engine_live_tasks(void);

#ifdef __cplusplus
}
#endif

#endif
