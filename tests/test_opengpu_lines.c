/*
 * WebKit patch 0080's drawing: GraphicsContextOpenGPU's drawRect,
 * drawLine (solid, dotted, dashed, horizontal and vertical), drawEllipse and
 * drawLinesForText as the writer calls they make (OpenGPUPaintAmiga.c, on
 * gfopenrtg's CPU core), against cairo making WebKit's own calls for
 * them (CairoOperations.cpp's drawRect, drawLine, drawEllipse and
 * drawLinesForText, ported here line for line). The geometry each side
 * computes is the same code; the pixels are the writer's against cairo's:
 * whole-pixel rectangles and aliased lines the same, anti-aliased edges
 * within a bound.
 *   test_opengpu_lines          (needs cairo's headers and library)
 * MIT, Copyright (c) 2026 AmiCradle Team.
 * SPDX-License-Identifier: MIT
 */
#include <cairo.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ogpu_core.h"
#include <gfopengpu/stream.h>
#include "OpenGPUPaintAmiga.h"

#define ARENA       (1L << 22)
#define W           96
#define H           64
#define BPR         (W * 4)
#define TARGET      0x1000UL
#define PICTURE     0x20000UL
#define MASKS       0x100000UL      /* the masks' memory, a bump allocator */
#define SCRATCH     0x300000UL
#define WORK        0x340000UL
#define WORK_BYTES  ((W + 4) * 32 * 8)

static unsigned char arena[ARENA];
static unsigned char streamBuffer[2048 * 4];
static struct ogpu_core core;
static struct obgpu G;
static struct obgpu_counts counts;
static unsigned long maskTop;
static int masksLive, failures, checks;
static cairo_surface_t *cs;
static cairo_t *cr;

static ogpu_u8 *map(void *user, ogpu_u32 address, ogpu_u32 length)
{
    (void)user;
    if (address >= ARENA || length > (ogpu_u32)ARENA - address)
        return 0;
    return arena + address;
}

static long submit(void *user, const unsigned char *stream, long words)
{
    (void)user;
    ogpu_core_run(&core, stream, words);
    return core.last_error;
}

static unsigned char *maskAlloc(void *user, unsigned long bytes, unsigned long *address)
{
    (void)user;
    if (maskTop + bytes > SCRATCH)
        return 0;
    *address = maskTop;
    maskTop += (bytes + 15) & ~15UL;
    masksLive++;
    return arena + *address;
}

static void maskFree(void *user, unsigned char *block)
{
    (void)user;
    (void)block;
    masksLive--;
}

static void check(int ok, const char *what)
{
    checks++;
    if (!ok) {
        printf("FAIL %s\n", what);
        failures++;
    }
}

static long fx(double v) { return lround(v * 256); }

/* A background with every pixel different, premultiplied, the same on both sides. */
static unsigned long background(int x, int y)
{
    unsigned a = (x * 7 + y * 13) % 3 ? 255 : (unsigned)(128 + (x * 5 + y) % 120);
    unsigned r = (unsigned)(x * 255 / W), g = (unsigned)(y * 255 / H), b = (unsigned)((x + y) * 3 % 256);
    r = r * a / 255; g = g * a / 255; b = b * a / 255;
    return (unsigned long)a << 24 | (unsigned long)r << 16 | (unsigned long)g << 8 | b;
}

static void begin(int caps)
{
    int x, y;
    unsigned char *t = arena + TARGET;
    unsigned int *c;
    memset(&core, 0, sizeof core);
    ogpu_core_init(&core);
    core.map = map;
    obgpu_init(&G, streamBuffer, (long)sizeof streamBuffer / 4, arena + SCRATCH, SCRATCH, 8192, submit, 0, caps, &counts);
    obgpu_target(&G, TARGET, BPR, W, H);
    obgpu_work(&G, WORK, WORK_BYTES);
    obgpu_masks(&G, maskAlloc, maskFree, 0);
    maskTop = MASKS;
    masksLive = 0;
    if (cr)
        cairo_destroy(cr);
    if (!cs)
        cs = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, W, H);
    cairo_surface_flush(cs);
    c = (unsigned int *)cairo_image_surface_get_data(cs);
    for (y = 0; y < H; y++)
        for (x = 0; x < W; x++) {
            unsigned long v = background(x, y);
            unsigned char *p = t + y * BPR + x * 4;
            p[0] = (unsigned char)(v >> 24); p[1] = (unsigned char)(v >> 16); p[2] = (unsigned char)(v >> 8); p[3] = (unsigned char)v;
            c[y * (cairo_image_surface_get_stride(cs) / 4) + x] = (unsigned int)v;
        }
    cairo_surface_mark_dirty(cs);
    cr = cairo_create(cs);
}

/* Both pictures compared: pixels that differ, the largest channel
 * difference, and whether a differing pixel lies away from every edge (the
 * edge set: pixels with a 3x3 neighbourhood that isn't one colour in
 * cairo's picture). */
/* compare() leaves out a disc (cairo's conic mesh at its centre, where
 * WebKit moves each quadrant's sectors by a tenth of a pixel) */
static double exX, exY, exR;
static void compare(const char *what, int exact, int bound)
{
    const unsigned int *c;
    int x, y, n = 0, inner = 0, worst = 0, stride;
    char msg[200];
    obgpu_flush(&G);
    cairo_surface_flush(cs);
    c = (const unsigned int *)cairo_image_surface_get_data(cs);
    stride = cairo_image_surface_get_stride(cs) / 4;
    for (y = 0; y < H; y++)
        for (x = 0; x < W; x++) {
            const unsigned char *p = arena + TARGET + y * BPR + x * 4;
            unsigned int v = c[y * stride + x];
            int d = 0, k, dx, dy, flat = 1;
            int cv[4] = { (int)(v >> 24), (int)(v >> 16) & 255, (int)(v >> 8) & 255, (int)v & 255 };
            for (k = 0; k < 4; k++)
                if (abs(cv[k] - p[k]) > d)
                    d = abs(cv[k] - p[k]);
            if (!d || (exR > 0 && (x + 0.5 - exX) * (x + 0.5 - exX) + (y + 0.5 - exY) * (y + 0.5 - exY) < exR * exR))
                continue;
            if (getenv("CLIP_DEBUG") && d > 8)
                printf("    %d,%d cairo %08x here %02x%02x%02x%02x\n", x, y, v, p[0], p[1], p[2], p[3]);
            n++;
            if (d > worst)
                worst = d;
            for (dy = -1; dy <= 1; dy++)
                for (dx = -1; dx <= 1; dx++) {
                    int xx = x + dx, yy = y + dy;
                    if (xx >= 0 && yy >= 0 && xx < W && yy < H && c[yy * stride + xx] != v)
                        flat = 0;
                }
            if (flat)
                inner++;
        }
    printf("  %-58s %4d pixels differ, largest %3d, %d away from edges\n", what, n, worst, inner);
    snprintf(msg, sizeof msg, "%s: %s", what, exact ? "the same pixels as cairo" : "the same as cairo away from edges, edges within the bound");
    check(exact ? n == 0 : inner == 0 && worst <= bound, msg);
}

/* ---- the calls, on both ------------------------------------------------------------------ */

static void bothMatrix(double a, double b, double c, double d, double e, double f)
{
    long m[6] = { lround(a * 65536), lround(b * 65536), lround(c * 65536), lround(d * 65536), fx(e), fx(f) };
    cairo_matrix_t cm;
    cairo_matrix_init(&cm, a, b, c, d, e, f);
    obgpu_set_matrix(&G, m);
    cairo_set_matrix(cr, &cm);
}

/* A path built on both: a polygon, or a circle of four cubics. */

/* premultiplied ARGB32 as cairo stores a colour (16-bit channels, top 8 bits) */
static unsigned long pm(double r, double g, double b, double a)
{
#define CH(v) ((unsigned long)((v) * 65535.0 + 0.5) >> 8)
    return CH(a) << 24 | CH(r * a) << 16 | CH(g * a) << 8 | CH(b * a);
#undef CH
}

/* Cairo's fillRectWithColor, on both */
static void bothPlainFill(double x, double y, double w, double h, const double *c, int antialias)
{
    if (c[3] > 0) {
        obgpu_set_antialias(&G, antialias);
        check(obgpu_fill(&G, fx(x), fx(y), fx(w), fx(h), pm(c[0], c[1], c[2], c[3]), OBGPU_OP_SOURCE_OVER), "a plain fill is drawn here");
        cairo_set_source_rgba(cr, c[0], c[1], c[2], c[3]);
        cairo_rectangle(cr, x, y, w, h);
        cairo_fill(cr);
    }
}

static struct obgpu_stroke strokeOf(double width, const long *dash, int dashes, double offset)
{
    struct obgpu_stroke s;
    memset(&s, 0, sizeof s);
    s.width = lround(width * 65536);
    s.cap = OBGPU_CAP_BUTT;
    s.join = OBGPU_JOIN_MITER;
    s.miter = 10 << 16;
    s.dashes = dashes;
    s.dash = dash;
    s.dash_offset = lround(offset * 65536);
    return s;
}

/* Cairo::drawRect: the fill, then a 1-pixel stroke half a pixel inside */
static void bothDrawRect(double x, double y, double w, double h, const double *fill, const double *stroke)
{
    struct obgpu_stroke s = strokeOf(1, 0, 0, 0);
    cairo_save(cr);
    bothPlainFill(x, y, w, h, fill, 1);
    check(obgpu_path_begin(&G) && obgpu_path_rect(&G, fx(x + 0.5), fx(y + 0.5), fx(w - 1), fx(h - 1)), "drawRect's stroke path");
    obgpu_set_antialias(&G, 1);
    check(obgpu_stroke_path(&G, pm(stroke[0], stroke[1], stroke[2], stroke[3]), 0, &s, OBGPU_OP_SOURCE_OVER), "drawRect's stroke is drawn here");
    cairo_set_source_rgba(cr, stroke[0], stroke[1], stroke[2], stroke[3]);
    cairo_rectangle(cr, x + 0.5, y + 0.5, w - 1, h - 1);
    cairo_set_line_width(cr, 1);
    cairo_stroke(cr);
    cairo_restore(cr);
}

/* Cairo::drawLine, both sides from the same geometry: style 0 solid, 1 dotted, 2 dashed */
static void bothDrawLine(double x1, double y1, double x2, double y2, int style, double thickness, const double *c)
{
    int vertical = x1 + thickness == x2;
    double strokeWidth = vertical ? y2 - y1 : x2 - x1, corner = 0, width = thickness, dash[2] = { 0, 0 }, offset = 0;
    long dl[2];
    struct obgpu_stroke s;
    if (!thickness || !strokeWidth)
        return;
    cairo_save(cr);
    cairo_set_line_width(cr, thickness);    /* the context's stroke thickness */
    if (style) {
        double pw;
        corner = style == 1 ? thickness : fmin(2 * thickness, fmax(thickness, strokeWidth / 3));
        if (vertical) {
            bothPlainFill(x1, y1, thickness, corner, c, 1);
            bothPlainFill(x1, y2 - corner, thickness, corner, c, 1);
        } else {
            bothPlainFill(x1, y1, corner, thickness, c, 1);
            bothPlainFill(x2 - corner, y1, corner, thickness, c, 1);
        }
        strokeWidth -= 2 * corner;
        pw = style == 1 ? thickness : fmin(3 * thickness, fmax(thickness, strokeWidth / 3));
        if (strokeWidth <= pw + 1) {
            cairo_restore(cr);
            return;
        }
        {
            int n = (int)floor(strokeWidth / pw), odd = n % 2;
            double rem = strokeWidth - n * pw;
            offset = pw;
            if (odd && rem)
                offset -= rem / 2;
            else if (!odd)
                offset += rem ? offset - (pw + rem) / 2 : pw / 2;
        }
        dash[0] = dash[1] = pw;
        cairo_set_dash(cr, dash, 2, offset);
    } else {
        cairo_set_source_rgba(cr, c[0], c[1], c[2], c[3]);
        if (thickness < 1)
            width = 1;
        cairo_set_line_width(cr, width);
    }
    if (vertical) {
        double k = (x2 - x1) / 2;
        x1 += k; y1 += corner; x2 -= k; y2 -= corner;
    } else {
        double k = (y2 - y1) / 2;
        x1 += corner; y1 += k; x2 -= corner; y2 -= k;
    }
    dl[0] = dl[1] = lround(dash[0] * 65536);
    s = strokeOf(width, dl, style ? 2 : 0, offset);
    obgpu_set_antialias(&G, 0);
    check(obgpu_path_begin(&G) && obgpu_path_move(&G, fx(x1), fx(y1)) && obgpu_path_line(&G, fx(x2), fx(y2)), "drawLine's path");
    check(obgpu_stroke_path(&G, pm(c[0], c[1], c[2], c[3]), 0, &s, OBGPU_OP_SOURCE_OVER), "drawLine is drawn here");
    obgpu_set_antialias(&G, 1);
    cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);
    cairo_new_path(cr);
    cairo_move_to(cr, x1, y1);
    cairo_line_to(cr, x2, y2);
    cairo_stroke(cr);
    cairo_restore(cr);
}

/* Cairo::drawEllipse: cairo_arc of a unit circle scaled; here WebCore's four-cubic ellipse */
static void bothDrawEllipse(double x, double y, double w, double h, const double *fill, const double *stroke, double thickness)
{
    double rx = w / 2, ry = h / 2, cx = x + rx, cy = y + ry, kx = 0.5522847498 * rx, ky = 0.5522847498 * ry;
    struct obgpu_stroke s = strokeOf(thickness, 0, 0, 0);
    int pass;
    obgpu_set_antialias(&G, 1);
    for (pass = 0; pass < 2; pass++) {
        if (!pass && fill[3] <= 0)
            continue;
        check(obgpu_path_begin(&G), "an ellipse path");
        obgpu_path_move(&G, fx(cx + rx), fx(cy));
        obgpu_path_cubic(&G, fx(cx + rx), fx(cy + ky), fx(cx + kx), fx(cy + ry), fx(cx), fx(cy + ry));
        obgpu_path_cubic(&G, fx(cx - kx), fx(cy + ry), fx(cx - rx), fx(cy + ky), fx(cx - rx), fx(cy));
        obgpu_path_cubic(&G, fx(cx - rx), fx(cy - ky), fx(cx - kx), fx(cy - ry), fx(cx), fx(cy - ry));
        obgpu_path_cubic(&G, fx(cx + kx), fx(cy - ry), fx(cx + rx), fx(cy - ky), fx(cx + rx), fx(cy));
        obgpu_path_close(&G);
        if (!pass)
            check(obgpu_fill_path(&G, pm(fill[0], fill[1], fill[2], fill[3]), 0, 0, OBGPU_OP_SOURCE_OVER), "an ellipse's fill");
        else
            check(obgpu_stroke_path(&G, pm(stroke[0], stroke[1], stroke[2], stroke[3]), 0, &s, OBGPU_OP_SOURCE_OVER), "an ellipse's stroke");
    }
    cairo_save(cr);
    cairo_translate(cr, cx, cy);
    cairo_scale(cr, rx, ry);
    cairo_arc(cr, 0, 0, 1, 0, 2 * M_PI);
    cairo_restore(cr);
    if (fill[3] > 0) {
        cairo_set_source_rgba(cr, fill[0], fill[1], fill[2], fill[3]);
        cairo_fill_preserve(cr);
    }
    cairo_set_source_rgba(cr, stroke[0], stroke[1], stroke[2], stroke[3]);
    cairo_set_line_width(cr, thickness);
    cairo_stroke(cr);
}

/* Cairo::drawLinesForText under a transform m (a, d, e, f): the box's origin on device pixels */
static void bothTextLines(double px, double py, double thickness, const double (*seg)[2], int n, int doubleLines, const double *c,
    double scale, double e, double f)
{
    double t = fmax(thickness, 0.5), color[4] = { c[0], c[1], c[2], c[3] }, ox, oy;
    int i, row;
    if (scale < 1)
        color[3] *= scale > 0.4 ? scale : 0.4;
    ox = (roundf((float)(px * scale + e)) - e) / scale;
    oy = (ceilf((float)(py * scale + f)) - f) / scale;
    for (row = 0; row < (doubleLines ? 2 : 1); row++)
        for (i = 0; i < n; i++)
            bothPlainFill(ox + seg[i][0], oy + row * 2 * t, seg[i][1] - seg[i][0], t, color, 1);
}

int main(void)
{
    const int all = OBGPU_CAN_PD | OBGPU_CAN_PATH | OBGPU_CAN_ALIASED;
    static const double black[4] = { 0, 0, 0, 1 }, red[4] = { 0.8, 0.1, 0.1, 1 }, faint[4] = { 0.1, 0.2, 0.9, 0.5 },
        none[4] = { 0, 0, 0, 0 }, green[4] = { 0.1, 0.6, 0.2, 1 };
    static const double segs[3][2] = { { 0, 20 }, { 26, 50 }, { 55, 70.5 } };

    printf("drawRect (borders of form controls and the like)\n");
    begin(all);
    bothDrawRect(10, 8, 40, 30, faint, black);
    compare("on whole pixels, translucent fill, black border", 1, 0);
    begin(all);
    bothDrawRect(10.5, 8.25, 40, 30.5, red, faint);
    compare("off whole pixels", 0, 40);
    begin(all);
    bothMatrix(1.5, 0, 0, 1.5, 3, 2);
    bothDrawRect(4, 4, 30, 20, none, black);
    compare("scaled 1.5, no fill", 0, 40);

    printf("drawLine (borders): solid, dotted and dashed, without anti-aliasing\n");
    begin(all);
    bothDrawLine(5, 10, 90, 10, 0, 1, black);
    bothDrawLine(5, 20, 90, 20, 0, 3, red);
    bothDrawLine(20, 25, 23, 60, 0, 3, green);
    compare("solid: 1 and 3 thick, a vertical one", 1, 0);
    begin(all);
    bothDrawLine(5, 10, 90, 10, 1, 2, black);
    bothDrawLine(5, 20, 90, 20, 1, 3, red);
    bothDrawLine(40, 25, 42, 63, 1, 2, green);
    compare("dotted: 2 and 3 thick, a vertical one", 1, 0);
    begin(all);
    bothDrawLine(5, 10, 91, 10, 2, 2, black);
    bothDrawLine(5, 20, 77, 20, 2, 3, red);
    bothDrawLine(60, 25, 63, 62, 2, 3, green);
    compare("dashed: 2 and 3 thick, a vertical one", 1, 0);
    begin(all);
    bothDrawLine(5.5, 30, 90.25, 30, 0, 0.5, faint);
    compare("half a pixel thick (drawn 1), translucent", 0, 0);

    printf("drawEllipse (list bullets)\n");
    begin(all);
    bothDrawEllipse(10, 10, 6, 6, black, black, 1);
    bothDrawEllipse(30, 10, 7, 7, none, red, 1);
    bothDrawEllipse(50, 8, 30, 20, faint, green, 2);
    compare("disc, circle, an ellipse filled and stroked", 0, 40);

    printf("drawLinesForText (underlines, line-through)\n");
    begin(all);
    bothTextLines(4.3, 20.6, 1, segs, 3, 0, black, 1, 0, 0);
    bothTextLines(4.3, 30.2, 2, segs, 3, 1, red, 1, 0, 0);
    compare("one and double underlines, origin on device pixels", 1, 0);
    begin(all);
    bothMatrix(0.75, 0, 0, 0.75, 1.5, 0.25);
    bothTextLines(4.3, 20.6, 1, segs, 3, 0, black, 0.75, 1.5, 0.25);
    compare("under a scale of 0.75 (fainter, as cairo's)", 0, 40);

    obgpu_release_masks(&G);
    check(masksLive == 0, "every mask freed");
    printf("test_opengpu_lines: %d checks, %d failed\n", checks, failures);
    return failures != 0;
}
