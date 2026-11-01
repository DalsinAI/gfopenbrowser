/*
 * weblayout: the calls of weblayout.h, over HTML Lite, CSS Lite,
 * GFOpenLayout, the painter and the picture loader.
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include "weblayout.h"

#include <string.h>

#include "wl_html.h"
#include "wl_image_dt.h"
#include "wl_mem.h"
#include "wl_openlayout.h"
#include "wl_paint.h"
#include "wl_url.h"

#define WL_URL_MAX 1280
#define WL_IMAGE_MAX 32
#define WL_CSS_MAX (256UL * 1024UL)

struct wl_image {
    LONG id;
    wl_dt_image picture;
};

struct WLDocument {
    wl_pool *pool;                  /* everything below comes from it */
    ULONG kind;
    char *source;                   /* the page's bytes, for building it again with CSS */
    ULONG source_len;
    char *base;                     /* "" when none */
    char *css;                      /* the stylesheets added, one after another */
    ULONG css_len;
    BOOL css_changed;
    wl_html_result page;            /* page.document is the GFOpenLayout tree */
    BOOL laid_out;
    char url[WL_URL_MAX];           /* WL_GetResource's and WL_HitLink's answer */
    /* The title and the stylesheets' addresses are the page's own, made
     * once by WL_NewDocument: building the page again with CSS (WL_Layout
     * after WL_AddStyleSheet) leaves them, as weblayout.h promises. */
    char *title;                    /* WL_GetTitle's answer */
    char **sheet_urls;              /* absolute, one per page.stylesheet_href */
    int nsheets;
    struct wl_image images[WL_IMAGE_MAX];
    int nimages;
};

static char *copy_bytes(WLDocument *doc, const char *s, ULONG n)
{
    return wl_strndup(doc->pool, s, (size_t)n);
}

static LONG to_px(ol_unit u)
{
    if (u >= 0) return (LONG)((u + OL_UNITS_PER_CSSPX / 2) / OL_UNITS_PER_CSSPX);
    return -(LONG)((-u + OL_UNITS_PER_CSSPX / 2) / OL_UNITS_PER_CSSPX);
}

static void free_sheet_urls(WLDocument *doc)
{
    int i;
    if (!doc->sheet_urls) return;
    for (i = 0; i < doc->nsheets; ++i) wl_free(doc->sheet_urls[i]);
    wl_free(doc->sheet_urls);
    doc->sheet_urls = NULL;
    doc->nsheets = 0;
}

static void free_images(WLDocument *doc)
{
    int i;
    for (i = 0; i < doc->nimages; ++i) wl_dt_image_free(&doc->images[i].picture);
    memset(doc->images, 0, sizeof doc->images);
    doc->nimages = 0;
}

static int resolve_sheets(WLDocument *doc)
{
    int i;
    free_sheet_urls(doc);
    if (doc->page.nstylesheets <= 0) return 1;
    doc->sheet_urls = (char **)wl_alloc(doc->pool, (size_t)doc->page.nstylesheets * sizeof(char *));
    if (!doc->sheet_urls) return 0;
    doc->nsheets = doc->page.nstylesheets;
    for (i = 0; i < doc->page.nstylesheets; ++i) {
        wl_url_resolve(doc->base, doc->page.stylesheet_href[i], doc->url, sizeof doc->url);
        doc->sheet_urls[i] = copy_bytes(doc, doc->url, (ULONG)strlen(doc->url));
        if (!doc->sheet_urls[i]) return 0;
    }
    return 1;
}

/* plain text: one block, the text as it is */
static int build_text(WLDocument *doc)
{
    ol_node *root, *pre, *text;
    ol_style style;
    memset(&doc->page, 0, sizeof doc->page);
    doc->page.pool = doc->pool;
    doc->page.document = ol_document_new();
    doc->page.title = copy_bytes(doc, "", 0);
    if (!doc->page.document || !doc->page.title) return 0;
    root = ol_document_root(doc->page.document);
    pre = ol_node_append(doc->page.document, root, OL_ROLE_BLOCK, NULL);
    text = pre ? ol_node_append(doc->page.document, pre, OL_ROLE_TEXT, doc->source) : NULL;
    if (!pre || !text) return 0;
    ol_style_init(&style);
    style.display = OL_DISPLAY_BLOCK;
    style.padding_top = style.padding_right = style.padding_bottom = style.padding_left = OL_CSSPX(8);
    ol_node_set_style(pre, &style);
    style.display = OL_DISPLAY_INLINE;
    style.text_flags = OL_TEXT_MONO;
    ol_node_set_style(text, &style);
    return 1;
}

/* the page's tree, from its bytes and the CSS added so far */
static int build(WLDocument *doc)
{
    wl_html_result_free(&doc->page);
    free_images(doc);
    doc->laid_out = FALSE;
    doc->css_changed = FALSE;
    if (doc->kind == WLKIND_TEXT) return build_text(doc);
    return wl_html_parse_in(doc->pool, doc->source, doc->source_len,
                            doc->css_len ? doc->css : NULL, doc->css_len, &doc->page);
}

WLDocument *WL_NewDocument(CONST_STRPTR data, ULONG length, ULONG kind,
                           CONST_STRPTR base_url)
{
    WLDocument *doc;
    wl_pool *pool;
    ULONG baselen;
    if (!data) length = 0;
    if (kind != WLKIND_HTML && kind != WLKIND_TEXT) return NULL;
    pool = wl_pool_new();
    if (!pool) return NULL;
    doc = (WLDocument *)wl_alloc(pool, sizeof *doc);
    if (!doc) {
        wl_pool_delete(pool);
        return NULL;
    }
    doc->pool = pool;
    doc->page.pool = pool;
    doc->kind = kind;
    doc->source = copy_bytes(doc, (const char *)data, length);
    doc->source_len = length;
    baselen = base_url ? (ULONG)strlen((const char *)base_url) : 0;
    doc->base = copy_bytes(doc, base_url ? (const char *)base_url : "", baselen);
    if (!doc->source || !doc->base || !build(doc) ||
        !(doc->title = copy_bytes(doc, doc->page.title, (ULONG)strlen(doc->page.title))) ||
        !resolve_sheets(doc)) {
        WL_DisposeDocument(doc);
        return NULL;
    }
    return doc;
}

void WL_DisposeDocument(WLDocument *doc)
{
    wl_pool *pool;
    if (!doc) return;
    free_images(doc);
    free_sheet_urls(doc);
    wl_html_result_free(&doc->page);
    wl_free(doc->source);
    wl_free(doc->base);
    wl_free(doc->css);
    wl_free(doc->title);
    pool = doc->pool;
    wl_free(doc);
    wl_pool_delete(pool);           /* and anything still in it */
}

CONST_STRPTR WL_GetTitle(const WLDocument *doc)
{
    if (!doc || !doc->title) return (CONST_STRPTR)"";
    return (CONST_STRPTR)doc->title;
}

BOOL WL_AddStyleSheet(WLDocument *doc, CONST_STRPTR css, ULONG length)
{
    char *more;
    if (!doc || !css || !length || doc->kind != WLKIND_HTML) return FALSE;
    if (doc->css_len + length + 1 > WL_CSS_MAX) return FALSE;
    more = (char *)wl_realloc(doc->pool, doc->css, (size_t)doc->css_len + length + 2);
    if (!more) return FALSE;
    memcpy(more + doc->css_len, css, length);
    doc->css_len += length;
    more[doc->css_len++] = '\n';
    more[doc->css_len] = 0;
    doc->css = more;
    doc->css_changed = TRUE;
    return TRUE;
}

static const ol_display_op *image_op(const WLDocument *doc, ULONG index)
{
    size_t i, count;
    ULONG seen = 0;
    if (!doc->laid_out) return NULL;
    count = ol_display_count(doc->page.document);
    for (i = 0; i < count; ++i) {
        const ol_display_op *op = ol_display_get(doc->page.document, i);
        if (op && op->type == OL_OP_IMAGE && seen++ == index) return op;
    }
    return NULL;
}

ULONG WL_CountResources(const WLDocument *doc, ULONG kind)
{
    size_t i, count;
    ULONG n = 0;
    if (!doc) return 0;
    if (kind == WLRES_STYLESHEET) return doc->nsheets > 0 ? (ULONG)doc->nsheets : 0;
    if (kind != WLRES_IMAGE || !doc->laid_out) return 0;
    count = ol_display_count(doc->page.document);
    for (i = 0; i < count; ++i) {
        const ol_display_op *op = ol_display_get(doc->page.document, i);
        if (op && op->type == OL_OP_IMAGE) ++n;
    }
    return n;
}

BOOL WL_GetResource(const WLDocument *doc, ULONG kind, ULONG index,
                    struct WLResource *res)
{
    WLDocument *d = (WLDocument *)doc;   /* the answer's address lives in doc->url */
    if (!doc || !res || res->wr_Size < sizeof *res) return FALSE;
    if (kind == WLRES_STYLESHEET) {
        if (index >= WL_CountResources(doc, kind) || !doc->sheet_urls) return FALSE;
        res->wr_Kind = kind;
        res->wr_ID = 0;
        res->wr_URL = (CONST_STRPTR)doc->sheet_urls[index];
        res->wr_Width = res->wr_Height = 0;
        return TRUE;
    }
    if (kind == WLRES_IMAGE) {
        const ol_display_op *op = image_op(doc, index);
        const ol_node *node;
        const char *src;
        LONG w, h;
        if (!op) return FALSE;
        node = ol_node_by_id(doc->page.document, op->u.image.node_id);
        src = node ? ol_node_value(node) : NULL;
        if (!src || !*src) d->url[0] = 0;
        else wl_url_resolve(doc->base, src, d->url, sizeof d->url);
        w = to_px(op->u.image.bounds.width);
        h = to_px(op->u.image.bounds.height);
        res->wr_Kind = kind;
        res->wr_ID = (LONG)op->u.image.image_id;
        res->wr_URL = (CONST_STRPTR)d->url;
        res->wr_Width = (UWORD)(w > 0 ? (w < 0xffff ? w : 0xffff) : 1);
        res->wr_Height = (UWORD)(h > 0 ? (h < 0xffff ? h : 0xffff) : 1);
        return TRUE;
    }
    return FALSE;
}

static struct wl_image *find_image(const WLDocument *doc, LONG id)
{
    int i;
    for (i = 0; i < doc->nimages; ++i)
        if (doc->images[i].id == id) return (struct wl_image *)&doc->images[i];
    return NULL;
}

static int draw_image(void *userdata, LONG image_id, struct RastPort *rp,
                      WORD x, WORD y, UWORD width, UWORD height)
{
    struct wl_image *image = find_image((const WLDocument *)userdata, image_id);
    return image ? wl_dt_image_draw(&image->picture, rp, x, y, width, height) : 0;
}

/* the painter's view of a WLView */
static int view_from(wl_view *v, const WLDocument *doc, const struct WLView *view)
{
    if (!view || view->wv_Size < sizeof *view || !view->wv_RPort || !view->wv_Font) return 0;
    wl_view_init(v, view->wv_RPort, view->wv_Font);
    wl_view_set_rect(v, view->wv_Left, view->wv_Top, view->wv_Width, view->wv_Height);
    wl_view_set_pens(v, view->wv_TextPen, view->wv_BackPen, view->wv_LinkPen, view->wv_FillPen);
    if (doc) {
        v->pool = doc->pool;
        wl_view_set_image_drawer(v, draw_image, (void *)doc);
        v->document = doc->page.document;
        wl_view_scroll_to(v, view->wv_ScrollY);
    }
    return 1;
}

BOOL WL_Layout(WLDocument *doc, const struct WLView *view)
{
    wl_view v;
    if (!doc || !view_from(&v, NULL, view)) return FALSE;
    v.pool = doc->pool;
    if (doc->css_changed && !build(doc)) return FALSE;
    doc->laid_out = wl_view_set_document(&v, doc->page.document) ? TRUE : FALSE;
    return doc->laid_out;
}

LONG WL_ContentHeight(const WLDocument *doc)
{
    if (!doc || !doc->laid_out) return 0;
    return to_px(ol_document_content_height(doc->page.document));
}

BOOL WL_SetImageFile(WLDocument *doc, LONG id, CONST_STRPTR path,
                     struct Screen *screen)
{
    struct wl_image *slot;
    struct WLResource res;
    ULONG i, n;
    UWORD w = 1, h = 1;
    if (!doc || !path || !*path || !screen || !doc->laid_out) return FALSE;
    if (find_image(doc, id) || doc->nimages >= WL_IMAGE_MAX) return FALSE;
    n = WL_CountResources(doc, WLRES_IMAGE);
    for (i = 0; i < n; ++i) {
        res.wr_Size = sizeof res;
        if (WL_GetResource(doc, WLRES_IMAGE, i, &res) && res.wr_ID == id) {
            w = res.wr_Width;
            h = res.wr_Height;
            break;
        }
    }
    if (i == n) return FALSE;
    slot = &doc->images[doc->nimages];
    memset(slot, 0, sizeof *slot);
    if (!wl_dt_image_load(&slot->picture, (const char *)path, screen, w, h)) return FALSE;
    slot->id = id;
    doc->nimages++;
    return TRUE;
}

void WL_Draw(const WLDocument *doc, const struct WLView *view)
{
    wl_view v;
    if (!view_from(&v, doc && doc->laid_out ? doc : NULL, view)) return;
    wl_view_draw(&v);
}

CONST_STRPTR WL_HitLink(WLDocument *doc, const struct WLView *view, WORD x, WORD y)
{
    wl_view v;
    const char *href;
    if (!doc || !doc->laid_out || !view_from(&v, doc, view)) return NULL;
    href = wl_view_hit_link(&v, x, y);
    if (!href) return NULL;
    wl_url_resolve(doc->base, href, doc->url, sizeof doc->url);
    return (CONST_STRPTR)doc->url;
}

BOOL WL_ResolveURL(CONST_STRPTR base, CONST_STRPTR href, STRPTR out, ULONG outlen)
{
    return wl_url_resolve((const char *)base, (const char *)href, (char *)out, outlen) ? TRUE : FALSE;
}

BOOL WL_Open(void)
{
    return wl_openlayout_open() ? TRUE : FALSE;
}

void WL_Close(void)
{
    wl_dt_close();
    wl_openlayout_close();
}
