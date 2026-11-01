/*
 * jsimage, the client's side (jsimage.h): the client's first code. It runs
 * before the client's C library is set up, so it uses nothing of it: Exec
 * and DOS through local library bases, no globals.
 *
 * It finds the fixup table (the client's second hunk), loads the image the
 * table names, fixes the client's references to it up once, and runs the
 * image with the client's command line. The image's main runs the
 * client's own start-up (jsimage_host.c); when the client is done, so is
 * the image, and the image is unloaded.
 *
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include <exec/execbase.h>
#include <dos/dosextens.h>
#include <proto/exec.h>
#include <proto/dos.h>

#include "jsimage.h"

/* Everything here, its text included, goes into the entry's own sections,
 * which the link script puts first: our ld resolves a reference to this
 * object's plain .text (where GCC puts the strings) wrongly when the entry
 * is put ahead of the C library's start-up. */
#define ENTRY __attribute__((section(".text.jsimage_entry")))
#define ENTRY_DATA __attribute__((section(".rodata.jsimage_entry")))

/* Where the image is looked for, first to last: in LIBS:, where
 * javascript.library's engine is (one copy on the disk when it is this
 * client's), then beside the client and in the browser's drawer */
static const char jsimage_paths[][48] ENTRY_DATA = {
    "LIBS:JavaScript/JavaScriptEngine",
    "PROGDIR:JavaScriptEngine",
    "PROGDIR:Libs/JavaScript/JavaScriptEngine",
    "PROGDIR:/JavaScript/JavaScriptEngine",
};
#define PATHS (sizeof jsimage_paths / sizeof jsimage_paths[0])
static const char dosName[] ENTRY_DATA = "dos.library";
static const char msgPrefix[] ENTRY_DATA = "jsimage: ";
static const char msgNoTable[] ENTRY_DATA = "this program has no fixup table";
static const char msgWrong[] ENTRY_DATA = "not the JavaScriptEngine this program was built with: ";
static const char msgNone[] ENTRY_DATA = "no matching JavaScriptEngine (beside the program or in LIBS:JavaScript)";
static const char msgEnd[] ENTRY_DATA = "\n";

extern char jsimage_cstart[];           /* the client's C start-up (its script) */
long jsimage_start(char *args, long len) ENTRY __attribute__((used));

/* The program's entry: the first bytes of its hunk */
__asm__(
    "       .section .text.jsimage_entry\n"
    "       .globl  _jsimage_entry\n"
    "_jsimage_entry:\n"
    "       movem.l d2-d7/a2-a6,-(sp)\n"
    "       move.l  d0,-(sp)\n"
    "       move.l  a0,-(sp)\n"
    "       bsr.l   _jsimage_start\n"
    "       addq.l  #8,sp\n"
    "       movem.l (sp)+,d2-d7/a2-a6\n"
    "       rts\n");
extern char jsimage_entry[];

/* The image id in the file at path, read from its header without loading
 * it (an image is one hunk), so that another build is passed over cheaply.
 * 0 when the file is not one hunk as expected: then LoadSeg decides. */
static ENTRY int file_id(struct Library *DOSBase, const char *path, long offset, ULONG *id)
{
    ULONG head[8];
    long at;
    int ok = 0;
    BPTR file = Open((CONST_STRPTR)path, MODE_OLDFILE);
    if (!file)
        return 0;
    /* HUNK_HEADER, no names, 1 hunk, first 0, last 0, its size, then
     * HUNK_CODE and its size; a size with both memory bits has one more
     * long */
    if (Read(file, head, 32) == 32 && head[0] == 0x3F3 && head[1] == 0 && head[2] == 1
        && head[3] == 0 && head[4] == 0 && (head[5] & 0xC0000000) != 0xC0000000
        && (head[6] & 0x3FFFFFFF) == 0x3E9 && offset >= 0 && (ULONG)offset + 4 <= (head[7] & 0x3FFFFFFF) * 4) {
        at = 32 + offset;
        if (Seek(file, at, OFFSET_BEGINNING) != -1 && Read(file, id, 4) == 4)
            ok = 1;
    }
    Close(file);
    return ok;
}

static ENTRY void say(struct Library *DOSBase, const char *a, const char *b)
{
    BPTR out = Output();
    const char *s;
    if (!out)
        return;
    for (s = a; *s; s++) ;
    Write(out, (APTR)a, s - a);
    for (s = b; *s; s++) ;
    Write(out, (APTR)b, s - b);
    Write(out, (APTR)msgEnd, 1);
}

long jsimage_start(char *args, long len)
{
    struct ExecBase *SysBase;
    struct Library *DOSBase;
    struct Process *me;
    struct JSImageTable *t;
    BPTR next, image = 0;
    UBYTE *base = (UBYTE *)jsimage_entry, *at = 0;
    ULONG stack, i, add;
    long rc;
    int p;

    __asm__("move.l 4.w,%0" : "=a"(SysBase));
    me = (struct Process *)FindTask(NULL);
    if (!(DOSBase = OpenLibrary((CONST_STRPTR)dosName, 36)))
        return 20;
    next = *(BPTR *)((ULONG)base - 4);     /* the segment list's next hunk */
    t = next ? (struct JSImageTable *)((UBYTE *)BADDR(next) + 4) : 0;
    if (!t || t->jt_Magic != JSIMAGE_MAGIC || t->jt_Version != JSIMAGE_VERSION) {
        say(DOSBase, msgPrefix, msgNoTable);
        CloseLibrary(DOSBase);
        return 20;
    }
    for (p = 0; p < (int)PATHS && !image; p++) {
        BPTR lock;
        APTR win = me->pr_WindowPtr;
        me->pr_WindowPtr = (APTR)-1;    /* no requester for a missing PROGDIR: */
        lock = Lock((CONST_STRPTR)jsimage_paths[p], ACCESS_READ);
        me->pr_WindowPtr = win;
        if (!lock)
            continue;
        UnLock(lock);
        {
            ULONG id;
            if (file_id(DOSBase, jsimage_paths[p], t->jt_IDOffset, &id) && id != t->jt_ID) {
                say(DOSBase, msgWrong, jsimage_paths[p]);
                continue;
            }
        }
        if (!(image = LoadSeg((CONST_STRPTR)jsimage_paths[p])))
            continue;
        at = (UBYTE *)BADDR(image) + 4;
        if (*(ULONG *)(at + t->jt_IDOffset) != t->jt_ID) {
            say(DOSBase, msgWrong, jsimage_paths[p]);
            UnLoadSeg(image);
            image = 0;
        }
    }
    if (!image) {
        say(DOSBase, msgPrefix, msgNone);
        CloseLibrary(DOSBase);
        return 20;
    }
    /* The references were made for an image at __jsimage_base in this
     * hunk; LoadSeg has added this hunk's address to the absolute ones */
    add = (ULONG)at - (ULONG)base - t->jt_Base;
    if (add != t->jt_Applied) {         /* differs only when made resident */
        for (i = 0; i < t->jt_Count; i++)
            *(ULONG *)(base + t->jt_Offset[i]) += add - t->jt_Applied;
        t->jt_Applied = add;
        CacheClearU();
    }
    *(void **)(at + t->jt_ClientOffset) = jsimage_cstart;

    stack = (ULONG)me->pr_Task.tc_SPUpper - (ULONG)me->pr_Task.tc_SPLower;
    if (me->pr_CLI) {
        ULONG cli = (ULONG)((struct CommandLineInterface *)BADDR(me->pr_CLI))->cli_DefaultStack * 4;
        if (cli > stack)
            stack = cli;
    }
    if (stack < 65536)
        stack = 65536;
    rc = RunCommand(image, stack, (STRPTR)args, len);
    UnLoadSeg(image);
    CloseLibrary(DOSBase);
    return rc;
}
