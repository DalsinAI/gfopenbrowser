/* webbrowser.library. MIT, Copyright (c) 2026 AmiCradle Team. */
#ifndef INLINE_WEBBROWSER_H
#define INLINE_WEBBROWSER_H

#include <inline/macros.h>

#ifndef WEBBROWSER_BASE_NAME
#define WEBBROWSER_BASE_NAME WebBrowserBase
#endif

#define WB_OpenView(width, height, tags) \
    LP3(0x1e, APTR, WB_OpenView, LONG, width, d0, LONG, height, d1, struct TagItem *, tags, a0, , WEBBROWSER_BASE_NAME)
#define WB_CloseView(view) \
    LP1NR(0x24, WB_CloseView, APTR, view, a0, , WEBBROWSER_BASE_NAME)
#define WB_Load(view, url) \
    LP2(0x2a, LONG, WB_Load, APTR, view, a0, CONST_STRPTR, url, a1, , WEBBROWSER_BASE_NAME)
#define WB_LoadHTML(view, html, baseURL) \
    LP3(0x30, LONG, WB_LoadHTML, APTR, view, a0, CONST_STRPTR, html, a1, CONST_STRPTR, baseURL, a2, , WEBBROWSER_BASE_NAME)
#define WB_State(view) \
    LP1(0x36, LONG, WB_State, APTR, view, a0, , WEBBROWSER_BASE_NAME)
#define WB_WaitLoaded(view, seconds) \
    LP2(0x3c, LONG, WB_WaitLoaded, APTR, view, a0, LONG, seconds, d0, , WEBBROWSER_BASE_NAME)
#define WB_Render(view, argb, stride, x, y, width, height) \
    LP7(0x42, LONG, WB_Render, APTR, view, a0, APTR, argb, a1, LONG, stride, d0, LONG, x, d1, LONG, y, d2, \
        LONG, width, d3, LONG, height, d4, , WEBBROWSER_BASE_NAME)
#define WB_RenderRastPort(view, rp, pageX, pageY, x, y, width, height) \
    LP8(0x48, LONG, WB_RenderRastPort, APTR, view, a0, struct RastPort *, rp, a1, LONG, pageX, d0, LONG, pageY, d1, \
        LONG, x, d2, LONG, y, d3, LONG, width, d4, LONG, height, d5, , WEBBROWSER_BASE_NAME)
#define WB_GetText(view, buffer, size) \
    LP3(0x4e, LONG, WB_GetText, APTR, view, a0, STRPTR, buffer, a1, LONG, size, d0, , WEBBROWSER_BASE_NAME)
#define WB_GetTitle(view, buffer, size) \
    LP3(0x54, LONG, WB_GetTitle, APTR, view, a0, STRPTR, buffer, a1, LONG, size, d0, , WEBBROWSER_BASE_NAME)
#define WB_GetURL(view, buffer, size) \
    LP3(0x5a, LONG, WB_GetURL, APTR, view, a0, STRPTR, buffer, a1, LONG, size, d0, , WEBBROWSER_BASE_NAME)
#define WB_Resize(view, width, height) \
    LP3(0x60, LONG, WB_Resize, APTR, view, a0, LONG, width, d0, LONG, height, d1, , WEBBROWSER_BASE_NAME)
#define WB_Mouse(view, type, x, y, button) \
    LP5(0x66, LONG, WB_Mouse, APTR, view, a0, LONG, type, d0, LONG, x, d1, LONG, y, d2, LONG, button, d3, , WEBBROWSER_BASE_NAME)
#define WB_Key(view, down, rawKey, text, qualifiers) \
    LP5(0x6c, LONG, WB_Key, APTR, view, a0, LONG, down, d0, LONG, rawKey, d1, CONST_STRPTR, text, a1, LONG, qualifiers, d2, \
        , WEBBROWSER_BASE_NAME)
#define WB_Scroll(view, dx, dy) \
    LP3(0x72, LONG, WB_Scroll, APTR, view, a0, LONG, dx, d0, LONG, dy, d1, , WEBBROWSER_BASE_NAME)
#define WB_Shutdown() \
    LP0(0x78, LONG, WB_Shutdown, , WEBBROWSER_BASE_NAME)
#define WB_OpenBrowser(url, tags) \
    LP2(0x7e, LONG, WB_OpenBrowser, CONST_STRPTR, url, a0, struct TagItem *, tags, a1, , WEBBROWSER_BASE_NAME)

#endif
