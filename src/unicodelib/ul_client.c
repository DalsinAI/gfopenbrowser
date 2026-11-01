/* unicode.library from a program: ICU's calls go to the library.
 * Copyright (c) 2026 AmiCradle Team. MIT.
 *
 * Linked with ul_stubs.s (gen_calls.py) in place of ICU's own libraries
 * (libicuuc.a, libicui18n.a, libicudata.a). Each of ICU's functions there
 * jumps through __ul_table. That starts as a boot table: the first ICU
 * call from anywhere opens the library and fetches its table, so a
 * program's ICU calls work whatever order its constructors run in. A
 * program may also call ul_client_open() itself, early, to report a
 * missing library or data file its own way.
 */
#include <exec/types.h>
#include <exec/libraries.h>
#include <exec/tasks.h>
#include <dos/dosextens.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <libraries/unicode.h>
#include <proto/unicode.h>

#include "ul_client.h"
#include "ul_count.h"

extern void *const __ul_boot_table[UL_CALLS];
void *const *__ul_table = __ul_boot_table;
struct Library *UnicodeBase;

/* LIBS:, then a package's own copy: a program in Libs/WebBrowser/ finds
   the drawer's Libs/ above it (PROGDIR:/), one in the drawer itself its
   Libs/ (PROGDIR:Libs/). PROGDIR: only means something to a process. */
static struct Library *openLibrary(void)
{
    static const char *const names[] = {
        UNICODENAME, "PROGDIR:/" UNICODENAME, "PROGDIR:Libs/" UNICODENAME
    };
    struct Task *me = FindTask(NULL);
    struct Library *base = NULL;
    unsigned i, n = me->tc_Node.ln_Type == NT_PROCESS ? 3 : 1;

    for (i = 0; i < n && !base; i++)
        base = OpenLibrary((CONST_STRPTR)names[i], 1);
    return base;
}

int ul_client_open(void)
{
    struct Library *base;
    void *const *table;

    if (__ul_table != __ul_boot_table)
        return UL_OPEN_OK;
    if (!(base = openLibrary()))
        return UL_OPEN_NO_LIBRARY;
    if (!(table = __UnicodeTable_base(base, UL_CALLS))) {
        CloseLibrary(base);
        return UL_OPEN_NO_DATA;
    }
    Forbid();
    if (__ul_table == __ul_boot_table) {
        UnicodeBase = base;
        __ul_table = table;
        base = NULL;
    }
    Permit();
    if (base)
        CloseLibrary(base); /* another task got there first */
    return UL_OPEN_OK;
}

const char *ul_client_error(int code)
{
    switch (code) {
    case UL_OPEN_NO_LIBRARY:
        return "unicode.library (version 1) is not in LIBS: or the program's Libs/";
    case UL_OPEN_NO_DATA:
        return "unicode.library cannot read " UL_DATA_FILE " from LOCALE:Unicode/, LIBS:Unicode/ or PROGDIR:Unicode/";
    default:
        return "";
    }
}

/* From the boot table (ul_stubs.s), with the call's registers saved. */
void __ul_bootstrap(void)
{
    int code = ul_client_open();
    struct Task *me;
    BPTR out;

    if (code == UL_OPEN_OK)
        return;
    me = FindTask(NULL);
    if (me->tc_Node.ln_Type == NT_PROCESS && (out = Output())) {
        const char *s = ul_client_error(code);
        LONG n = 0;

        while (s[n])
            n++;
        Write(out, (APTR)s, n);
        Write(out, (APTR)"\n", 1);
    }
    /* An ICU call cannot go on without ICU. */
    Wait(0);
}

static void __attribute__((destructor)) ul_client_close(void)
{
    if (UnicodeBase) {
        __ul_table = __ul_boot_table;
        CloseLibrary(UnicodeBase);
        UnicodeBase = NULL;
    }
}
