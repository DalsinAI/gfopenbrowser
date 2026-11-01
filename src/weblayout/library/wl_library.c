/*
 * weblayout.library 1.0: weblayout (../weblayout.h) as a resident library.
 *
 * The romtag, the library's own Open, Close and Expunge, and one vector per
 * weblayout.h call, in weblayout_lib.sfd's order. Each vector takes its
 * arguments in the registers the SFD names and calls the engine's C
 * function. The library's only global state is the bases it opens in its
 * Open (graphics, layers, openlayout, and datatypes when there is a 44 or
 * later) and closes in its Expunge. Memory is an Exec pool per document
 * (../wl_mem.c); the C library calls the engine needs are its own
 * (../wl_libc.c, wl_runtime.c), so it runs in any program, whatever its
 * C library.
 *
 * MIT, Copyright (c) 2026 AmigaChrome Team.
 */
#include <exec/types.h>
#include <exec/resident.h>
#include <exec/libraries.h>
#include <exec/execbase.h>
#include <exec/memory.h>
#include <dos/dos.h>
#include <proto/exec.h>
#include <libraries/openlayout.h>

#include "weblayout.h"

#define REG(r, decl) register decl __asm(#r)
#define LIB_VERSION 1
#define LIB_REVISION 0

struct WebLayoutLibBase {
    struct Library lib;
    BPTR seglist;
};

struct ExecBase *SysBase;
struct GfxBase *GfxBase;
struct Library *LayersBase;
struct Library *DataTypesBase;
struct Library *OpenLayoutBase;

/* run from a Shell, the library does nothing */
int start(void) { return -1; }

static const char lib_name[] = "weblayout.library";
static const char lib_id[] =
    "weblayout.library 1.0 (20.10.2026) GFOpenBrowser Lite 0.4.1, The AmiCradle Team\r\n";
static const char lib_ver[] __attribute__((used)) =
    "$VER: weblayout.library 1.0 (20.10.2026)";

static struct Library *lib_init(REG(d0, struct WebLayoutLibBase *base),
                                REG(a0, BPTR seglist),
                                REG(a6, struct ExecBase *sys));
static struct Library *lib_open(REG(a6, struct WebLayoutLibBase *base));
static BPTR lib_close(REG(a6, struct WebLayoutLibBase *base));
static BPTR lib_expunge(REG(a6, struct WebLayoutLibBase *base));
static ULONG lib_null(void);

static WLDocument *LIB_NewDocument(REG(a0, CONST_STRPTR data), REG(d0, ULONG length),
                                   REG(d1, ULONG kind), REG(a1, CONST_STRPTR base_url),
                                   REG(a6, struct WebLayoutLibBase *base));
static void LIB_DisposeDocument(REG(a0, WLDocument *doc), REG(a6, struct WebLayoutLibBase *base));
static CONST_STRPTR LIB_GetTitle(REG(a0, const WLDocument *doc), REG(a6, struct WebLayoutLibBase *base));
static BOOL LIB_AddStyleSheet(REG(a0, WLDocument *doc), REG(a1, CONST_STRPTR css),
                              REG(d0, ULONG length), REG(a6, struct WebLayoutLibBase *base));
static ULONG LIB_CountResources(REG(a0, const WLDocument *doc), REG(d0, ULONG kind),
                                REG(a6, struct WebLayoutLibBase *base));
static BOOL LIB_GetResource(REG(a0, const WLDocument *doc), REG(d0, ULONG kind),
                            REG(d1, ULONG index), REG(a1, struct WLResource *res),
                            REG(a6, struct WebLayoutLibBase *base));
static BOOL LIB_Layout(REG(a0, WLDocument *doc), REG(a1, const struct WLView *view),
                       REG(a6, struct WebLayoutLibBase *base));
static LONG LIB_ContentHeight(REG(a0, const WLDocument *doc), REG(a6, struct WebLayoutLibBase *base));
static BOOL LIB_SetImageFile(REG(a0, WLDocument *doc), REG(d0, LONG id),
                             REG(a1, CONST_STRPTR path), REG(a2, struct Screen *screen),
                             REG(a6, struct WebLayoutLibBase *base));
static void LIB_Draw(REG(a0, const WLDocument *doc), REG(a1, const struct WLView *view),
                     REG(a6, struct WebLayoutLibBase *base));
static CONST_STRPTR LIB_HitLink(REG(a0, WLDocument *doc), REG(a1, const struct WLView *view),
                                REG(d0, WORD x), REG(d1, WORD y),
                                REG(a6, struct WebLayoutLibBase *base));
static BOOL LIB_ResolveURL(REG(a0, CONST_STRPTR base_url), REG(a1, CONST_STRPTR href),
                           REG(a2, STRPTR out), REG(d0, ULONG outlen),
                           REG(a6, struct WebLayoutLibBase *base));
static BOOL LIB_Open(REG(a6, struct WebLayoutLibBase *base));
static void LIB_Close(REG(a6, struct WebLayoutLibBase *base));

/* weblayout_lib.sfd's order: -30 WL_NewDocument ... -108 WL_Close */
static const APTR lib_vectors[] = {
    (APTR)lib_open, (APTR)lib_close, (APTR)lib_expunge, (APTR)lib_null,
    (APTR)LIB_NewDocument, (APTR)LIB_DisposeDocument, (APTR)LIB_GetTitle,
    (APTR)LIB_AddStyleSheet, (APTR)LIB_CountResources, (APTR)LIB_GetResource,
    (APTR)LIB_Layout, (APTR)LIB_ContentHeight, (APTR)LIB_SetImageFile,
    (APTR)LIB_Draw, (APTR)LIB_HitLink, (APTR)LIB_ResolveURL,
    (APTR)LIB_Open, (APTR)LIB_Close,
    (APTR)-1
};

static const struct {
    ULONG size;
    const APTR *vectors;
    APTR data;
    APTR init;
} lib_inittable = {
    sizeof(struct WebLayoutLibBase), lib_vectors, NULL, (APTR)lib_init
};

const struct Resident lib_romtag = {
    RTC_MATCHWORD, (struct Resident *)&lib_romtag, (APTR)(&lib_romtag + 1),
    RTF_AUTOINIT, LIB_VERSION, NT_LIBRARY, 0,
    (char *)lib_name, (char *)lib_id, (APTR)&lib_inittable
};

static struct Library *lib_init(REG(d0, struct WebLayoutLibBase *base),
                                REG(a0, BPTR seglist),
                                REG(a6, struct ExecBase *sys))
{
    SysBase = sys;
    base->seglist = seglist;
    base->lib.lib_Revision = LIB_REVISION;
    return &base->lib;
}

static void close_bases(void)
{
    if (OpenLayoutBase) { CloseLibrary(OpenLayoutBase); OpenLayoutBase = NULL; }
    if (DataTypesBase) { CloseLibrary(DataTypesBase); DataTypesBase = NULL; }
    if (LayersBase) { CloseLibrary(LayersBase); LayersBase = NULL; }
    if (GfxBase) { CloseLibrary((struct Library *)GfxBase); GfxBase = NULL; }
}

/* the bases, once, by the first opener; one call per statement (no library
 * call in another's arguments) */
static BOOL open_bases(void)
{
    struct Library *lib;
    if (OpenLayoutBase) return TRUE;
    lib = OpenLibrary((CONST_STRPTR)"graphics.library", 39);
    GfxBase = (struct GfxBase *)lib;
    LayersBase = OpenLibrary((CONST_STRPTR)"layers.library", 39);
    DataTypesBase = OpenLibrary((CONST_STRPTR)"datatypes.library", 44);   /* pictures: optional */
    OpenLayoutBase = OpenLibrary((CONST_STRPTR)OPENLAYOUTLIB_NAME, OPENLAYOUTLIB_VERSION);
    if (!GfxBase || !LayersBase || !OpenLayoutBase) {
        close_bases();
        return FALSE;
    }
    return TRUE;
}

static struct Library *lib_open(REG(a6, struct WebLayoutLibBase *base))
{
    /* counted first, so an expunge while the bases open cannot take us */
    base->lib.lib_OpenCnt++;
    base->lib.lib_Flags &= ~LIBF_DELEXP;
    if (!open_bases()) {
        base->lib.lib_OpenCnt--;
        return NULL;
    }
    return &base->lib;
}

static BPTR lib_close(REG(a6, struct WebLayoutLibBase *base))
{
    base->lib.lib_OpenCnt--;
    if (base->lib.lib_OpenCnt == 0 && (base->lib.lib_Flags & LIBF_DELEXP))
        return lib_expunge(base);
    return 0;
}

static BPTR lib_expunge(REG(a6, struct WebLayoutLibBase *base))
{
    BPTR seglist;
    if (base->lib.lib_OpenCnt) {
        base->lib.lib_Flags |= LIBF_DELEXP;
        return 0;
    }
    close_bases();
    seglist = base->seglist;
    Remove(&base->lib.lib_Node);
    FreeMem((UBYTE *)base - base->lib.lib_NegSize,
            base->lib.lib_NegSize + base->lib.lib_PosSize);
    return seglist;
}

static ULONG lib_null(void) { return 0; }

static WLDocument *LIB_NewDocument(REG(a0, CONST_STRPTR data), REG(d0, ULONG length),
                                   REG(d1, ULONG kind), REG(a1, CONST_STRPTR base_url),
                                   REG(a6, struct WebLayoutLibBase *base))
{
    (void)base;
    return WL_NewDocument(data, length, kind, base_url);
}

static void LIB_DisposeDocument(REG(a0, WLDocument *doc), REG(a6, struct WebLayoutLibBase *base))
{
    (void)base;
    WL_DisposeDocument(doc);
}

static CONST_STRPTR LIB_GetTitle(REG(a0, const WLDocument *doc), REG(a6, struct WebLayoutLibBase *base))
{
    (void)base;
    return WL_GetTitle(doc);
}

static BOOL LIB_AddStyleSheet(REG(a0, WLDocument *doc), REG(a1, CONST_STRPTR css),
                              REG(d0, ULONG length), REG(a6, struct WebLayoutLibBase *base))
{
    (void)base;
    return WL_AddStyleSheet(doc, css, length);
}

static ULONG LIB_CountResources(REG(a0, const WLDocument *doc), REG(d0, ULONG kind),
                                REG(a6, struct WebLayoutLibBase *base))
{
    (void)base;
    return WL_CountResources(doc, kind);
}

static BOOL LIB_GetResource(REG(a0, const WLDocument *doc), REG(d0, ULONG kind),
                            REG(d1, ULONG index), REG(a1, struct WLResource *res),
                            REG(a6, struct WebLayoutLibBase *base))
{
    (void)base;
    return WL_GetResource(doc, kind, index, res);
}

static BOOL LIB_Layout(REG(a0, WLDocument *doc), REG(a1, const struct WLView *view),
                       REG(a6, struct WebLayoutLibBase *base))
{
    (void)base;
    return WL_Layout(doc, view);
}

static LONG LIB_ContentHeight(REG(a0, const WLDocument *doc), REG(a6, struct WebLayoutLibBase *base))
{
    (void)base;
    return WL_ContentHeight(doc);
}

static BOOL LIB_SetImageFile(REG(a0, WLDocument *doc), REG(d0, LONG id),
                             REG(a1, CONST_STRPTR path), REG(a2, struct Screen *screen),
                             REG(a6, struct WebLayoutLibBase *base))
{
    (void)base;
    return WL_SetImageFile(doc, id, path, screen);
}

static void LIB_Draw(REG(a0, const WLDocument *doc), REG(a1, const struct WLView *view),
                     REG(a6, struct WebLayoutLibBase *base))
{
    (void)base;
    WL_Draw(doc, view);
}

static CONST_STRPTR LIB_HitLink(REG(a0, WLDocument *doc), REG(a1, const struct WLView *view),
                                REG(d0, WORD x), REG(d1, WORD y),
                                REG(a6, struct WebLayoutLibBase *base))
{
    (void)base;
    return WL_HitLink(doc, view, x, y);
}

static BOOL LIB_ResolveURL(REG(a0, CONST_STRPTR base_url), REG(a1, CONST_STRPTR href),
                           REG(a2, STRPTR out), REG(d0, ULONG outlen),
                           REG(a6, struct WebLayoutLibBase *base))
{
    (void)base;
    return WL_ResolveURL(base_url, href, out, outlen);
}

/* the bases are the library's own, opened in its Open: ready when it is */
static BOOL LIB_Open(REG(a6, struct WebLayoutLibBase *base))
{
    (void)base;
    return WL_Open();
}

static void LIB_Close(REG(a6, struct WebLayoutLibBase *base))
{
    (void)base;
}
