/*
 * webbrowser.library (libraries/webbrowser.h): every call becomes one
 * WBMessage to the engine's port, answered when the engine has done it.
 * The engine is started the first time a program needs it and stays in
 * memory after the last close, so the next program finds it ready.
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include <string.h>
#include <exec/memory.h>
#include <exec/resident.h>
#include <exec/libraries.h>
#include <dos/dos.h>
#include <dos/dostags.h>
#include <graphics/rastport.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/utility.h>

#include "libraries/webbrowser.h"
#include "wb_protocol.h"
#include "ob_cgx.h"

#ifndef REG
#define REG(reg, arg) arg __asm(#reg)
#endif

const char LibName[] = WEBBROWSER_NAME;
const char LibVersion[] __attribute__((used)) = "$VER: webbrowser.library 1.1 (6.10.2026)";
#define LibIdString (LibVersion + 6)

struct ExecBase *SysBase;
struct DosLibrary *DOSBase;
struct Library *UtilityBase;
struct Library *CyberGfxBase;
static BPTR segList;

#define ENGINE_PATH "LIBS:WebBrowser/WebBrowserEngine"
/* Not installed: GFOpenBrowser's drawer, as unpacked, carries the library and
 * the engine in Libs (PROGDIR: of the program asking). */
#define ENGINE_PATH_DRAWER "PROGDIR:Libs/WebBrowser/WebBrowserEngine"
#define ENGINE_WAIT 180                       /* seconds for a cold start on a slow disk */

/* One request, waited for. Returns the engine's result, or WBERR_NOENGINE. */
static LONG ask(struct WBMessage *m)
{
    struct MsgPort *reply = CreateMsgPort(), *engine;
    LONG result = WBERR_NOENGINE;
    if (!reply)
        return WBERR_NOMEMORY;
    m->message.mn_Node.ln_Type = NT_MESSAGE;
    m->message.mn_ReplyPort = reply;
    m->message.mn_Length = sizeof *m;
    m->task = FindTask(NULL);
    Forbid();
    engine = FindPort((CONST_STRPTR)WEBBROWSER_ENGINE_PORT);
    if (engine)
        PutMsg(engine, &m->message);
    Permit();
    if (engine) {
        WaitPort(reply);
        GetMsg(reply);
        result = m->result;
    }
    DeleteMsgPort(reply);
    return result;
}

static LONG simple(ULONG command, APTR view)
{
    struct WBMessage m;
    memset(&m, 0, sizeof m);
    m.command = command;
    m.view = (ULONG)view;
    return ask(&m);
}

/* Starts the engine when it is not running, and waits for its port. */
static int ensureEngine(void)
{
    char path[300];
    BPTR lock;
    int i;
    if (FindPort((CONST_STRPTR)WEBBROWSER_ENGINE_PORT))
        return 1;
    /* The engine's full name: the shell that starts it has no PROGDIR: of
     * ours. */
    if (!(lock = Lock((CONST_STRPTR)ENGINE_PATH, ACCESS_READ)) && !(lock = Lock((CONST_STRPTR)ENGINE_PATH_DRAWER, ACCESS_READ)))
        return 0;
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
    if (SystemTags((CONST_STRPTR)path, SYS_Asynch, TRUE, SYS_Input, Open((CONST_STRPTR)"NIL:", MODE_OLDFILE),
            SYS_Output, Open((CONST_STRPTR)"NIL:", MODE_NEWFILE), NP_Name, (ULONG)"WebBrowserEngine", TAG_DONE) == -1)
        return 0;
    for (i = 0; i < ENGINE_WAIT; i++) {
        Delay(TICKS_PER_SECOND);
        if (FindPort((CONST_STRPTR)WEBBROWSER_ENGINE_PORT))
            return 1;
    }
    return 0;
}

/* ---- the calls (LVO -30 onwards, in this order) ---- */

static APTR wbOpenView(REG(d0, LONG width), REG(d1, LONG height), REG(a0, struct TagItem *tags), REG(a6, struct Library *base))
{
    struct WBMessage m;
    LONG result;
    (void)base;
    if (!ensureEngine())
        return NULL;
    memset(&m, 0, sizeof m);
    m.command = WBC_OPEN;
    m.arg[0] = width;
    m.arg[1] = height;
    m.arg[2] = GetTagData(WBA_Scripts, TRUE, tags);
    m.arg[3] = GetTagData(WBA_Pictures, TRUE, tags);
    m.arg[4] = (LONG)GetTagData(WBA_Signal, (ULONG)-1, tags);
    result = ask(&m);
    /* The engine quits when idle, and may have just done so. */
    if (result == WBERR_NOENGINE && ensureEngine())
        result = ask(&m);
    return result > 0 ? (APTR)result : NULL;
}

static void wbCloseView(REG(a0, APTR view), REG(a6, struct Library *base))
{
    (void)base;
    if (view)
        simple(WBC_CLOSE, view);
}

static LONG wbLoad(REG(a0, APTR view), REG(a1, CONST_STRPTR url), REG(a6, struct Library *base))
{
    struct WBMessage m;
    (void)base;
    memset(&m, 0, sizeof m);
    m.command = WBC_LOAD;
    m.view = (ULONG)view;
    m.text = (const char *)url;
    return ask(&m);
}

static LONG wbLoadHTML(REG(a0, APTR view), REG(a1, CONST_STRPTR html), REG(a2, CONST_STRPTR baseURL), REG(a6, struct Library *base))
{
    struct WBMessage m;
    (void)base;
    memset(&m, 0, sizeof m);
    m.command = WBC_LOADHTML;
    m.view = (ULONG)view;
    m.text = (const char *)html;
    m.text2 = (const char *)baseURL;
    return ask(&m);
}

static LONG wbState(REG(a0, APTR view), REG(a6, struct Library *base))
{
    (void)base;
    return simple(WBC_STATE, view);
}

static LONG wbWaitLoaded(REG(a0, APTR view), REG(d0, LONG seconds), REG(a6, struct Library *base))
{
    struct WBMessage m;
    (void)base;
    memset(&m, 0, sizeof m);
    m.command = WBC_WAIT;
    m.view = (ULONG)view;
    m.arg[0] = seconds;
    return ask(&m);
}

static LONG wbRender(REG(a0, APTR view), REG(a1, APTR argb), REG(d0, LONG stride), REG(d1, LONG x), REG(d2, LONG y),
                     REG(d3, LONG width), REG(d4, LONG height), REG(a6, struct Library *base))
{
    struct WBMessage m;
    (void)base;
    memset(&m, 0, sizeof m);
    m.command = WBC_RENDER;
    m.view = (ULONG)view;
    m.data = argb;
    m.arg[0] = stride;
    m.arg[1] = x;
    m.arg[2] = y;
    m.arg[3] = width;
    m.arg[4] = height;
    return ask(&m);
}

static LONG wbRenderRastPort(REG(a0, APTR view), REG(a1, struct RastPort *rp), REG(d0, LONG pageX), REG(d1, LONG pageY),
                             REG(d2, LONG x), REG(d3, LONG y), REG(d4, LONG width), REG(d5, LONG height),
                             REG(a6, struct Library *base))
{
    UBYTE *pixels;
    LONG result;
    if (width <= 0 || height <= 0)
        return 0;
    if (!CyberGfxBase)
        CyberGfxBase = OpenLibrary((CONST_STRPTR)"cybergraphics.library", 40);
    if (!CyberGfxBase)
        return WBERR_NOENGINE;
    pixels = AllocVec(width * height * 4, MEMF_ANY);
    if (!pixels)
        return WBERR_NOMEMORY;
    result = wbRender(view, pixels, width * 4, pageX, pageY, width, height, base);
    if (result >= 0)
        ob_WritePixelArray(pixels, 0, 0, width * 4, rp, x, y, width, height, RECTFMT_ARGB);
    FreeVec(pixels);
    return result;
}

static LONG textCall(ULONG command, APTR view, STRPTR buffer, LONG size)
{
    struct WBMessage m;
    memset(&m, 0, sizeof m);
    m.command = command;
    m.view = (ULONG)view;
    m.data = buffer;
    m.dataSize = size;
    return ask(&m);
}

static LONG wbGetText(REG(a0, APTR view), REG(a1, STRPTR buffer), REG(d0, LONG size), REG(a6, struct Library *base))
{
    (void)base;
    return textCall(WBC_TEXT, view, buffer, size);
}

static LONG wbGetTitle(REG(a0, APTR view), REG(a1, STRPTR buffer), REG(d0, LONG size), REG(a6, struct Library *base))
{
    (void)base;
    return textCall(WBC_TITLE, view, buffer, size);
}

static LONG wbGetURL(REG(a0, APTR view), REG(a1, STRPTR buffer), REG(d0, LONG size), REG(a6, struct Library *base))
{
    (void)base;
    return textCall(WBC_URL, view, buffer, size);
}

static LONG wbResize(REG(a0, APTR view), REG(d0, LONG width), REG(d1, LONG height), REG(a6, struct Library *base))
{
    struct WBMessage m;
    (void)base;
    memset(&m, 0, sizeof m);
    m.command = WBC_RESIZE;
    m.view = (ULONG)view;
    m.arg[0] = width;
    m.arg[1] = height;
    return ask(&m);
}

static LONG wbMouse(REG(a0, APTR view), REG(d0, LONG type), REG(d1, LONG x), REG(d2, LONG y), REG(d3, LONG button),
                    REG(a6, struct Library *base))
{
    struct WBMessage m;
    (void)base;
    memset(&m, 0, sizeof m);
    m.command = WBC_MOUSE;
    m.view = (ULONG)view;
    m.arg[0] = type;
    m.arg[1] = x;
    m.arg[2] = y;
    m.arg[3] = button;
    return ask(&m);
}

static LONG wbKey(REG(a0, APTR view), REG(d0, LONG down), REG(d1, LONG rawKey), REG(a1, CONST_STRPTR text),
                  REG(d2, LONG qualifiers), REG(a6, struct Library *base))
{
    struct WBMessage m;
    (void)base;
    memset(&m, 0, sizeof m);
    m.command = WBC_KEY;
    m.view = (ULONG)view;
    m.arg[0] = down;
    m.arg[1] = rawKey;
    m.arg[2] = qualifiers;
    m.text = (const char *)text;
    return ask(&m);
}

static LONG wbScroll(REG(a0, APTR view), REG(d0, LONG dx), REG(d1, LONG dy), REG(a6, struct Library *base))
{
    struct WBMessage m;
    (void)base;
    memset(&m, 0, sizeof m);
    m.command = WBC_SCROLL;
    m.view = (ULONG)view;
    m.arg[0] = dx;
    m.arg[1] = dy;
    return ask(&m);
}

static LONG wbOpenBrowser(REG(a0, CONST_STRPTR url), REG(a1, struct TagItem *tags), REG(a6, struct Library *base))
{
    struct WBMessage m;
    LONG result;
    (void)base;
    if (!ensureEngine())
        return WBERR_NOENGINE;
    memset(&m, 0, sizeof m);
    m.command = WBC_BROWSER;
    m.text = (const char *)url;
    m.arg[0] = GetTagData(WBA_Scripts, TRUE, tags);
    m.arg[1] = GetTagData(WBA_Pictures, TRUE, tags);
    m.arg[2] = GetTagData(WBA_WebFonts, FALSE, tags);
    m.arg[3] = GetTagData(WBA_Lite, FALSE, tags);
    result = ask(&m);
    if (result == WBERR_NOENGINE && ensureEngine())
        result = ask(&m);
    return result;
}

static LONG wbShutdown(REG(a6, struct Library *base))
{
    (void)base;
    return simple(WBC_SHUTDOWN, NULL);
}

/* ---- the library ---- */

static struct Library *libInit(REG(d0, struct Library *lib), REG(a0, BPTR seg), REG(a6, struct ExecBase *sysBase))
{
    SysBase = sysBase;
    segList = seg;
    lib->lib_Node.ln_Type = NT_LIBRARY;
    lib->lib_Node.ln_Name = (char *)LibName;
    lib->lib_Flags = LIBF_SUMUSED | LIBF_CHANGED;
    lib->lib_Version = 1;
    lib->lib_Revision = 1;
    lib->lib_IdString = (APTR)LibIdString;
    DOSBase = (struct DosLibrary *)OpenLibrary((CONST_STRPTR)"dos.library", 37);
    UtilityBase = OpenLibrary((CONST_STRPTR)"utility.library", 37);
    if (!DOSBase || !UtilityBase) {
        if (DOSBase)
            CloseLibrary((struct Library *)DOSBase);
        if (UtilityBase)
            CloseLibrary(UtilityBase);
        FreeMem((UBYTE *)lib - lib->lib_NegSize, lib->lib_NegSize + lib->lib_PosSize);
        return NULL;
    }
    return lib;
}

static struct Library *libOpen(REG(a6, struct Library *lib))
{
    lib->lib_OpenCnt++;
    lib->lib_Flags &= ~LIBF_DELEXP;
    return lib;
}

static BPTR libExpunge(REG(a6, struct Library *lib))
{
    BPTR seg;
    if (lib->lib_OpenCnt) {
        lib->lib_Flags |= LIBF_DELEXP;
        return 0;
    }
    /* The engine stays: it is its own program, ended by WB_Shutdown. */
    Remove(&lib->lib_Node);
    if (CyberGfxBase)
        CloseLibrary(CyberGfxBase);
    CloseLibrary(UtilityBase);
    CloseLibrary((struct Library *)DOSBase);
    seg = segList;
    FreeMem((UBYTE *)lib - lib->lib_NegSize, lib->lib_NegSize + lib->lib_PosSize);
    return seg;
}

static BPTR libClose(REG(a6, struct Library *lib))
{
    if (--lib->lib_OpenCnt == 0 && (lib->lib_Flags & LIBF_DELEXP))
        return libExpunge(lib);
    return 0;
}

static ULONG libNull(void)
{
    return 0;
}

static const APTR funcTable[] = {
    (APTR)libOpen, (APTR)libClose, (APTR)libExpunge, (APTR)libNull,
    (APTR)wbOpenView,        /* -30 */
    (APTR)wbCloseView,       /* -36 */
    (APTR)wbLoad,            /* -42 */
    (APTR)wbLoadHTML,        /* -48 */
    (APTR)wbState,           /* -54 */
    (APTR)wbWaitLoaded,      /* -60 */
    (APTR)wbRender,          /* -66 */
    (APTR)wbRenderRastPort,  /* -72 */
    (APTR)wbGetText,         /* -78 */
    (APTR)wbGetTitle,        /* -84 */
    (APTR)wbGetURL,          /* -90 */
    (APTR)wbResize,          /* -96 */
    (APTR)wbMouse,           /* -102 */
    (APTR)wbKey,             /* -108 */
    (APTR)wbScroll,          /* -114 */
    (APTR)wbShutdown,        /* -120 */
    (APTR)wbOpenBrowser,     /* -126 */
    (APTR)-1
};

static const ULONG initTable[] = { sizeof(struct Library), (ULONG)funcTable, 0, (ULONG)libInit };

const struct Resident wb_romtag __attribute__((used)) = {
    RTC_MATCHWORD, (struct Resident *)&wb_romtag, (APTR)(&wb_romtag + 1), RTF_AUTOINIT, 1, NT_LIBRARY, 0,
    (char *)LibName, (char *)LibIdString, (APTR)initTable
};
