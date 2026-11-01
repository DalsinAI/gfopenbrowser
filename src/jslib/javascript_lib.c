/* javascript.library: JavaScript for any program.
 * Copyright (c) 2026 AmigaChrome Team. MIT.
 *
 * The library holds no engine. JavaScriptEngine (jsl_engine.c with
 * JavaScriptCore and the 68k JIT) is a program this library starts the
 * first time a program asks for a script task; it opens the public port
 * JAVASCRIPT.ENGINE carrying its table (jsl_engine_amiga.h). JS_NewTask
 * goes through the port; every other call takes a JSTask, which starts with
 * the table, and calls the engine directly on the caller's task. The
 * engine hands the work to the script task and waits, or does it at once
 * when the caller is the script task itself (a host class's function).
 *
 * The RomTag, the vector table in sfd/javascript_lib.sfd's order
 * (check_vectors.py), each call taking its arguments in its registers.
 * SysBase is set once at init; DOSBase once, the first time the engine is
 * started. Any task may call any vector at once.
 */
#include <exec/types.h>
#include <dos/dos.h>
#include <dos/dostags.h>
#include <exec/resident.h>
#include <exec/libraries.h>
#include <exec/execbase.h>
#include <exec/memory.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <libraries/javascript.h>

#include "jsl_engine_amiga.h"

#ifndef REG
#define REG(r, decl) register decl __asm(#r)
#endif

#define LIB_VERSION  1
#define LIB_REVISION 0

#define ENGINE_PATH "LIBS:JavaScript/JavaScriptEngine"
/* Not installed: a program's drawer, as unpacked, may carry it in Libs */
#define ENGINE_PATH_DRAWER "PROGDIR:Libs/JavaScript/JavaScriptEngine"
#define ENGINE_WAIT 120     /* seconds for a cold start on a slow disk */

struct JSLBase {
    struct Library lib;
    BPTR seglist;
};

struct ExecBase *SysBase;
struct DosLibrary *DOSBase;

/* Run as a program, the library does nothing. This must stay the first code
 * in the file (-fno-toplevel-reorder). */
int start(void) { return -1; }

static const char lib_name[] = JAVASCRIPTNAME;
static const char lib_id[] = "javascript.library 1.0 (22.10.2026) AmiCradle Team\r\n";
static const char lib_ver[] __attribute__((used)) =
    "\0$VER: javascript.library 1.0 (22.10.2026) AmiCradle Team";

/* ---- Library housekeeping -------------------------------------------------- */

static struct Library *lib_init(REG(d0, struct JSLBase *base), REG(a0, BPTR seglist),
                                REG(a6, struct ExecBase *sys))
{
    SysBase = sys;
    base->seglist = seglist;
    base->lib.lib_Node.ln_Type = NT_LIBRARY;
    base->lib.lib_Node.ln_Name = (char *)lib_name;
    base->lib.lib_Flags = LIBF_SUMUSED | LIBF_CHANGED;
    base->lib.lib_Version = LIB_VERSION;
    base->lib.lib_Revision = LIB_REVISION;
    base->lib.lib_IdString = (APTR)lib_id;
    return &base->lib;
}

static struct Library *lib_open(REG(a6, struct JSLBase *base))
{
    base->lib.lib_OpenCnt++;
    base->lib.lib_Flags &= ~LIBF_DELEXP;
    return &base->lib;
}

static BPTR lib_expunge(REG(a6, struct JSLBase *base));

static BPTR lib_close(REG(a6, struct JSLBase *base))
{
    if (base->lib.lib_OpenCnt)
        base->lib.lib_OpenCnt--;
    if (base->lib.lib_OpenCnt == 0 && (base->lib.lib_Flags & LIBF_DELEXP))
        return lib_expunge(base);
    return 0;
}

static BPTR lib_expunge(REG(a6, struct JSLBase *base))
{
    BPTR seglist;
    if (base->lib.lib_OpenCnt) {
        base->lib.lib_Flags |= LIBF_DELEXP;
        return 0;
    }
    seglist = base->seglist;
    Remove(&base->lib.lib_Node);
    if (DOSBase)
        CloseLibrary((struct Library *)DOSBase);
    FreeMem((UBYTE *)base - base->lib.lib_NegSize,
            base->lib.lib_NegSize + base->lib.lib_PosSize);
    return seglist;
}

static ULONG lib_null(void)
{
    return 0;
}

/* ---- Starting the engine ----------------------------------------------------- */

/* The engine's port with this caller counted in its users, or NULL. The
 * engine never quits while ep_Users is not 0 (jsl_engine.c). */
static struct JSEnginePort *useEngine(void)
{
    struct JSEnginePort *port;
    Forbid();
    port = (struct JSEnginePort *)FindPort((CONST_STRPTR)JAVASCRIPT_ENGINE_PORT);
    if (port && port->ep_Version == JSL_TABLE_VERSION)
        port->ep_Users++;
    else
        port = NULL;
    Permit();
    return port;
}

static void doneWithEngine(struct JSEnginePort *port)
{
    Forbid();
    port->ep_Users--;
    Permit();
}

static int startEngine(void)
{
    char path[300];
    BPTR lock;
    LONG i;
    struct TagItem tags[] = {
        { SYS_Asynch, TRUE },
        { SYS_Input, 0 },
        { SYS_Output, 0 },
        { NP_Name, (ULONG)"JavaScriptEngine" },
        { TAG_DONE, 0 }
    };
    if (!DOSBase && !(DOSBase = (struct DosLibrary *)OpenLibrary((CONST_STRPTR)"dos.library", 39)))
        return 0;
    if (!(lock = Lock((CONST_STRPTR)ENGINE_PATH, ACCESS_READ)) &&
        !(lock = Lock((CONST_STRPTR)ENGINE_PATH_DRAWER, ACCESS_READ)))
        return 0;
    /* Its full name, quoted: the shell that runs it has no PROGDIR: of ours */
    if (!NameFromLock(lock, (STRPTR)path + 1, sizeof path - 2)) {
        UnLock(lock);
        return 0;
    }
    UnLock(lock);
    path[0] = '"';
    for (i = 1; path[i]; i++)
        ;
    path[i] = '"';
    path[i + 1] = 0;
    tags[1].ti_Data = (ULONG)Open((CONST_STRPTR)"NIL:", MODE_OLDFILE);
    tags[2].ti_Data = (ULONG)Open((CONST_STRPTR)"NIL:", MODE_NEWFILE);
    if (SystemTagList((CONST_STRPTR)path, tags) == -1) {
        Close((BPTR)tags[1].ti_Data);
        Close((BPTR)tags[2].ti_Data);
        return 0;
    }
    for (i = 0; i < ENGINE_WAIT * 10; i++) {
        Delay(TICKS_PER_SECOND / 10);
        Forbid();
        lock = (BPTR)FindPort((CONST_STRPTR)JAVASCRIPT_ENGINE_PORT);
        Permit();
        if (lock)
            return 1;
    }
    return 0;
}

/* ---- The calls, in their registers (sfd/javascript_lib.sfd) ------------------ */

#define T(task) (((struct JSTaskHead *)(task))->th_Table)

static JSTask L_JS_NewTask(REG(a0, struct TagItem *tags))
{
    struct JSEnginePort *port = useEngine();
    JSTask task;
    if (!port && (!startEngine() || !(port = useEngine())))
        return NULL;
    task = port->ep_Table->NewTask(tags);
    doneWithEngine(port);
    return task;
}

static VOID L_JS_EndTask(REG(a0, JSTask task))
{ if (task) T(task)->EndTask(task); }
static LONG L_JS_Evaluate(REG(a0, JSTask task), REG(a1, CONST_STRPTR source), REG(d0, LONG length),
                          REG(a2, CONST_STRPTR name), REG(a3, JSValue *result))
{ return task ? T(task)->Evaluate(task, source, length, name, result) : JSERR_BADARGS; }
static LONG L_JS_Post(REG(a0, JSTask task), REG(a1, JSJobFunc func), REG(a2, APTR data), REG(d0, ULONG wait))
{ return task ? T(task)->Post(task, func, data, wait) : JSERR_BADARGS; }
static APTR L_JS_UserData(REG(a0, JSTask task))
{ return task ? T(task)->UserData(task) : NULL; }
static JSValue L_JS_Global(REG(a0, JSTask task))
{ return task ? T(task)->Global(task) : NULL; }
static VOID L_JS_Free(REG(a0, JSTask task), REG(a1, JSValue value))
{ if (task) T(task)->Free(task, value); }
static JSValue L_JS_Dup(REG(a0, JSTask task), REG(a1, JSValue value))
{ return task ? T(task)->Dup(task, value) : NULL; }
static LONG L_JS_TypeOf(REG(a0, JSTask task), REG(a1, JSValue value))
{ return task ? T(task)->TypeOf(task, value) : JST_UNDEFINED; }
static JSValue L_JS_NewLong(REG(a0, JSTask task), REG(d0, LONG value))
{ return task ? T(task)->NewLong(task, value) : NULL; }
static JSValue L_JS_NewDouble(REG(a0, JSTask task), REG(a1, CONST DOUBLE *value))
{ return task ? T(task)->NewDouble(task, value) : NULL; }
static JSValue L_JS_NewBool(REG(a0, JSTask task), REG(d0, LONG value))
{ return task ? T(task)->NewBool(task, value) : NULL; }
static JSValue L_JS_NewNull(REG(a0, JSTask task))
{ return task ? T(task)->NewNull(task) : NULL; }
static JSValue L_JS_NewString(REG(a0, JSTask task), REG(a1, CONST_STRPTR utf8), REG(d0, LONG length))
{ return task ? T(task)->NewString(task, utf8, length) : NULL; }
static JSValue L_JS_NewObject(REG(a0, JSTask task))
{ return task ? T(task)->NewObject(task) : NULL; }
static JSValue L_JS_NewFunction(REG(a0, JSTask task), REG(a1, CONST_STRPTR name), REG(a2, JSMethodFunc func),
                                REG(a3, APTR data))
{ return task ? T(task)->NewFunction(task, name, func, data) : NULL; }
static LONG L_JS_ToLong(REG(a0, JSTask task), REG(a1, JSValue value), REG(a2, LONG *result))
{ return task && result ? T(task)->ToLong(task, value, result) : JSERR_BADARGS; }
static LONG L_JS_ToDouble(REG(a0, JSTask task), REG(a1, JSValue value), REG(a2, DOUBLE *result))
{ return task && result ? T(task)->ToDouble(task, value, result) : JSERR_BADARGS; }
static LONG L_JS_ToBool(REG(a0, JSTask task), REG(a1, JSValue value))
{ return task ? T(task)->ToBool(task, value) : 0; }
static LONG L_JS_ToString(REG(a0, JSTask task), REG(a1, JSValue value), REG(a2, STRPTR buffer), REG(d0, LONG size))
{ return task ? T(task)->ToString(task, value, buffer, size) : -1; }
static JSValue L_JS_GetProp(REG(a0, JSTask task), REG(a1, JSValue object), REG(a2, CONST_STRPTR name))
{ return task ? T(task)->GetProp(task, object, name) : NULL; }
static LONG L_JS_SetProp(REG(a0, JSTask task), REG(a1, JSValue object), REG(a2, CONST_STRPTR name),
                         REG(a3, JSValue value))
{ return task ? T(task)->SetProp(task, object, name, value) : JSERR_BADARGS; }
static JSValue L_JS_GetIndex(REG(a0, JSTask task), REG(a1, JSValue object), REG(d0, ULONG index))
{ return task ? T(task)->GetIndex(task, object, index) : NULL; }
static LONG L_JS_SetIndex(REG(a0, JSTask task), REG(a1, JSValue object), REG(d0, ULONG index), REG(a2, JSValue value))
{ return task ? T(task)->SetIndex(task, object, index, value) : JSERR_BADARGS; }
static LONG L_JS_Call(REG(a0, JSTask task), REG(a1, JSValue func), REG(a2, JSValue self), REG(d0, LONG argc),
                      REG(a3, JSValue *argv), REG(d1, JSValue *result))
{ return task ? T(task)->Call(task, func, self, argc, argv, result) : JSERR_BADARGS; }
static JSClass L_JS_RegisterClass(REG(a0, JSTask task), REG(a1, CONST struct JSHostClass *hostClass))
{ return task ? T(task)->RegisterClass(task, hostClass) : NULL; }
static JSValue L_JS_Wrap(REG(a0, JSTask task), REG(a1, JSClass cls), REG(a2, APTR object))
{ return task ? T(task)->Wrap(task, cls, object) : NULL; }
static APTR L_JS_Unwrap(REG(a0, JSTask task), REG(a1, JSValue value), REG(a2, JSClass cls))
{ return task ? T(task)->Unwrap(task, value, cls) : NULL; }
static JSValue L_JS_Throw(REG(a0, JSTask task), REG(a1, CONST_STRPTR message))
{ return task ? T(task)->Throw(task, message) : NULL; }
static LONG L_JS_ErrorText(REG(a0, JSTask task), REG(a1, STRPTR buffer), REG(d0, LONG size))
{ return task ? T(task)->ErrorText(task, buffer, size) : 0; }

/* sfd/javascript_lib.sfd's order, line for line (check_vectors.py) */
static const APTR lib_vectors[] = {
    (APTR)lib_open, (APTR)lib_close, (APTR)lib_expunge, (APTR)lib_null,
    (APTR)L_JS_NewTask,             /*  -30 */
    (APTR)L_JS_EndTask,             /*  -36 */
    (APTR)L_JS_Evaluate,            /*  -42 */
    (APTR)L_JS_Post,                /*  -48 */
    (APTR)L_JS_UserData,            /*  -54 */
    (APTR)L_JS_Global,              /*  -60 */
    (APTR)L_JS_Free,                /*  -66 */
    (APTR)L_JS_Dup,                 /*  -72 */
    (APTR)L_JS_TypeOf,              /*  -78 */
    (APTR)L_JS_NewLong,             /*  -84 */
    (APTR)L_JS_NewDouble,           /*  -90 */
    (APTR)L_JS_NewBool,             /*  -96 */
    (APTR)L_JS_NewNull,             /* -102 */
    (APTR)L_JS_NewString,           /* -108 */
    (APTR)L_JS_NewObject,           /* -114 */
    (APTR)L_JS_NewFunction,         /* -120 */
    (APTR)L_JS_ToLong,              /* -126 */
    (APTR)L_JS_ToDouble,            /* -132 */
    (APTR)L_JS_ToBool,              /* -138 */
    (APTR)L_JS_ToString,            /* -144 */
    (APTR)L_JS_GetProp,             /* -150 */
    (APTR)L_JS_SetProp,             /* -156 */
    (APTR)L_JS_GetIndex,            /* -162 */
    (APTR)L_JS_SetIndex,            /* -168 */
    (APTR)L_JS_Call,                /* -174 */
    (APTR)L_JS_RegisterClass,       /* -180 */
    (APTR)L_JS_Wrap,                /* -186 */
    (APTR)L_JS_Unwrap,              /* -192 */
    (APTR)L_JS_Throw,               /* -198 */
    (APTR)L_JS_ErrorText,           /* -204 */
    (APTR)-1
};

static const struct {
    ULONG size;
    const APTR *vectors;
    APTR data;
    APTR init;
} lib_inittable = {
    sizeof(struct JSLBase), lib_vectors, NULL, (APTR)lib_init
};

const struct Resident lib_romtag = {
    RTC_MATCHWORD, (struct Resident *)&lib_romtag, (APTR)(&lib_romtag + 1),
    RTF_AUTOINIT, LIB_VERSION, NT_LIBRARY, 0,
    (char *)lib_name, (char *)lib_id, (APTR)&lib_inittable
};
