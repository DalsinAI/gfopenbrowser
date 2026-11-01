/*
 * weblayout: a web page laid out and drawn into a RastPort.
 *
 * The public face of GFOpenBrowser Lite's page engine: HTML Lite (the
 * semantic HTML adapter), CSS Lite (the small cascade), GFOpenLayout (the
 * geometry and the display list) and page pictures through
 * picture.datatype. GFOpenBrowser Lite calls nothing else of the engine.
 *
 * It is shaped as a library (docs/WEBLAYOUT_LIBRARY.md): opaque documents,
 * Exec types, no callbacks, no varargs and no C library types in the calls,
 * and structures that carry their own size. It is linked into a program,
 * or opened as LIBS:weblayout.library 1, whose vectors are these calls
 * (src/weblayout/library: weblayout_lib.sfd, and <proto/weblayout.h> from
 * it). A program using the library includes <proto/weblayout.h> after this
 * header and opens WebLayoutBase before WL_Open.
 *
 * A page, step by step:
 *
 *   doc = WL_NewDocument(bytes, length, WLKIND_HTML, "https://host/page");
 *   for each WLRES_STYLESHEET: fetch it, WL_AddStyleSheet(doc, css, len)
 *   WL_Layout(doc, &view)                       (width, font, RastPort)
 *   for each WLRES_IMAGE: fetch it to a file, WL_SetImageFile(doc, ...)
 *   WL_Draw(doc, &view)                         (again after scrolling)
 *   url = WL_HitLink(doc, &view, mouse_x, mouse_y)
 *   WL_DisposeDocument(doc)
 *
 * Fetching is the caller's: weblayout never opens a socket or a file
 * except the picture files it is handed.
 *
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#ifndef WEBLAYOUT_H
#define WEBLAYOUT_H

#include <exec/types.h>

#ifdef __cplusplus
extern "C" {
#endif

#define WEBLAYOUT_NAME    "weblayout.library"
#define WEBLAYOUT_VERSION 1   /* the first weblayout.library: docs/WEBLAYOUT_LIBRARY.md */

struct RastPort;
struct TextFont;
struct Screen;

/* A laid-out page; opaque, made by WL_NewDocument. */
typedef struct WLDocument WLDocument;

/* What the bytes given to WL_NewDocument are. */
#define WLKIND_HTML 0   /* HTML, UTF-8 */
#define WLKIND_TEXT 1   /* plain text, UTF-8, shown as it is */

/* Where a page is drawn, and how. WL_Layout reads wv_RPort, wv_Font and
 * wv_Width; WL_Draw and WL_HitLink read all of it. Pixels throughout. */
struct WLView {
    ULONG wv_Size;                  /* sizeof(struct WLView) */
    struct RastPort *wv_RPort;
    struct TextFont *wv_Font;       /* the page's text, already opened */
    WORD wv_Left, wv_Top;           /* the page's area in the RastPort */
    WORD wv_Width, wv_Height;
    LONG wv_ScrollY;                /* the page's first line shown */
    UWORD wv_TextPen, wv_BackPen, wv_LinkPen, wv_FillPen;
};

/* What a page asks for (WL_GetResource). */
#define WLRES_STYLESHEET 1   /* <link rel="stylesheet">: listed after WL_NewDocument */
#define WLRES_IMAGE      2   /* <img>: listed after WL_Layout, with its size there */

struct WLResource {
    ULONG wr_Size;                  /* sizeof(struct WLResource) */
    ULONG wr_Kind;                  /* WLRES_... */
    LONG wr_ID;                     /* WL_SetImageFile's id; 0 for a stylesheet */
    CONST_STRPTR wr_URL;            /* absolute, the document's: a picture's is good until
                                       the next WL_GetResource or WL_HitLink, a
                                       stylesheet's until WL_DisposeDocument */
    UWORD wr_Width, wr_Height;      /* WLRES_IMAGE: its box on the page */
};

/* A page from UTF-8 bytes. base_url (may be NULL) is the page's own
 * address, which relative addresses are made absolute against. NULL when
 * there is no memory. The bytes are copied. */
WLDocument *WL_NewDocument(CONST_STRPTR data, ULONG length, ULONG kind,
                           CONST_STRPTR base_url);
void WL_DisposeDocument(WLDocument *doc);

/* The <title>, UTF-8, "" when there is none. */
CONST_STRPTR WL_GetTitle(const WLDocument *doc);

/* CSS fetched for the page, in the page's order. The next WL_Layout
 * builds the page again with it. TRUE when taken. */
BOOL WL_AddStyleSheet(WLDocument *doc, CONST_STRPTR css, ULONG length);

/* How many of kind the page asks for, and the index'th of them. */
ULONG WL_CountResources(const WLDocument *doc, ULONG kind);
BOOL WL_GetResource(const WLDocument *doc, ULONG kind, ULONG index,
                    struct WLResource *res);

/* Lay the page out at view->wv_Width, measuring with wv_Font in wv_RPort.
 * Again whenever the width changes. TRUE when laid out. */
BOOL WL_Layout(WLDocument *doc, const struct WLView *view);

/* The laid-out page's height in pixels; 0 before WL_Layout. */
LONG WL_ContentHeight(const WLDocument *doc);

/* Decode the picture at path (any picture.datatype can read) for image id,
 * remapped for screen and scaled to its box where the datatype can.
 * The document keeps the picture until it is disposed; keep the file
 * until then too. TRUE when decoded; a picture that is not decoded is
 * drawn as its box and its alt text. */
BOOL WL_SetImageFile(WLDocument *doc, LONG id, CONST_STRPTR path,
                     struct Screen *screen);

/* Draw the page into the view's area, scrolled by wv_ScrollY. doc may be
 * NULL: the area is cleared. */
void WL_Draw(const WLDocument *doc, const struct WLView *view);

/* The absolute address of the link at (x, y) in the RastPort, or NULL.
 * Good until the next WL_HitLink or WL_DisposeDocument. */
CONST_STRPTR WL_HitLink(WLDocument *doc, const struct WLView *view,
                        WORD x, WORD y);

/* Make href (as written on a page) absolute against base into out.
 * TRUE when it fitted. */
BOOL WL_ResolveURL(CONST_STRPTR base, CONST_STRPTR href, STRPTR out,
                   ULONG outlen);

/* Open and close what the engine needs (gfopenlayout.library when it is
 * built for the resident GFOpenLayout, and datatypes.library on first use).
 * TRUE when it is ready. */
BOOL WL_Open(void);
void WL_Close(void);

#ifdef __cplusplus
}
#endif
#endif
