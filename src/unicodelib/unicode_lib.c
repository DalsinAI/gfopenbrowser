/* unicode.library: one shared copy of Unicode for every program.
 * Copyright (c) 2026 AmigaChrome Team. MIT.
 *
 * ICU 78's code and data, loaded once and shared, in place of a copy of
 * ICU inside each program (GFOpenBrowser's engine carried 3.4 MB of ICU
 * code and read 4.6 MB of ICU data at every start).
 *
 * The library has one call, UnicodeTable(count): the first time it is
 * called it reads ICU's data file and hands it to ICU, then it returns a
 * table of pointers to ICU's C functions, in calls.txt's order. A program
 * links ul_stubs.s and ul_client.c in place of ICU's libraries: each of
 * ICU's functions there jumps through that table, so the program's ICU
 * calls run the library's ICU with no change to the program's source.
 * The calls are ordinary C calls (arguments on the stack), as ICU's are.
 *
 * ICU keeps one state for all programs, as it does for all threads of one
 * program: its caches, its mutexes (ul_runtime.c, on Exec semaphores) and
 * its memory (one pool for the library, under a semaphore).
 */
#include <exec/types.h>
#include <dos/dos.h>
#include <exec/resident.h>
#include <exec/libraries.h>
#include <exec/execbase.h>
#include <exec/memory.h>
#include <exec/semaphores.h>
#include <dos/dosextens.h>
#include <proto/exec.h>
#include <proto/dos.h>

#include <unicode/udata.h>
#include <unicode/utypes.h>

#include "ul_count.h"
#include "ul_runtime.h"

#ifndef REG
#define REG(r, decl) register decl __asm(#r)
#endif

#define LIB_VERSION  1
#define LIB_REVISION 0

struct ULBase {
    struct Library lib;
    BPTR seglist;
};

struct ExecBase *SysBase;
struct DosLibrary *DOSBase;

extern void *const ul_table[UL_CALLS];

static const char lib_name[] = "unicode.library";
static const char lib_id[] = "unicode.library 1.0 (23.10.2026) ICU " U_ICU_VERSION ", AmiCradle Team\r\n";
static const char lib_ver[] __attribute__((used)) =
    "\0$VER: unicode.library 1.0 (23.10.2026) ICU " U_ICU_VERSION ", AmiCradle Team";

/* ---- ICU's data -------------------------------------------------------------- */

#define DATA_FILE U_ICUDATA_NAME ".dat"

/* LOCALE:Unicode/ for everyone, LIBS:Unicode/ beside the library, and the
 * calling program's own Unicode/ drawer, where GFOpenBrowser keeps it. */
static const char *const dataFolders[] = {
    "LOCALE:Unicode/",
    "LIBS:Unicode/",
    "PROGDIR:Unicode/",
};

static struct SignalSemaphore setupLock;
static LONG setupState;          /* 1 once ICU has its data */
static APTR dataBlock;           /* AllocMem'd, dataSize + 16 */
static ULONG dataBlockSize;

/* The whole file in memory aligned to 16 bytes, as ICU's tables want. */
static const void *readDataFile(const char *path)
{
    BPTR fh = Open((STRPTR)path, MODE_OLDFILE);
    struct FileInfoBlock *fib;
    const void *aligned = NULL;
    LONG size;

    if (!fh)
        return NULL;
    fib = AllocDosObject(DOS_FIB, NULL);
    if (fib && ExamineFH(fh, fib) && (size = fib->fib_Size) > 0) {
        ULONG blockSize = (ULONG)size + 16;
        UBYTE *block = AllocMem(blockSize, MEMF_ANY);
        if (block) {
            UBYTE *data = (UBYTE *)(((ULONG)block + 15) & ~15UL);
            if (Read(fh, data, size) == size) {
                dataBlock = block;
                dataBlockSize = blockSize;
                aligned = data;
            } else {
                FreeMem(block, blockSize);
            }
        }
    }
    if (fib)
        FreeDosObject(DOS_FIB, fib);
    Close(fh);
    return aligned;
}

static void freeData(void)
{
    if (dataBlock) {
        FreeMem(dataBlock, dataBlockSize);
        dataBlock = NULL;
    }
}

/* Once, in the first caller's process (DOS calls need one). */
static LONG setup(void)
{
    struct Task *me = FindTask(NULL);
    char path[64];
    unsigned i;

    if (me->tc_Node.ln_Type != NT_PROCESS)
        return 0;
    /* No file of ICU's own is ever looked for: all its data is in the
     * common block (fopen() fails at once, ul_runtime.c). */
    for (i = 0; i < sizeof dataFolders / sizeof dataFolders[0]; i++) {
        const void *data;
        UErrorCode error = U_ZERO_ERROR;
        unsigned n = 0;
        const char *s;

        for (s = dataFolders[i]; *s; s++)
            path[n++] = *s;
        for (s = DATA_FILE; *s; s++)
            path[n++] = *s;
        path[n] = 0;
        if (!(data = readDataFile(path)))
            continue;
        udata_setCommonData(data, &error);
        if (U_SUCCESS(error))
            return 1;
        freeData();
    }
    return -1;
}

/* ---- Library housekeeping -------------------------------------------------- */

static struct Library *lib_init(REG(d0, struct ULBase *base), REG(a0, BPTR seglist),
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
    InitSemaphore(&setupLock);
    DOSBase = (struct DosLibrary *)OpenLibrary("dos.library", 39);
    if (!DOSBase || !ul_runtime_init()) {
        ul_runtime_end();
        if (DOSBase)
            CloseLibrary((struct Library *)DOSBase);
        FreeMem((UBYTE *)base - base->lib.lib_NegSize, base->lib.lib_NegSize + base->lib.lib_PosSize);
        return NULL;
    }
    return &base->lib;
}

static struct Library *lib_open(REG(a6, struct ULBase *base))
{
    base->lib.lib_OpenCnt++;
    base->lib.lib_Flags &= ~LIBF_DELEXP;
    return &base->lib;
}

static BPTR lib_expunge(REG(a6, struct ULBase *base));

static BPTR lib_close(REG(a6, struct ULBase *base))
{
    if (base->lib.lib_OpenCnt)
        base->lib.lib_OpenCnt--;
    if (base->lib.lib_OpenCnt == 0 && (base->lib.lib_Flags & LIBF_DELEXP))
        return lib_expunge(base);
    return 0;
}

static BPTR lib_expunge(REG(a6, struct ULBase *base))
{
    BPTR seglist;

    if (base->lib.lib_OpenCnt) {
        base->lib.lib_Flags |= LIBF_DELEXP;
        return 0;
    }
    seglist = base->seglist;
    Remove(&base->lib.lib_Node);
    /* No program has the library open, so no ICU object is in use. ICU's
     * whole heap is the pool, which goes in one call, and its other state
     * is in the library's own hunks: u_cleanup() is not needed, and is not
     * run here, where it would run on whatever stack and under Forbid. */
    ul_runtime_end();
    freeData();
    CloseLibrary((struct Library *)DOSBase);
    FreeMem((UBYTE *)base - base->lib.lib_NegSize, base->lib.lib_NegSize + base->lib.lib_PosSize);
    return seglist;
}

static ULONG lib_null(void)
{
    return 0;
}

/* ---- The call (sfd/unicode_lib.sfd) ----------------------------------------- */

/* The table of ICU's calls, for a program that knows the first `count` of
 * them; NULL when the library has fewer or ICU's data cannot be read. */
static APTR L_UnicodeTable(REG(d0, ULONG count), REG(a6, struct ULBase *base))
{
    LONG state;

    (void)base;
    if (count == 0 || count > UL_CALLS)
        return NULL;
    ObtainSemaphore(&setupLock);
    /* tried again until it works: the next program may bring the file */
    if (setupState != 1)
        setupState = setup();
    state = setupState;
    ReleaseSemaphore(&setupLock);
    return state == 1 ? (APTR)ul_table : NULL;
}

/* ---- RomTag ------------------------------------------------------------------ */

static const APTR lib_vectors[] = {
    (APTR)lib_open,
    (APTR)lib_close,
    (APTR)lib_expunge,
    (APTR)lib_null,
    (APTR)L_UnicodeTable,
    (APTR)-1,
};

static const ULONG lib_inittab[] = {
    sizeof(struct ULBase),
    (ULONG)lib_vectors,
    0,
    (ULONG)lib_init,
};

static const struct Resident lib_romtag __attribute__((used)) = {
    RTC_MATCHWORD,
    (struct Resident *)&lib_romtag,
    (APTR)(&lib_romtag + 1),
    RTF_AUTOINIT,
    LIB_VERSION,
    NT_LIBRARY,
    0,
    (char *)lib_name,
    (char *)lib_id,
    (APTR)lib_inittab,
};
