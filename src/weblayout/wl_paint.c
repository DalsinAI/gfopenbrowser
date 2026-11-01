/*
 * weblayout: a GFOpenLayout display list painted into a RastPort.
 * MIT, Copyright (c) 2026 AmigaChrome Team.
 */
#include "wl_paint.h"

#include <graphics/gfxmacros.h>
#include <graphics/regions.h>
#include <proto/graphics.h>
#include <proto/layers.h>
#include <string.h>

#include "wl_text.h"

static int to_px(ol_unit u)
{
    if (u >= 0) return (int)((u + OL_UNITS_PER_CSSPX / 2) / OL_UNITS_PER_CSSPX);
    return -(int)((-u + OL_UNITS_PER_CSSPX / 2) / OL_UNITS_PER_CSSPX);
}


static int measure_text(void *userdata, const char *utf8, size_t length,
                        const ol_style *style, ol_text_metrics *metrics)
{
    wl_view *view = (wl_view *)userdata;
    char *latin1;
    ULONG width, n;
    (void)style;
    if (!view || !view->rp || !view->font || !metrics) return 0;
    latin1 = wl_utf8_to_latin1(view->pool, utf8, length);
    if (!latin1) return 0;
    n = (ULONG)strlen(latin1);              /* no call inside a library call's arguments */
    width = TextLength(view->rp, (STRPTR)latin1, n);
    wl_free(latin1);
    metrics->width = OL_CSSPX((ol_unit)width);
    metrics->ascent = OL_CSSPX((ol_unit)view->font->tf_Baseline);
    metrics->descent = OL_CSSPX((ol_unit)(view->font->tf_YSize - view->font->tf_Baseline));
    return 1;
}

void wl_view_init(wl_view *view, struct RastPort *rp,
                         struct TextFont *font)
{
    memset(view, 0, sizeof(*view));
    view->rp = rp;
    view->font = font;
    view->width = view->height = 1;
}

void wl_view_set_rect(wl_view *view, WORD left, WORD top,
                             WORD width, WORD height)
{
    if (!view) return;
    view->left = left;
    view->top = top;
    view->width = width > 0 ? width : 1;
    view->height = height > 0 ? height : 1;
}

void wl_view_set_pens(wl_view *view, UWORD text_pen,
                             UWORD back_pen, UWORD link_pen, UWORD fill_pen)
{
    if (!view) return;
    view->text_pen = text_pen;
    view->back_pen = back_pen;
    view->link_pen = link_pen;
    view->fill_pen = fill_pen;
}

void wl_view_set_image_drawer(wl_view *view,
                                     wl_image_draw_fn draw,
                                     void *userdata)
{
    if (!view) return;
    view->image_draw = draw;
    view->image_userdata = userdata;
}

int wl_view_set_document(wl_view *view, ol_document *document)
{
    if (!view) return 0;
    view->document = document;
    view->scroll_px = 0;
    if (!document) return 1;
    SetFont(view->rp, view->font);
    return ol_layout(document, OL_CSSPX(view->width), measure_text, view);
}

LONG wl_view_content_px(const wl_view *view)
{
    if (!view || !view->document) return 0;
    return (LONG)to_px(ol_document_content_height(view->document));
}

LONG wl_view_max_scroll(const wl_view *view)
{
    LONG content, max;
    if (!view) return 0;
    content = wl_view_content_px(view);
    max = content - view->height;
    return max > 0 ? max : 0;
}

void wl_view_scroll_to(wl_view *view, LONG scroll_px)
{
    LONG max;
    if (!view) return;
    max = wl_view_max_scroll(view);
    if (scroll_px < 0) scroll_px = 0;
    if (scroll_px > max) scroll_px = max;
    view->scroll_px = scroll_px;
}

static int visible_y(const wl_view *view, int y, int h)
{
    int top = y - view->scroll_px;
    return top + h > 0 && top < view->height;
}

/* text and pictures inside a link carry its href (HTML Lite) */
static int link_node(const ol_node *node)
{
    const char *href = node ? ol_node_href(node) : NULL;
    return (href && *href) || (node && ol_node_role(node) == OL_ROLE_LINK);
}

static void draw_text_op(wl_view *view, const ol_display_op *op)
{
    const ol_node *node;
    const char *text;
    char *latin1;
    ULONG style = 0, n;
    UWORD pen;
    int x, y, h;

    node = ol_node_by_id(view->document, op->u.text.node_id);
    text = node ? ol_node_text(node) : NULL;
    if (!text) return;

    x = to_px(op->u.text.bounds.x);
    y = to_px(op->u.text.baseline);
    h = to_px(op->u.text.bounds.height);
    if (!visible_y(view, to_px(op->u.text.bounds.y), h)) return;

    latin1 = wl_utf8_to_latin1(view->pool, text + op->u.text.text_offset, op->u.text.text_length);
    if (!latin1) return;

    if (op->u.text.style.text_flags & OL_TEXT_BOLD) style |= FSF_BOLD;
    if (op->u.text.style.text_flags & OL_TEXT_ITALIC) style |= FSF_ITALIC;
    if (op->u.text.style.text_flags & OL_TEXT_UNDERLINE) style |= FSF_UNDERLINED;

    SetSoftStyle(view->rp, style, FSF_BOLD | FSF_ITALIC | FSF_UNDERLINED);
    pen = link_node(node) ? view->link_pen : view->text_pen;
    SetAPen(view->rp, pen);
    Move(view->rp, view->left + x, view->top + y - view->scroll_px);
    n = (ULONG)strlen(latin1);
    Text(view->rp, (STRPTR)latin1, n);
    wl_free(latin1);
}

static int in_rect(ol_rect r, ol_unit x, ol_unit y)
{
    return r.width > 0 && r.height > 0 &&
           x >= r.x && y >= r.y && x < r.x + r.width && y < r.y + r.height;
}

static void draw_fill_op(wl_view *view, const ol_display_op *op)
{
    int x = to_px(op->u.fill_rect.bounds.x);
    int y = to_px(op->u.fill_rect.bounds.y);
    int w = to_px(op->u.fill_rect.bounds.width);
    int h = to_px(op->u.fill_rect.bounds.height);
    if (w <= 0 || h <= 0 || !visible_y(view, y, h)) return;
    SetAPen(view->rp, view->fill_pen);
    RectFill(view->rp, view->left + x, view->top + y - view->scroll_px,
             view->left + x + w - 1, view->top + y - view->scroll_px + h - 1);
}

static void draw_image_op(wl_view *view, const ol_display_op *op)
{
    const ol_node *node = ol_node_by_id(view->document, op->u.image.node_id);
    const char *alt = node ? ol_node_name(node) : NULL;
    int x = to_px(op->u.image.bounds.x);
    int y = to_px(op->u.image.bounds.y);
    int w = to_px(op->u.image.bounds.width);
    int h = to_px(op->u.image.bounds.height);
    if (w <= 0 || h <= 0 || !visible_y(view, y, h)) return;

    if (view->image_draw &&
        view->image_draw(view->image_userdata, (LONG)op->u.image.image_id,
                         view->rp, (WORD)(view->left + x),
                         (WORD)(view->top + y - view->scroll_px),
                         (UWORD)w, (UWORD)h))
        return;

    SetAPen(view->rp, view->text_pen);
    Move(view->rp, view->left + x, view->top + y - view->scroll_px);
    Draw(view->rp, view->left + x + w - 1, view->top + y - view->scroll_px);
    Draw(view->rp, view->left + x + w - 1, view->top + y - view->scroll_px + h - 1);
    Draw(view->rp, view->left + x, view->top + y - view->scroll_px + h - 1);
    Draw(view->rp, view->left + x, view->top + y - view->scroll_px);

    if (alt && *alt) {
        size_t len = strlen(alt);
        char *latin1 = wl_utf8_to_latin1(view->pool, alt, len);
        if (latin1) {
            ULONG n = (ULONG)strlen(latin1);
            Move(view->rp, view->left + x + 3,
                 view->top + y - view->scroll_px + view->font->tf_Baseline + 2);
            Text(view->rp, (STRPTR)latin1, n);
            wl_free(latin1);
        }
    }
}

static void draw_rule_op(wl_view *view, const ol_display_op *op)
{
    int y1 = to_px(op->u.rule.y1);
    int y2 = to_px(op->u.rule.y2);
    int x1, x2;
    if (!visible_y(view, y1, y2 - y1 + 1)) return;
    SetAPen(view->rp, view->text_pen);
    x1 = to_px(op->u.rule.x1);
    x2 = to_px(op->u.rule.x2);
    Move(view->rp, view->left + x1, view->top + y1 - view->scroll_px);
    Draw(view->rp, view->left + x2, view->top + y2 - view->scroll_px);
}

void wl_view_draw(wl_view *view)
{
    struct Region *clip = NULL, *old = NULL;
    BOOL clipped = FALSE;
    size_t i;
    if (!view || !view->rp) return;

    SetAPen(view->rp, view->back_pen);
    RectFill(view->rp, view->left, view->top,
             view->left + view->width - 1, view->top + view->height - 1);

    if (!view->document) return;
    SetFont(view->rp, view->font);

    /* Text, pictures and boxes cut across the area's edges: clip them to
     * it, so nothing lands on the gadgets around the page. */
    if (view->rp->Layer && (clip = NewRegion())) {
        struct Rectangle r;
        r.MinX = view->left;
        r.MinY = view->top;
        r.MaxX = view->left + view->width - 1;
        r.MaxY = view->top + view->height - 1;
        if (OrRectRegion(clip, &r)) {
            old = InstallClipRegion(view->rp->Layer, clip);
            clipped = TRUE;
        }
    }

    for (i = 0; i < ol_display_count(view->document); ++i) {
        const ol_display_op *op = ol_display_get(view->document, i);
        if (!op) continue;
        switch (op->type) {
        case OL_OP_TEXT: draw_text_op(view, op); break;
        case OL_OP_FILL_RECT: draw_fill_op(view, op); break;
        case OL_OP_IMAGE: draw_image_op(view, op); break;
        case OL_OP_RULE: draw_rule_op(view, op); break;
        default: break;
        }
    }
    SetSoftStyle(view->rp, 0, FSF_BOLD | FSF_ITALIC | FSF_UNDERLINED);
    if (clipped) InstallClipRegion(view->rp->Layer, old);
    if (clip) DisposeRegion(clip);
}

const ol_node *wl_view_hit_action(const wl_view *view,
                                         WORD mouse_x, WORD mouse_y,
                                         ULONG action)
{
    ol_unit x, y;
    if (!view || !view->document) return NULL;
    if (mouse_x < view->left || mouse_y < view->top ||
        mouse_x >= view->left + view->width ||
        mouse_y >= view->top + view->height)
        return NULL;
    x = OL_CSSPX(mouse_x - view->left);
    y = OL_CSSPX(mouse_y - view->top + view->scroll_px);
    return ol_hit_action(view->document, x, y, action);
}

/* The link under the mouse, from the pieces of text and the pictures as
 * they are drawn. The nodes' own boxes will not do: an inline element
 * that wraps (a <b> after a link on the same line) has one box around
 * all of its lines, which covers the link, and is deeper in the tree. */
const char *wl_view_hit_link(const wl_view *view, WORD mouse_x, WORD mouse_y)
{
    ol_unit x, y;
    size_t i;
    const ol_node *node;
    const char *href;
    if (!view || !view->document) return NULL;
    if (mouse_x < view->left || mouse_y < view->top ||
        mouse_x >= view->left + view->width ||
        mouse_y >= view->top + view->height)
        return NULL;
    x = OL_CSSPX(mouse_x - view->left);
    y = OL_CSSPX(mouse_y - view->top + view->scroll_px);
    for (i = ol_display_count(view->document); i-- > 0;) {
        const ol_display_op *op = ol_display_get(view->document, i);
        ol_id id;
        if (!op) continue;
        if (op->type == OL_OP_TEXT && in_rect(op->u.text.bounds, x, y)) id = op->u.text.node_id;
        else if (op->type == OL_OP_IMAGE && in_rect(op->u.image.bounds, x, y)) id = op->u.image.node_id;
        else continue;
        node = ol_node_by_id(view->document, id);
        href = node ? ol_node_href(node) : NULL;
        if (href && *href) return href;
    }
    /* a link with nothing drawn in it: GFOpenLayout's own hit test */
    node = ol_hit_action(view->document, x, y, OL_ACTION_ACTIVATE);
    href = node ? ol_node_href(node) : NULL;
    return href && *href ? href : NULL;
}
