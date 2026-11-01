/*
 * weblayout's calls (weblayout.h) on the host: wl_document.c and the
 * painter built against tests/host's Exec and graphics headers, with a
 * graphics.library here that draws topaz 8 sized boxes into a byte
 * per pixel: the pages that broke on the Amiga (a link inside a
 * paragraph with bold text after it, a title kept past WL_Layout and
 * WL_Draw, https://example.com/ without </head>).
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include "weblayout.h"
#include "wl_image_dt.h"

#include <graphics/rastport.h>
#include <graphics/text.h>
#include <proto/graphics.h>
#include <proto/layers.h>

#include <stdio.h>
#include <string.h>

#define FB_W 320
#define FB_H 400

static unsigned char fb[FB_H][FB_W];
static int failed;

#define CHECK(cond) do { if (!(cond)) { printf("WLDOC FAIL line %d: %s\n", __LINE__, #cond); failed = 1; } } while (0)

/* graphics.library: topaz 8, every glyph an 8 by 8 cell */
static void plot(LONG x, LONG y, UWORD pen)
{
    if (x >= 0 && y >= 0 && x < FB_W && y < FB_H) fb[y][x] = (unsigned char)pen;
}
void SetFont(struct RastPort *rp, struct TextFont *font) { rp->Font = font; }
ULONG TextLength(struct RastPort *rp, STRPTR s, ULONG n) { (void)rp; (void)s; return n * 8; }
void Move(struct RastPort *rp, LONG x, LONG y) { rp->cp_x = (WORD)x; rp->cp_y = (WORD)y; }
void SetAPen(struct RastPort *rp, ULONG pen) { rp->FgPen = (UWORD)pen; }
void Text(struct RastPort *rp, STRPTR s, ULONG n)
{
    ULONG i;
    LONG x, y;
    for (i = 0; i < n; ++i) {
        if (s[i] == ' ') continue;
        for (y = rp->cp_y - 6; y < rp->cp_y + 2; ++y)
            for (x = rp->cp_x + (LONG)i * 8 + 1; x < rp->cp_x + (LONG)i * 8 + 7; ++x)
                plot(x, y, rp->FgPen);
    }
    rp->cp_x = (WORD)(rp->cp_x + (WORD)(n * 8));
}
void Draw(struct RastPort *rp, LONG x, LONG y) { plot(x, y, rp->FgPen); Move(rp, x, y); }
void RectFill(struct RastPort *rp, LONG x0, LONG y0, LONG x1, LONG y1)
{
    LONG x, y;
    for (y = y0; y <= y1; ++y)
        for (x = x0; x <= x1; ++x) plot(x, y, rp->FgPen);
}
ULONG SetSoftStyle(struct RastPort *rp, ULONG style, ULONG enable) { (void)rp; (void)enable; return style; }
struct Region *NewRegion(void) { return NULL; }
void DisposeRegion(struct Region *r) { (void)r; }
BOOL OrRectRegion(struct Region *r, const struct Rectangle *rect) { (void)r; (void)rect; return FALSE; }
struct Region *InstallClipRegion(struct Layer *layer, struct Region *region) { (void)layer; return region; }

/* picture.datatype: none on the host */
int wl_dt_open(void) { return 0; }
void wl_dt_close(void) { }
int wl_dt_image_load(wl_dt_image *image, const char *path, struct Screen *screen,
                     UWORD width, UWORD height)
{
    (void)image; (void)path; (void)screen; (void)width; (void)height;
    return 0;
}
void wl_dt_image_free(wl_dt_image *image) { (void)image; }
int wl_dt_image_draw(const wl_dt_image *image, struct RastPort *rp,
                     WORD x, WORD y, UWORD width, UWORD height)
{
    (void)image; (void)rp; (void)x; (void)y; (void)width; (void)height;
    return 0;
}

/* weblayout.library's test page (WebLayoutLibTest) and sheet */
static const char page[] =
    "<html><head><title>weblayout &amp; the library</title>"
    "<link rel=\"stylesheet\" href=\"style.css\"></head><body>"
    "<h1>Heading</h1>"
    "<p>A paragraph with <a href=\"../next.html\">a link</a> in it, and "
    "<b>bold</b> text: caf\xc3\xa9 \xe2\x80\x9cquoted\xe2\x80\x9d.</p>"
    "<p><img src=\"/pics/one.png\" width=\"32\" height=\"16\" alt=\"one\"></p>"
    "<ul><li>first</li><li>second</li></ul>"
    "</body></html>";
static const char sheet[] = "h1 { color: #800000; } p { padding-left: 4px; }";

/* https://example.com/ as it is served: no </head>, <p>s left open */
static const char example_com[] =
    "<!doctype html><html lang=\"en\"><head><title>Example Domain</title>"
    "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">"
    "<style>body{background:#eee;width:60vw;margin:15vh auto;font-family:system-ui,sans-serif}"
    "h1{font-size:1.5em}div{opacity:0.8}a:link,a:visited{color:#348}</style>"
    "<body><div><h1>Example Domain</h1><p>This domain is for use in documentation "
    "examples without needing permission. Avoid use in operations.<p>"
    "<a href=\"https://iana.org/domains/example\">Learn more</a></div></body></html>\n";

/* the same, written out in full */
static const char example_com_closed[] =
    "<!doctype html>\n<html lang=\"en\">\n<head>\n<title>Example Domain</title>\n"
    "<meta charset=\"utf-8\">\n"
    "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\n"
    "<style>body{background:#eee;width:60vw;margin:15vh auto;font-family:system-ui,sans-serif}"
    "h1{font-size:1.5em}div{opacity:0.8}a:link,a:visited{color:#348}</style>\n</head>\n"
    "<body><div><h1>Example Domain</h1><p>This domain is for use in documentation "
    "examples without needing permission. Avoid use in operations.</p>"
    "<p><a href=\"https://iana.org/domains/example\">Learn more</a></p></div></body>\n</html>\n";

static struct TextFont topaz = { 8, 8, 6 };
static struct RastPort rp;

static void init_view(struct WLView *view)
{
    memset(view, 0, sizeof *view);
    view->wv_Size = sizeof *view;
    view->wv_RPort = &rp;
    view->wv_Font = &topaz;
    view->wv_Left = 4;
    view->wv_Top = 4;
    view->wv_Width = 300;
    view->wv_Height = 380;
    view->wv_TextPen = 1;
    view->wv_BackPen = 0;
    view->wv_LinkPen = 3;
    view->wv_FillPen = 2;
}

static unsigned long inked(UWORD pen)
{
    unsigned long n = 0;
    int x, y;
    for (y = 0; y < FB_H; ++y)
        for (x = 0; x < FB_W; ++x)
            if (pen ? fb[y][x] == pen : fb[y][x] != 0) ++n;
    return n;
}

/* the first link found scanning the view as WebLayoutLibTest does, and how many differ */
static CONST_STRPTR scan_links(WLDocument *doc, const struct WLView *view, int *distinct)
{
    static char first[256];
    CONST_STRPTR href, last = NULL;
    char seen[256] = "";
    WORD x, y;
    first[0] = 0;
    *distinct = 0;
    for (y = view->wv_Top; y < view->wv_Top + view->wv_Height; y += 2)
        for (x = view->wv_Left; x < view->wv_Left + view->wv_Width; x += 2) {
            href = WL_HitLink(doc, view, x, y);
            if (!href) continue;
            if (!first[0]) snprintf(first, sizeof first, "%s", (const char *)href);
            if (strcmp(seen, (const char *)href)) {
                snprintf(seen, sizeof seen, "%s", (const char *)href);
                ++*distinct;
            }
            last = href;
        }
    return last ? (CONST_STRPTR)first : NULL;
}

static void library_page(void)
{
    struct WLView view;
    struct WLResource res;
    WLDocument *doc;
    CONST_STRPTR title, href;
    char title_copy[64];
    int distinct;

    init_view(&view);
    memset(fb, 0, sizeof fb);
    doc = WL_NewDocument((CONST_STRPTR)page, (ULONG)strlen(page), WLKIND_HTML,
                         (CONST_STRPTR)"https://example.com/dir/page.html");
    CHECK(doc != NULL);
    if (!doc) return;
    title = WL_GetTitle(doc);
    CHECK(!strcmp((const char *)title, "weblayout & the library"));
    snprintf(title_copy, sizeof title_copy, "%s", (const char *)title);
    CHECK(WL_CountResources(doc, WLRES_STYLESHEET) == 1);
    res.wr_Size = sizeof res;
    CHECK(WL_GetResource(doc, WLRES_STYLESHEET, 0, &res));
    CHECK(res.wr_URL && !strcmp((const char *)res.wr_URL, "https://example.com/dir/style.css"));
    CHECK(WL_AddStyleSheet(doc, (CONST_STRPTR)sheet, (ULONG)strlen(sheet)));

    CHECK(WL_Layout(doc, &view));
    CHECK(WL_ContentHeight(doc) > 40);
    CHECK(WL_CountResources(doc, WLRES_IMAGE) == 1);
    WL_Draw(doc, &view);
    CHECK(inked(0) > 50);
    CHECK(inked(view.wv_LinkPen) > 0);                 /* the link is drawn as one */

    /* the title's pointer lasts until the document is disposed of: the
     * stylesheet made the page again in WL_Layout */
    CHECK(WL_GetTitle(doc) == title);
    CHECK(!strcmp((const char *)title, title_copy));
    /* and still so once the page is made again for another sheet */
    CHECK(WL_AddStyleSheet(doc, (CONST_STRPTR)"p { color: #123456; }", 21));
    CHECK(WL_Layout(doc, &view));
    WL_Draw(doc, &view);
    CHECK(!strcmp((const char *)title, title_copy));

    href = scan_links(doc, &view, &distinct);
    CHECK(href && !strcmp((const char *)href, "https://example.com/next.html"));
    CHECK(distinct == 1);
    CHECK(WL_HitLink(doc, &view, 0, 0) == NULL);         /* outside the page's area */
    printf("WLDOC library page: \"%s\", %ld px high, link %s\n", (const char *)title,
           (long)WL_ContentHeight(doc), href ? (const char *)href : "(none)");
    WL_DisposeDocument(doc);
}

static void example_page(const char *name, const char *html)
{
    struct WLView view;
    WLDocument *doc;
    CONST_STRPTR href;
    int distinct;
    LONG height;

    init_view(&view);
    memset(fb, 0, sizeof fb);
    doc = WL_NewDocument((CONST_STRPTR)html, (ULONG)strlen(html), WLKIND_HTML,
                         (CONST_STRPTR)"https://example.com/");
    CHECK(doc != NULL);
    if (!doc) return;
    CHECK(!strcmp((const char *)WL_GetTitle(doc), "Example Domain"));
    CHECK(WL_CountResources(doc, WLRES_STYLESHEET) == 0);
    CHECK(WL_Layout(doc, &view));
    height = WL_ContentHeight(doc);
    WL_Draw(doc, &view);
    href = scan_links(doc, &view, &distinct);
    CHECK(href && !strcmp((const char *)href, "https://iana.org/domains/example"));
    CHECK(distinct == 1);
    CHECK(inked(view.wv_TextPen) > 1000);              /* the heading and the paragraph */
    CHECK(inked(view.wv_LinkPen) > 100);               /* "Learn more" */
    CHECK(height > 60);
    printf("WLDOC %s: %ld px high, %lu text pixels, link %s\n", name, (long)height,
           inked(view.wv_TextPen), href ? (const char *)href : "(none)");
    WL_DisposeDocument(doc);
}

int main(void)
{
    CHECK(WL_Open());
    library_page();
    example_page("example.com", example_com);
    example_page("example.com (closed)", example_com_closed);
    WL_Close();
    printf("WLDOC %s\n", failed ? "FAIL" : "PASS");
    return failed ? 1 : 0;
}
