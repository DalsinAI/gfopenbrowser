/*
 * GFOpenBrowser's launcher: the small GFOpenBrowser program that the icon (or a
 * Shell) starts. The browser runs in WebBrowserEngine, the one engine behind
 * webbrowser.library: the launcher opens LIBS:webbrowser.library, or the
 * drawer's own PROGDIR:Libs/webbrowser.library before Install, and asks it
 * for a browser window (WB_OpenBrowser). The library starts the engine when
 * it is not running: LIBS:WebBrowser/WebBrowserEngine, or else the drawer's
 * PROGDIR:Libs/WebBrowser/WebBrowserEngine. The engine is about 76 MB and
 * takes a while to load from disk, so the launcher first opens a title
 * window; the engine reports its own steps to it (ob_splash.h) and closes it
 * once its window is open.
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include <stdio.h>
#include <string.h>
#include <exec/execbase.h>
#include <exec/memory.h>
#include <dos/dos.h>
#include <dos/dosextens.h>
#include <dos/dostags.h>
#include <graphics/gfxbase.h>
#include <graphics/text.h>
#include <intuition/intuition.h>
#include <intuition/screens.h>
#include <workbench/startup.h>
#include <workbench/workbench.h>
#include <utility/tagitem.h>
#include <proto/exec.h>
#include <proto/icon.h>
#include <proto/dos.h>
#include <proto/graphics.h>
#include <proto/intuition.h>

#include "ob_splash.h"
#include "libraries/webbrowser.h"
#include "proto/webbrowser.h"

static const char version[] __attribute__((used)) = "$VER: GFOpenBrowser 0.8.4 (1.11.2026)";

struct IntuitionBase *IntuitionBase;
struct GfxBase *GfxBase;
struct Library *WebBrowserBase;

static struct Task *mainTask;
static volatile int splashDone;  /* set, under Forbid, as the title window's process ends */

/* ---- The title window, in a process of its own so that it stays alive
 * while the main process is busy loading. */

static struct Window *window;
static struct DrawInfo *drawInfo;
static char statusText[80] = "Loading GFOpenBrowser";
static LONG percent;

static void drawText(struct RastPort *rp, const char *text, WORD x, WORD y, WORD width)
{
    struct TextExtent extent;
    ULONG fits = TextFit(rp, (CONST_STRPTR)text, strlen(text), &extent, NULL, 1, width, rp->TxHeight + 1);
    Move(rp, x, y + rp->TxBaseline);
    Text(rp, (CONST_STRPTR)text, fits);
}

static void drawWindow(void)
{
    struct RastPort *rp = window->RPort;
    UWORD *pens = drawInfo->dri_Pens;
    WORD left = window->BorderLeft + 10, top = window->BorderTop + 6;
    WORD width = window->Width - window->BorderLeft - window->BorderRight - 20;
    WORD line = rp->TxHeight + 4, barTop = top + 2 * line + 2, barHeight = rp->TxHeight;
    WORD filled = (WORD)((width - 4) * (percent < 0 ? 0 : percent > 100 ? 100 : percent) / 100);

    SetDrMd(rp, JAM1);
    SetAPen(rp, pens[BACKGROUNDPEN]);
    RectFill(rp, window->BorderLeft, window->BorderTop, window->Width - window->BorderRight - 1,
        window->Height - window->BorderBottom - 1);
    SetAPen(rp, pens[TEXTPEN]);
    drawText(rp, version + 6, left, top, width);
    drawText(rp, statusText, left, top + line, width);
    /* A recessed box with the bar inside. */
    SetAPen(rp, pens[SHADOWPEN]);
    RectFill(rp, left, barTop, left + width - 1, barTop);
    RectFill(rp, left, barTop, left, barTop + barHeight - 1);
    SetAPen(rp, pens[SHINEPEN]);
    RectFill(rp, left + 1, barTop + barHeight - 1, left + width - 1, barTop + barHeight - 1);
    RectFill(rp, left + width - 1, barTop + 1, left + width - 1, barTop + barHeight - 1);
    if (filled > 0) {
        SetAPen(rp, pens[FILLPEN]);
        RectFill(rp, left + 2, barTop + 2, left + 1 + filled, barTop + barHeight - 3);
    }
}

static void splashMain(void)
{
    struct MsgPort *port = CreateMsgPort();
    struct Screen *screen = LockPubScreen(NULL);
    int open = 1;

    if (port && screen) {
        WORD fontHeight = screen->Font->ta_YSize, width = 340;
        WORD height = screen->WBorTop + fontHeight + 1 + 6 + 2 * (fontHeight + 4) + 2 + fontHeight + 10 + screen->WBorBottom;
        port->mp_Node.ln_Name = (char *)OB_SPLASH_PORT;
        port->mp_Node.ln_Pri = 0;
        AddPort(port);
        drawInfo = GetScreenDrawInfo(screen);
        window = OpenWindowTags(NULL, WA_PubScreen, (ULONG)screen,
            WA_Left, (screen->Width - width) / 2, WA_Top, (screen->Height - height) / 2,
            WA_Width, width, WA_Height, height, WA_Title, (ULONG)"GFOpenBrowser",
            WA_DragBar, TRUE, WA_DepthGadget, TRUE, WA_SmartRefresh, TRUE, WA_Activate, TRUE,
            WA_IDCMP, IDCMP_REFRESHWINDOW, TAG_DONE);
    }
    if (screen)
        UnlockPubScreen(NULL, screen);
    if (window)
        drawWindow();
    Signal(mainTask, SIGBREAKF_CTRL_F);

    while (open && window) {
        ULONG got = Wait((1UL << port->mp_SigBit) | (1UL << window->UserPort->mp_SigBit) | SIGBREAKF_CTRL_C);
        struct OBSplashMessage *message;
        struct IntuiMessage *intuiMessage;
        int changed = 0;
        while ((message = (struct OBSplashMessage *)GetMsg(port))) {
            if (message->percent == OB_SPLASH_CLOSE)
                open = 0;
            else {
                if (message->text[0])
                    strcpy(statusText, message->text);
                percent = message->percent;
                changed = 1;
            }
            FreeVec(message);
        }
        while ((intuiMessage = (struct IntuiMessage *)GetMsg(window->UserPort))) {
            if (intuiMessage->Class == IDCMP_REFRESHWINDOW) {
                BeginRefresh(window);
                drawWindow();
                EndRefresh(window, TRUE);
            }
            ReplyMsg((struct Message *)intuiMessage);
        }
        if (changed && open)
            drawWindow();
        if (got & SIGBREAKF_CTRL_C)
            open = 0;
    }

    if (port) {
        struct Message *message;
        if (port->mp_Node.ln_Name) {
            Forbid();
            RemPort(port);
            Permit();
        }
        while ((message = GetMsg(port)))
            FreeVec(message);
        DeleteMsgPort(port);
    }
    if (window)
        CloseWindow(window);
    if (drawInfo) {
        struct Screen *screen = LockPubScreen(NULL);
        if (screen) {
            FreeScreenDrawInfo(screen, drawInfo);
            UnlockPubScreen(NULL, screen);
        }
    }
    /* Under Forbid until this process has gone: its code is the launcher's,
     * which the main process unloads when it ends. */
    Forbid();
    splashDone = 1;
    Signal(mainTask, SIGBREAKF_CTRL_E);
}

/* The icon's tool types (JAVASCRIPT=NO, PICTURES=NO, WEBFONTS=YES,
 * LITE=YES) as webbrowser.library tags. */
static void toolTypeTags(int argc, char **argv, struct TagItem *tags)
{
    struct WBStartup *startup = (struct WBStartup *)argv;
    struct Library *IconBase;
    struct DiskObject *icon;
    BPTR oldDir;
    STRPTR value;
    tags[0].ti_Tag = WBA_Scripts;   tags[0].ti_Data = TRUE;
    tags[1].ti_Tag = WBA_Pictures;  tags[1].ti_Data = TRUE;
    tags[2].ti_Tag = WBA_WebFonts;  tags[2].ti_Data = FALSE;
    tags[3].ti_Tag = WBA_Lite;      tags[3].ti_Data = FALSE;
    tags[4].ti_Tag = TAG_DONE;
    if (argc != 0 || !startup || !startup->sm_NumArgs)
        return;
    if (!(IconBase = OpenLibrary((CONST_STRPTR)"icon.library", 37)))
        return;
    oldDir = CurrentDir(startup->sm_ArgList[0].wa_Lock);
    icon = GetDiskObject(startup->sm_ArgList[0].wa_Name);
    CurrentDir(oldDir);
    if (icon) {
        if ((value = FindToolType((CONST_STRPTR *)icon->do_ToolTypes, (CONST_STRPTR)"JAVASCRIPT")) && MatchToolValue(value, (CONST_STRPTR)"NO"))
            tags[0].ti_Data = FALSE;
        if ((value = FindToolType((CONST_STRPTR *)icon->do_ToolTypes, (CONST_STRPTR)"PICTURES")) && MatchToolValue(value, (CONST_STRPTR)"NO"))
            tags[1].ti_Data = FALSE;
        if ((value = FindToolType((CONST_STRPTR *)icon->do_ToolTypes, (CONST_STRPTR)"WEBFONTS")) && MatchToolValue(value, (CONST_STRPTR)"YES"))
            tags[2].ti_Data = TRUE;
        if ((value = FindToolType((CONST_STRPTR *)icon->do_ToolTypes, (CONST_STRPTR)"LITE")) && MatchToolValue(value, (CONST_STRPTR)"YES"))
            tags[3].ti_Data = TRUE;
        FreeDiskObject(icon);
    }
    CloseLibrary(IconBase);
}

/* The browser window opens in webbrowser.library's engine (1.1 or newer):
 * at once when the engine is already in memory. Returns -1 when there is no
 * such library, in LIBS: or in this drawer's Libs. */
static LONG openInLibrary(int argc, char **argv, struct Task **splash)
{
    struct TagItem tags[5];
    LONG result;
    if (!(WebBrowserBase = OpenLibrary((CONST_STRPTR)WEBBROWSER_NAME, 1))
        && !(WebBrowserBase = OpenLibrary((CONST_STRPTR)"PROGDIR:Libs/" WEBBROWSER_NAME, 1)))
        return -1;
    if (WebBrowserBase->lib_Version == 1 && WebBrowserBase->lib_Revision < 1) {
        CloseLibrary(WebBrowserBase);
        WebBrowserBase = NULL;
        return -1;
    }
    if (*splash && !FindPort((CONST_STRPTR)WEBBROWSER_ENGINE_PORT))
        ob_splash("Loading the browser engine", 10);
    toolTypeTags(argc, argv, tags);
    result = WB_OpenBrowser((CONST_STRPTR)(argc > 1 ? argv[1] : ""), tags);
    if (result < 0) {
        ob_splash("The browser engine did not start", 0);
        Delay(5 * TICKS_PER_SECOND);
    }
    CloseLibrary(WebBrowserBase);
    WebBrowserBase = NULL;
    return result < 0 ? RETURN_FAIL : RETURN_OK;
}

int main(int argc, char **argv)
{
    struct Process *self = (struct Process *)FindTask(NULL);
    struct Task *splash = NULL;
    LONG result;

    mainTask = &self->pr_Task;
    IntuitionBase = (struct IntuitionBase *)OpenLibrary((CONST_STRPTR)"intuition.library", 39);
    GfxBase = (struct GfxBase *)OpenLibrary((CONST_STRPTR)"graphics.library", 39);
    /* No title window when the resident engine is there to open the
     * browser at once. */
    if (IntuitionBase && GfxBase && !FindPort((CONST_STRPTR)OB_SPLASH_PORT) && !FindPort((CONST_STRPTR)WEBBROWSER_ENGINE_PORT)) {
        SetSignal(0, SIGBREAKF_CTRL_E | SIGBREAKF_CTRL_F);
        splash = (struct Task *)CreateNewProcTags(NP_Entry, (ULONG)splashMain, NP_Name, (ULONG)"GFOpenBrowser title",
            NP_StackSize, 8192, NP_Priority, 1, TAG_DONE);
        if (splash)
            Wait(SIGBREAKF_CTRL_F | SIGBREAKF_CTRL_E);
    }

    if ((result = openInLibrary(argc, argv, &splash)) < 0) {
        result = RETURN_FAIL;
        ob_splash("GFOpenBrowser needs webbrowser.library 1.1", 0);
        if (argc)
            printf("GFOpenBrowser: needs webbrowser.library 1.1 or newer in LIBS: or in this drawer's Libs\n");
        Delay(5 * TICKS_PER_SECOND);
    }

    if (splash) {
        /* The engine closed the title window when its own window opened;
         * otherwise close it now, and wait until its process has gone. */
        ob_splash(NULL, OB_SPLASH_CLOSE);
        Forbid();
        while (!splashDone)
            Wait(SIGBREAKF_CTRL_E);
        Permit();
    }
    if (GfxBase)
        CloseLibrary((struct Library *)GfxBase);
    if (IntuitionBase)
        CloseLibrary((struct Library *)IntuitionBase);
    return result;
}
