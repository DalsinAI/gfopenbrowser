/*
 * webbrowser.library: the messages between the library and the engine
 * (WebBrowserEngine). Private to the two.
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#ifndef WB_PROTOCOL_H
#define WB_PROTOCOL_H

#include <exec/ports.h>

enum {
    WBC_PING = 1,
    WBC_OPEN,        /* arg 0, 1: width, height; arg 2: scripts; arg 3: pictures; task + arg 4: signal bit or -1 */
    WBC_CLOSE,
    WBC_LOAD,        /* text: the URL */
    WBC_LOADHTML,    /* text: the HTML; text2: the base URL */
    WBC_STATE,
    WBC_WAIT,        /* arg 0: seconds; the reply waits for the page */
    WBC_RENDER,      /* data: ARGB, arg 0: stride, arg 1-4: x, y, width, height */
    WBC_TEXT,        /* data, dataSize */
    WBC_TITLE,
    WBC_URL,
    WBC_RESIZE,      /* arg 0, 1 */
    WBC_MOUSE,       /* arg 0: type, 1: x, 2: y, 3: button */
    WBC_KEY,         /* arg 0: down, 1: raw key, 2: qualifiers; text: the characters */
    WBC_SCROLL,      /* arg 0, 1: dx, dy */
    WBC_SHUTDOWN,
    WBC_BROWSER      /* text: the URL (or NULL); arg 0-3: scripts, pictures, web fonts, lite */
};

struct WBMessage {
    struct Message message;
    ULONG command;
    ULONG view;           /* the engine's handle, 1 and up */
    LONG arg[6];
    APTR data;
    LONG dataSize;
    const char *text;
    const char *text2;
    struct Task *task;
    LONG result;
};

#endif
