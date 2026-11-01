/*
 * GFOpenBrowser: JavaScriptCore options from the environment, for tests.
 * JavaScriptCore reads JSC_<name> with getenv(), but in the engine that
 * getenv() is libnix's inside the JavaScriptEngine image, whose environ is
 * never filled in, so JSC_ variables never reached it. The variable OBJSC
 * (a Shell's local Set, else ENV:) holds options in JSC's own form,
 * "name=value name=value", handed to JSC::Options::setOptions() at start.
 * MIT, Copyright (c) 2026 AmigaChrome Team.
 */
#include <exec/types.h>
#include <dos/var.h>
#include <proto/dos.h>

int ob_jsc_options_from_env(char *buffer, int size)
{
    LONG length;
    if (!buffer || size < 2)
        return -1;
    buffer[0] = 0;
    length = GetVar((CONST_STRPTR)"OBJSC", (STRPTR)buffer, size, 0);
    if (length <= 0) {
        buffer[0] = 0;
        return -1;
    }
    return (int)length;
}

/* OBJSCLOG names a file for JavaScriptCore's dataLog (where its GC verifier
 * and option dumps write); without it they go to stderr. */
int ob_jsc_log_from_env(char *buffer, int size)
{
    LONG length;
    if (!buffer || size < 2)
        return -1;
    buffer[0] = 0;
    length = GetVar((CONST_STRPTR)"OBJSCLOG", (STRPTR)buffer, size, 0);
    if (length <= 0) {
        buffer[0] = 0;
        return -1;
    }
    return (int)length;
}
