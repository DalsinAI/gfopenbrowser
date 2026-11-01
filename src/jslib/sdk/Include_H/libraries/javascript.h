#ifndef LIBRARIES_JAVASCRIPT_H
#define LIBRARIES_JAVASCRIPT_H
/*
 * libraries/javascript.h: javascript.library, JavaScript for any program.
 * Copyright (c) 2026 AmiCradle Team. MIT.
 *
 * The library runs JavaScript (JavaScriptCore with the 68k JIT) for the
 * program that opens it. The program owns its objects; the library owns
 * the scripts. A program hands its objects to scripts as host classes,
 * tables of named slots (methods, accessors, fields), and scripts work on
 * them. The program never calls into the engine's insides.
 *
 * A JSTask is a script task: an Exec task with its own stack, its own
 * JavaScript heap and one global object. Everything about its scripts
 * happens on that task, one job at a time. Any task may call the library;
 * a call made from another task is handed to the script task and waited
 * for, so calls are synchronous. A host class's functions are called on
 * the script task, and may call the library from there.
 *
 * A JSValue is a value kept alive for you until you JS_Free it. NULL is
 * undefined. Values handed to a slot function (self, argv) belong to the
 * library for the length of that call: do not free them. A value a slot
 * function returns is given to the library, which frees it.
 *
 * Nothing crosses the interface as floating point: whole numbers are
 * LONGs, other numbers go through a pointer to a DOUBLE, so programs built
 * with soft float can call every vector.
 */

#ifndef EXEC_TYPES_H
#include <exec/types.h>
#endif
#ifndef UTILITY_TAGITEM_H
#include <utility/tagitem.h>
#endif

#define JAVASCRIPTNAME    "javascript.library"
#define JAVASCRIPTVERSION 1

typedef APTR JSTask;
typedef APTR JSValue;
typedef APTR JSClass;

/* ---- JS_NewTask tags ------------------------------------------------------ */

#define JSA_Dummy      (TAG_USER + 0x4A530000)
#define JSA_StackSize  (JSA_Dummy + 1)    /* the script task's stack in bytes (default 524288) */
#define JSA_Name       (JSA_Dummy + 2)    /* the script task's name (default "JavaScript") */
#define JSA_UserData   (JSA_Dummy + 3)    /* any pointer: JS_UserData gives it back */
#define JSA_CacheDir   (JSA_Dummy + 4)    /* STRPTR: a folder where each script's translation is
                                           * kept, made on first run and loaded after (default none) */

/* ---- Errors (LONG results; 0 is success) ----------------------------------- */

#define JSERR_OK         0
#define JSERR_EXCEPTION  1    /* the script threw; JS_ErrorText says what */
#define JSERR_NOMEMORY   2
#define JSERR_NOENGINE   3    /* the engine could not be started */
#define JSERR_BADARGS    4
#define JSERR_TYPE       5    /* the value is not of the kind asked for */

/* ---- JS_TypeOf ---------------------------------------------------------------- */

#define JST_UNDEFINED  0
#define JST_NULL       1
#define JST_BOOLEAN    2
#define JST_NUMBER     3
#define JST_STRING     4
#define JST_OBJECT     5
#define JST_FUNCTION   6
#define JST_SYMBOL     7
#define JST_BIGINT     8

/* ---- Host classes ----------------------------------------------------------------
 *
 * A class is a table, normally generated: its name, its parent class (its
 * slots are seen too), its slots up to one with a NULL name, and what to
 * call when the library drops a wrapper. JS_Wrap gives the one wrapper for
 * a program pointer, the same each time while it lives. The table must
 * stay in memory while the task lives.
 */

/* A method: argv holds argc values; return a new value or NULL. To throw,
 * return JS_Throw(task, "message"). */
typedef JSValue (*JSMethodFunc)(JSTask task, APTR self, LONG argc, JSValue *argv, APTR data);
/* An accessor's getter and setter (setter NULL: read-only) */
typedef JSValue (*JSGetFunc)(JSTask task, APTR self, APTR data);
typedef LONG (*JSSetFunc)(JSTask task, APTR self, JSValue value, APTR data);
/* Called when the library has dropped the wrapper of self */
typedef VOID (*JSReleaseFunc)(JSTask task, APTR self, APTR data);
/* A job run on the script task (JS_Post) */
typedef LONG (*JSJobFunc)(JSTask task, APTR data);

#define JSS_METHOD     1    /* js_Func: JSMethodFunc */
#define JSS_ACCESSOR   2    /* js_Func: JSGetFunc, js_Func2: JSSetFunc or NULL */
#define JSS_FIELD      3    /* js_Offset, js_Type: a value inside the program's object, read-only */

/* JSS_FIELD types */
#define JSF_LONG       1
#define JSF_ULONG      2
#define JSF_WORD       3
#define JSF_UWORD      4
#define JSF_BYTE       5
#define JSF_UBYTE      6
#define JSF_BOOL       7    /* a UBYTE: 0 is false */
#define JSF_STRPTR     8    /* a UTF-8 string pointer, NULL reads as null */

struct JSHostSlot {
    CONST_STRPTR js_Name;
    UWORD js_Kind;          /* JSS_ */
    UWORD js_Type;          /* JSF_ for a field */
    APTR js_Func;
    APTR js_Func2;
    LONG js_Offset;         /* a field's offset in the program's object */
    LONG js_Args;           /* a method's length (as Function.length) */
};

struct JSHostClass {
    CONST_STRPTR jc_Name;
    CONST struct JSHostClass *jc_Parent;
    CONST struct JSHostSlot *jc_Slots;  /* ends with a NULL js_Name */
    JSReleaseFunc jc_Release;           /* or NULL */
    APTR jc_Data;                       /* handed to every function of the class */
};

/* ---- The engine's port (private to the library and the engine) ------------ */

#define JAVASCRIPT_ENGINE_PORT "JAVASCRIPT.ENGINE"

#endif /* LIBRARIES_JAVASCRIPT_H */
