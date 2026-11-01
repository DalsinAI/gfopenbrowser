/*
 * webbrowser.library: web pages for any Amiga program. URL in, a page
 * ready to use out: its picture (32-bit pixels, or drawn into a RastPort),
 * its text and title.
 *
 *   struct Library *WebBrowserBase = OpenLibrary("webbrowser.library", 1);
 *   APTR view = WB_OpenView(640, 480, NULL);
 *   WB_Load(view, "https://www.bbc.co.uk/");
 *   WB_WaitLoaded(view, 120);                    seconds at most
 *   WB_RenderRastPort(view, window->RPort, 0, 0, 0, 0, 640, 480);
 *   WB_CloseView(view);
 *   CloseLibrary(WebBrowserBase);
 *
 * One engine serves every program. The library starts it the first time it
 * is opened (LIBS:WebBrowser/WebBrowserEngine) and keeps it in memory after
 * the last program closes the library, so later programs find it ready;
 * WB_Shutdown() ends it. Calls are synchronous and may come from any task.
 *
 * WB_OpenBrowser(url, tags) opens GFOpenBrowser's own window in the engine
 * (or shows the page in it when it is already open): the GFOpenBrowser icon
 * does just that, so the browser opens at once while the engine is in
 * memory.
 *
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#ifndef LIBRARIES_WEBBROWSER_H
#define LIBRARIES_WEBBROWSER_H

#include <exec/types.h>
#include <utility/tagitem.h>

#define WEBBROWSER_NAME "webbrowser.library"
#define WEBBROWSER_ENGINE_PORT "WEBBROWSER.ENGINE"

/* WB_OpenView tags */
#define WBA_Dummy    (TAG_USER + 0x57420000)
#define WBA_Scripts  (WBA_Dummy + 1)   /* BOOL, default TRUE: run the pages' JavaScript */
#define WBA_Pictures (WBA_Dummy + 2)   /* BOOL, default TRUE */
#define WBA_Signal   (WBA_Dummy + 3)   /* ULONG signal bit: Signal()led to the opener when the page changes or finishes */
#define WBA_WebFonts (WBA_Dummy + 4)   /* BOOL, default FALSE: fonts the page downloads (WB_OpenBrowser) */
#define WBA_Lite     (WBA_Dummy + 5)   /* BOOL, default FALSE: lighter pages (WB_OpenBrowser) */

/* WB_State flags */
#define WBS_LOADING  (1 << 0)
#define WBS_FAILED   (1 << 1)
#define WBS_CHANGED  (1 << 2)          /* drawn differently since the last render */

/* Mouse events for WB_Mouse */
#define WBM_MOVE 0
#define WBM_DOWN 1
#define WBM_UP   2

/* Errors (negative results) */
#define WBERR_NOENGINE (-1)
#define WBERR_NOMEMORY (-2)
#define WBERR_BADVIEW  (-3)
#define WBERR_TIMEOUT  (-4)

#endif
