/*
 * GFOpenBrowser's browser window inside WebBrowserEngine (webbrowser.library's
 * resident engine): ob_browser.c built with OB_BROWSER_IN_ENGINE. The engine
 * runs WebCore and calls these from its loop.
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#ifndef OB_BROWSER_H
#define OB_BROWSER_H

#include <exec/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Opens the window and loads url (or loads it in the window already open
 * and brings that to the front). 1 on success. */
int obb_open(const char *url, int scripts, int pictures, int webFonts, int lite, int network);

/* The window's signal, to wait on; 0 when no window is open. */
ULONG obb_signals(void);

/* Input, then repaint what changed. 0 once the window has closed. */
int obb_update(void);

/* Closes the window, if open, and the GUI libraries. */
void obb_close(void);

#ifdef __cplusplus
}
#endif

#endif
