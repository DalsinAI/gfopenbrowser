/*
 * GFOpenBrowser's title window, shared by the launcher (ob_launcher.c, the
 * small GFOpenBrowser program the icon starts) and the browser itself
 * (WebBrowserEngine's window; GFOpenBrowser.engine). The launcher opens the window and a public message
 * port; the browser sends it what it is doing, and asks it to close when its
 * own window is open.
 * MIT, Copyright (c) 2026 AmigaChrome Team.
 */
#ifndef OB_SPLASH_H
#define OB_SPLASH_H

#include <exec/ports.h>

#define OB_SPLASH_PORT "GFOPENBROWSER.SPLASH"

/* percent: 0 to 100 for the bar, OB_SPLASH_CLOSE to close the window. */
#define OB_SPLASH_CLOSE (-1)

/* Sent and forgotten: the title window frees each message with FreeVec. */
struct OBSplashMessage {
    struct Message message;
    LONG percent;
    char text[80];
};

/* Tells the title window what is happening (a no-op when there is none). */
void ob_splash(const char *text, int percent);

#endif
