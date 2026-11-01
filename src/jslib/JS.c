/*
 * JS: run JavaScript from the Shell, through javascript.library.
 *
 *   JS FILE/A [ARGS/M]            runs a script file
 *   JS EVAL "print(6 * 7)"        runs the text given
 *   STACK/K/N                     the script task's stack in KB (default 512)
 *   CACHE/K                       where a FILE's translation is kept (default
 *                                 LIBS:JavaScript/Cache): made on its first
 *                                 run, loaded on the next
 *   NOCACHE/S                     keeps none
 *
 * The script sees `arguments` (the ARGS, as strings) and three functions:
 *   print(...)              writes its arguments, spaces between, then a newline
 *   readFile(name)          a file's bytes as a string (UTF-8), or null
 *   writeFile(name, text)   writes text (UTF-8); true when it did
 *
 * Return code 0, or 10 when the script threw (its error is printed), 20 when
 * the library or the engine could not be had.
 *
 * The functions run on the script task, not this one: they use only DOS
 * calls on the handles this program gives them, never the C library.
 *
 * MIT, Copyright (c) 2026 AmigaChrome Team.
 */
#include <exec/types.h>
#include <exec/memory.h>
#include <dos/dos.h>
#include <dos/rdargs.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/javascript.h>

static const char version[] __attribute__((used)) = "$VER: JS 1.0 (22.10.2026) AmiCradle Team";

struct Library *JavaScriptBase;

struct Context {
    BPTR out;
};

static void put(BPTR out, const char *s)
{
    LONG n = 0;
    while (s[n])
        n++;
    Write(out, (APTR)s, n);
}

/* A value as text in memory of its own (FreeVec it), or NULL */
static STRPTR text(JSTask t, JSValue v, LONG *length)
{
    LONG n = JS_ToString(t, v, NULL, 0);
    STRPTR s;
    if (n < 0 || !(s = AllocVec(n + 1, MEMF_ANY)))
        return NULL;
    JS_ToString(t, v, s, n + 1);
    if (length)
        *length = n;
    return s;
}

static JSValue fnPrint(JSTask t, APTR self, LONG argc, JSValue *argv, APTR data)
{
    struct Context *c = data;
    LONG i, n;
    (void)self;
    for (i = 0; i < argc; i++) {
        STRPTR s = text(t, argv[i], &n);
        if (i)
            Write(c->out, (APTR)" ", 1);
        if (s) {
            Write(c->out, s, n);
            FreeVec(s);
        }
    }
    Write(c->out, (APTR)"\n", 1);
    return NULL;
}

static JSValue fnReadFile(JSTask t, APTR self, LONG argc, JSValue *argv, APTR data)
{
    STRPTR name, bytes;
    BPTR fh;
    LONG size;
    JSValue r = NULL;
    (void)self; (void)data;
    if (argc < 1 || !(name = text(t, argv[0], NULL)))
        return JS_Throw(t, (CONST_STRPTR)"readFile needs a file name");
    fh = Open(name, MODE_OLDFILE);
    FreeVec(name);
    if (!fh)
        return JS_NewNull(t);
    Seek(fh, 0, OFFSET_END);
    size = Seek(fh, 0, OFFSET_BEGINNING);
    if (size >= 0 && (bytes = AllocVec(size + 1, MEMF_ANY))) {
        if (Read(fh, bytes, size) == size)
            r = JS_NewString(t, bytes, size);
        FreeVec(bytes);
    }
    Close(fh);
    return r ? r : JS_NewNull(t);
}

static JSValue fnWriteFile(JSTask t, APTR self, LONG argc, JSValue *argv, APTR data)
{
    STRPTR name, body;
    LONG n = 0, ok = 0;
    BPTR fh;
    (void)self; (void)data;
    if (argc < 2 || !(name = text(t, argv[0], NULL)))
        return JS_Throw(t, (CONST_STRPTR)"writeFile needs a file name and the text");
    if ((body = text(t, argv[1], &n))) {
        if ((fh = Open(name, MODE_NEWFILE))) {
            ok = Write(fh, body, n) == n;
            Close(fh);
        }
        FreeVec(body);
    }
    FreeVec(name);
    return JS_NewBool(t, ok);
}

static void setFunction(JSTask t, JSValue global, const char *name, JSMethodFunc f, struct Context *c)
{
    JSValue fn = JS_NewFunction(t, (CONST_STRPTR)name, f, c);
    JS_SetProp(t, global, (CONST_STRPTR)name, fn);
    JS_Free(t, fn);
}

/* The file's bytes, NUL after them (FreeVec it) */
static STRPTR load(CONST_STRPTR name, LONG *size)
{
    BPTR fh = Open(name, MODE_OLDFILE);
    STRPTR bytes = NULL;
    LONG n;
    if (!fh)
        return NULL;
    Seek(fh, 0, OFFSET_END);
    n = Seek(fh, 0, OFFSET_BEGINNING);
    if (n >= 0 && (bytes = AllocVec(n + 1, MEMF_ANY))) {
        if (Read(fh, bytes, n) != n) {
            FreeVec(bytes);
            bytes = NULL;
        } else {
            bytes[n] = 0;
            *size = n;
        }
    }
    Close(fh);
    return bytes;
}

enum { ARG_FILE, ARG_EVAL, ARG_STACK, ARG_CACHE, ARG_NOCACHE, ARG_ARGS, ARG_COUNT };

int main(void)
{
    LONG args[ARG_COUNT] = { 0 };
    struct RDArgs *rd = ReadArgs((CONST_STRPTR)"FILE,EVAL/K,STACK/K/N,CACHE/K,NOCACHE/S,ARGS/M", args, NULL);
    struct Context c;
    struct TagItem tags[] = {
        { JSA_StackSize, 512 * 1024 }, { JSA_Name, (ULONG)"JS" },
        { JSA_CacheDir, (ULONG)"LIBS:JavaScript/Cache" }, { TAG_DONE, 0 }
    };
    JSTask t;
    JSValue global, list;
    STRPTR source;
    LONG size = -1, error, rc = RETURN_OK, i;
    CONST_STRPTR name;

    if (!rd) {
        PrintFault(IoErr(), (CONST_STRPTR)"JS");
        return RETURN_FAIL;
    }
    c.out = Output();
    if (!args[ARG_FILE] && !args[ARG_EVAL]) {
        put(c.out, "JS: give a script FILE or EVAL \"text\"\n");
        FreeArgs(rd);
        return RETURN_ERROR;
    }
    if (!(JavaScriptBase = OpenLibrary((CONST_STRPTR)JAVASCRIPTNAME, JAVASCRIPTVERSION))) {
        put(c.out, "JS: no javascript.library 1\n");
        FreeArgs(rd);
        return RETURN_FAIL;
    }
    if (args[ARG_STACK] && *(LONG *)args[ARG_STACK] >= 64)
        tags[0].ti_Data = *(LONG *)args[ARG_STACK] * 1024;
    if (args[ARG_CACHE])
        tags[2].ti_Data = args[ARG_CACHE];
    if (args[ARG_NOCACHE] || !args[ARG_FILE])
        tags[2].ti_Tag = TAG_IGNORE;    /* EVAL text is not kept */
    if (!(t = JS_NewTask(tags))) {
        put(c.out, "JS: the JavaScript engine did not start (LIBS:JavaScript/JavaScriptEngine)\n");
        CloseLibrary(JavaScriptBase);
        FreeArgs(rd);
        return RETURN_FAIL;
    }

    global = JS_Global(t);
    setFunction(t, global, "print", fnPrint, &c);
    setFunction(t, global, "readFile", fnReadFile, &c);
    setFunction(t, global, "writeFile", fnWriteFile, &c);
    JS_Evaluate(t, (CONST_STRPTR)"[]", -1, NULL, &list);
    for (i = 0; args[ARG_ARGS] && ((STRPTR *)args[ARG_ARGS])[i]; i++) {
        JSValue s = JS_NewString(t, ((CONST_STRPTR *)args[ARG_ARGS])[i], -1);
        JS_SetIndex(t, list, i, s);
        JS_Free(t, s);
    }
    JS_SetProp(t, global, (CONST_STRPTR)"arguments", list);
    JS_Free(t, list);
    JS_Free(t, global);

    if (args[ARG_EVAL]) {
        source = (STRPTR)args[ARG_EVAL];
        name = (CONST_STRPTR)"EVAL";
        error = JS_Evaluate(t, source, -1, name, NULL);
    } else {
        name = (CONST_STRPTR)args[ARG_FILE];
        if (!(source = load(name, &size))) {
            PrintFault(IoErr(), name);
            rc = RETURN_ERROR;
            error = JSERR_OK;
        } else {
            error = JS_Evaluate(t, source, size, name, NULL);
            FreeVec(source);
        }
    }
    if (error != JSERR_OK) {
        char message[512];
        JS_ErrorText(t, (STRPTR)message, sizeof message);
        put(c.out, "JS: ");
        put(c.out, error == JSERR_EXCEPTION ? message : "the script could not be run");
        put(c.out, "\n");
        rc = RETURN_ERROR;
    }

    JS_EndTask(t);
    CloseLibrary(JavaScriptBase);
    FreeArgs(rd);
    return rc;
}
