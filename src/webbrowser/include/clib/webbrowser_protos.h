/* webbrowser.library. MIT, Copyright (c) 2026 AmiCradle Team. */
#ifndef CLIB_WEBBROWSER_PROTOS_H
#define CLIB_WEBBROWSER_PROTOS_H

#include <libraries/webbrowser.h>
#include <graphics/rastport.h>

APTR WB_OpenView(LONG width, LONG height, struct TagItem *tags);
void WB_CloseView(APTR view);
LONG WB_Load(APTR view, CONST_STRPTR url);
LONG WB_LoadHTML(APTR view, CONST_STRPTR html, CONST_STRPTR baseURL);
LONG WB_State(APTR view);
LONG WB_WaitLoaded(APTR view, LONG seconds);
LONG WB_Render(APTR view, APTR argb, LONG stride, LONG x, LONG y, LONG width, LONG height);
LONG WB_RenderRastPort(APTR view, struct RastPort *rp, LONG pageX, LONG pageY, LONG x, LONG y, LONG width, LONG height);
LONG WB_GetText(APTR view, STRPTR buffer, LONG size);
LONG WB_GetTitle(APTR view, STRPTR buffer, LONG size);
LONG WB_GetURL(APTR view, STRPTR buffer, LONG size);
LONG WB_Resize(APTR view, LONG width, LONG height);
LONG WB_Mouse(APTR view, LONG type, LONG x, LONG y, LONG button);
LONG WB_Key(APTR view, LONG down, LONG rawKey, CONST_STRPTR text, LONG qualifiers);
LONG WB_Scroll(APTR view, LONG dx, LONG dy);
LONG WB_Shutdown(void);
LONG WB_OpenBrowser(CONST_STRPTR url, struct TagItem *tags);

#endif
