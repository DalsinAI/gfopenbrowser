/*
 * WebKit patch 0078's drawing (OpenGPUPaintAmiga.c: clips to paths as A8
 * masks, rectangle clips off whole pixels, paths without anti-aliasing,
 * copy and clear on paths) run on gfopenrtg's CPU core, and the same
 * calls made on cairo, which draws them today: the two pictures compared
 * pixel for pixel. Where both sides cover whole pixels (rectangles on whole
 * pixels, rectangles clipped or filled without anti-aliasing) they must be
 * the same; where cairo's and GFOpenGPU's rasterisers each anti-alias an edge
 * (curves, slanted edges) the pixels away from the edges must be the same
 * and the edge pixels within a bound, and the count is printed.
 *   test_opengpu_clip          (needs cairo's headers and library)
 * MIT, Copyright (c) 2026 AmiCradle Team.
 * SPDX-License-Identifier: MIT
 */
#include <cairo.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ogpu_core.h"
#include <opengpu/stream.h>
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
static cairo_surface_t *cs, *pic;
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
            if (!d)
                continue;
            if (getenv("CLIP_DEBUG") && n < 4)
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

static void rgba(unsigned long argb)        /* a premultiplied colour as cairo's source */
{
    double a = (argb >> 24) / 255.0;
    cairo_set_source_rgba(cr, a ? ((argb >> 16) & 255) / 255.0 / a : 0, a ? ((argb >> 8) & 255) / 255.0 / a : 0,
        a ? (argb & 255) / 255.0 / a : 0, a);
}

static cairo_operator_t cairoOp(int op)
{
    switch (op) {
    case OBGPU_OP_CLEAR: return CAIRO_OPERATOR_CLEAR;
    case OBGPU_OP_COPY: return CAIRO_OPERATOR_SOURCE;
    case OBGPU_OP_SOURCE_ATOP: return CAIRO_OPERATOR_ATOP;
    case OBGPU_OP_DESTINATION_OVER: return CAIRO_OPERATOR_DEST_OVER;
    case OBGPU_OP_DESTINATION_OUT: return CAIRO_OPERATOR_DEST_OUT;
    case OBGPU_OP_XOR: return CAIRO_OPERATOR_XOR;
    case OBGPU_OP_PLUS_LIGHTER: return CAIRO_OPERATOR_ADD;
    default: return CAIRO_OPERATOR_OVER;
    }
}

static void bothSave(void) { obgpu_save(&G); cairo_save(cr); }
static void bothRestore(void) { obgpu_restore(&G); cairo_restore(cr); }

static void bothMatrix(double a, double b, double c, double d, double e, double f)
{
    long m[6] = { lround(a * 65536), lround(b * 65536), lround(c * 65536), lround(d * 65536), fx(e), fx(f) };
    cairo_matrix_t cm;
    cairo_matrix_init(&cm, a, b, c, d, e, f);
    obgpu_set_matrix(&G, m);
    cairo_set_matrix(cr, &cm);
}

/* GraphicsContextCairo's clip(FloatRect): a rectangle, non-zero, no anti-aliasing. */
static void bothClipRect(double x, double y, double w, double h)
{
    obgpu_clip(&G, fx(x), fx(y), fx(w), fx(h));
    cairo_rectangle(cr, x, y, w, h);
    cairo_set_fill_rule(cr, CAIRO_FILL_RULE_WINDING);
    cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);
    cairo_clip(cr);
    cairo_set_antialias(cr, CAIRO_ANTIALIAS_DEFAULT);
}

/* A path built on both: a polygon, or a circle of four cubics. */
static int npts;
static double pts[16][2];
static void writePath(int circle, double cx, double cy, double r)
{
    int i;
    if (circle) {
        double k = 0.5522847498 * r;
        obgpu_path_move(&G, fx(cx + r), fx(cy));
        obgpu_path_cubic(&G, fx(cx + r), fx(cy + k), fx(cx + k), fx(cy + r), fx(cx), fx(cy + r));
        obgpu_path_cubic(&G, fx(cx - k), fx(cy + r), fx(cx - r), fx(cy + k), fx(cx - r), fx(cy));
        obgpu_path_cubic(&G, fx(cx - r), fx(cy - k), fx(cx - k), fx(cy - r), fx(cx), fx(cy - r));
        obgpu_path_cubic(&G, fx(cx + k), fx(cy - r), fx(cx + r), fx(cy - k), fx(cx + r), fx(cy));
        obgpu_path_close(&G);
        cairo_move_to(cr, fx(cx + r) / 256.0, fx(cy) / 256.0);
        cairo_curve_to(cr, fx(cx + r) / 256.0, fx(cy + k) / 256.0, fx(cx + k) / 256.0, fx(cy + r) / 256.0, fx(cx) / 256.0, fx(cy + r) / 256.0);
        cairo_curve_to(cr, fx(cx - k) / 256.0, fx(cy + r) / 256.0, fx(cx - r) / 256.0, fx(cy + k) / 256.0, fx(cx - r) / 256.0, fx(cy) / 256.0);
        cairo_curve_to(cr, fx(cx - r) / 256.0, fx(cy - k) / 256.0, fx(cx - k) / 256.0, fx(cy - r) / 256.0, fx(cx) / 256.0, fx(cy - r) / 256.0);
        cairo_curve_to(cr, fx(cx + k) / 256.0, fx(cy - r) / 256.0, fx(cx + r) / 256.0, fx(cy - k) / 256.0, fx(cx + r) / 256.0, fx(cy) / 256.0);
        cairo_close_path(cr);
        return;
    }
    for (i = 0; i < npts; i++) {
        if (i) {
            obgpu_path_line(&G, fx(pts[i][0]), fx(pts[i][1]));
            cairo_line_to(cr, fx(pts[i][0]) / 256.0, fx(pts[i][1]) / 256.0);
        } else {
            obgpu_path_move(&G, fx(pts[i][0]), fx(pts[i][1]));
            cairo_move_to(cr, fx(pts[i][0]) / 256.0, fx(pts[i][1]) / 256.0);
        }
    }
    obgpu_path_close(&G);
    cairo_close_path(cr);
}

/* GraphicsContextCairo's clipPath (anti-aliased, the rule given) or clipOut
 * of a path (the clip's extents and the path, even-odd, anti-aliased). */
static int bothClipPath(int circle, double cx, double cy, double r, int evenodd, int out)
{
    int done;
    cairo_new_path(cr);
    if (out) {
        double x1, y1, x2, y2;
        cairo_clip_extents(cr, &x1, &y1, &x2, &y2);
        cairo_rectangle(cr, x1, y1, x2 - x1, y2 - y1);
    }
    check(obgpu_path_begin(&G), "a clip path can begin");
    writePath(circle, cx, cy, r);
    done = obgpu_clip_path(&G, (evenodd || out ? OBGPU_CLIP_EVENODD : 0) | (out ? OBGPU_CLIP_OUT : 0));
    cairo_set_fill_rule(cr, evenodd || out ? CAIRO_FILL_RULE_EVEN_ODD : CAIRO_FILL_RULE_WINDING);
    cairo_clip(cr);
    cairo_set_fill_rule(cr, CAIRO_FILL_RULE_WINDING);
    return done;
}

/* GraphicsContextCairo's clipOut(FloatRect): even-odd, no anti-aliasing. */
static int bothClipOutRect(double x, double y, double w, double h)
{
    double x1, y1, x2, y2;
    int done;
    check(obgpu_path_begin(&G), "a clip-out rectangle can begin");
    obgpu_path_rect(&G, fx(x), fx(y), fx(w), fx(h));
    done = obgpu_clip_path(&G, OBGPU_CLIP_OUT | OBGPU_CLIP_EVENODD | OBGPU_CLIP_ALIASED);
    cairo_clip_extents(cr, &x1, &y1, &x2, &y2);
    cairo_rectangle(cr, x1, y1, x2 - x1, y2 - y1);
    cairo_rectangle(cr, x, y, w, h);
    cairo_set_fill_rule(cr, CAIRO_FILL_RULE_EVEN_ODD);
    cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);
    cairo_clip(cr);
    cairo_set_antialias(cr, CAIRO_ANTIALIAS_DEFAULT);
    cairo_set_fill_rule(cr, CAIRO_FILL_RULE_WINDING);
    return done;
}

static void bothFillRect(double x, double y, double w, double h, unsigned long argb, int op)
{
    char msg[80];
    int r = obgpu_fill(&G, fx(x), fx(y), fx(w), fx(h), argb, op);
    snprintf(msg, sizeof msg, "a rectangle by op %d is drawn here", op);
    check(r, msg);
    cairo_set_operator(cr, cairoOp(op));
    rgba(argb);
    cairo_rectangle(cr, x, y, w, h);
    cairo_fill(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
}

static void bothFillPath(int circle, double cx, double cy, double r, unsigned long argb, int op, int antialias)
{
    char msg[80];
    int done;
    obgpu_set_antialias(&G, antialias);
    cairo_set_antialias(cr, antialias ? CAIRO_ANTIALIAS_DEFAULT : CAIRO_ANTIALIAS_NONE);
    cairo_new_path(cr);
    check(obgpu_path_begin(&G), "a path can begin");
    writePath(circle, cx, cy, r);
    done = obgpu_fill_path(&G, argb, 0, 0, op);
    snprintf(msg, sizeof msg, "a path by op %d%s is drawn here", op, antialias ? "" : " without anti-aliasing");
    check(done, msg);
    cairo_set_operator(cr, cairoOp(op));
    rgba(argb);
    cairo_fill(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
    cairo_set_antialias(cr, CAIRO_ANTIALIAS_DEFAULT);
    obgpu_set_antialias(&G, 1);
}

static void bothImage(double dx, double dy, unsigned alpha)
{
    struct obgpu_source s;
    long dst[4] = { fx(dx), fx(dy), fx(20), fx(16) }, src[4] = { 0, 0, fx(20), fx(16) };
    s.address = PICTURE;
    s.bpr = 20 * 4;
    s.w = 20;
    s.h = 16;
    s.format = OBGPU_ARGB;
    check(obgpu_image(&G, &s, dst, src, OBGPU_OP_SOURCE_OVER, alpha, 0), "a picture through a mask is drawn here");
    cairo_set_source_surface(cr, pic, dx, dy);
    cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_NEAREST);
    if (alpha < 255)
        cairo_paint_with_alpha(cr, alpha / 255.0);
    else
        cairo_paint(cr);
}

static void makePicture(void)
{
    int x, y;
    unsigned int *c;
    pic = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 20, 16);
    c = (unsigned int *)cairo_image_surface_get_data(pic);
    for (y = 0; y < 16; y++)
        for (x = 0; x < 20; x++) {
            unsigned a = (x + y) % 5 ? 255 : 90, r = (unsigned)(x * 12) * a / 255, g = (unsigned)(y * 15) * a / 255, b = 80 * a / 255;
            unsigned long v = (unsigned long)a << 24 | (unsigned long)r << 16 | (unsigned long)g << 8 | b;
            unsigned char *p = arena + PICTURE + y * 80 + x * 4;
            c[y * (cairo_image_surface_get_stride(pic) / 4) + x] = (unsigned int)v;
            p[0] = (unsigned char)a; p[1] = (unsigned char)r; p[2] = (unsigned char)g; p[3] = (unsigned char)b;
        }
    cairo_surface_mark_dirty(pic);
}

int main(void)
{
    const int all = OBGPU_CAN_PD | OBGPU_CAN_PATH | OBGPU_CAN_ALIASED;
    int i;
    makePicture();

    printf("clips:\n");
    /* a clip to a path of whole pixels, then fills by each operator here */
    {
        static const int ops[] = { OBGPU_OP_SOURCE_OVER, OBGPU_OP_COPY, OBGPU_OP_CLEAR, OBGPU_OP_XOR, OBGPU_OP_PLUS_LIGHTER,
            OBGPU_OP_DESTINATION_OUT };
        for (i = 0; i < 6; i++) {
            char what[80];
            begin(all);
            npts = 4;
            pts[0][0] = 10; pts[0][1] = 6; pts[1][0] = 70; pts[1][1] = 6; pts[2][0] = 70; pts[2][1] = 50; pts[3][0] = 10; pts[3][1] = 50;
            check(bothClipPath(0, 0, 0, 0, 0, 0), "a clip path is clipped here");
            bothFillRect(4.5, 3.25, 80, 40, 0xC0306090UL, ops[i]);
            snprintf(what, sizeof what, "path clip on whole pixels, rectangle by op %d", ops[i]);
            compare(what, 1, 0);
        }
    }
    /* rectangle clips off whole pixels: masks of their pixels by centres */
    begin(all);
    bothClipRect(8.5, 4.25, 50.5, 30.75);
    bothFillRect(0, 0, W, H, 0xFF20A040UL, OBGPU_OP_SOURCE_OVER);
    compare("a rectangle clip off whole pixels, then a fill", 1, 0);
    check(masksLive == 1, "it made one mask");
    begin(all);
    bothSave();
    bothMatrix(2, 0, 0, 2, 3.5, 1);
    bothClipRect(4.3, 3.6, 20.2, 10.1);
    bothMatrix(1, 0, 0, 1, 0, 0);
    bothFillRect(0, 0, W, H, 0x80402010UL, OBGPU_OP_SOURCE_OVER);
    bothRestore();
    check(masksLive == 0, "the restore freed the mask");
    bothFillRect(0, 0, 30, 10, 0xFF0000FFUL, OBGPU_OP_SOURCE_OVER);
    compare("a scaled clip off whole pixels, restored", 1, 0);
    /* turned: cairo's mono rasteriser against GFOpenGPU's pixel centres */
    begin(all);
    bothMatrix(cos(0.4), sin(0.4), -sin(0.4), cos(0.4), 30, 2);
    bothClipRect(5, 5, 40, 25);
    bothMatrix(1, 0, 0, 1, 0, 0);
    bothFillRect(0, 0, W, H, 0xFFE0E000UL, OBGPU_OP_SOURCE_OVER);
    compare("a turned rectangle clip (aliased, slanted edges)", 0, 255);
    /* clip-outs */
    begin(all);
    check(bothClipOutRect(20.5, 10.5, 30.25, 20.75), "a clip-out rectangle is clipped here");
    bothFillRect(0, 0, W, H, 0xFF102080UL, OBGPU_OP_SOURCE_OVER);
    compare("clipOut of a rectangle off whole pixels", 1, 0);
    /* anti-aliased clips: circles */
    begin(all);
    check(bothClipPath(1, 40, 30, 22.3, 0, 0), "a circle clip is clipped here");
    bothFillRect(0, 0, W, H, 0xFF8040C0UL, OBGPU_OP_SOURCE_OVER);
    compare("a circle clip, then a fill", 0, 48);
    begin(all);
    bothClipRect(5, 5, 80, 50);
    check(bothClipPath(1, 40, 30, 22.3, 0, 0), "a circle clip in a rectangle clip");
    check(bothClipPath(1, 50, 30, 10.6, 0, 1), "a circle clipped out of it");
    bothFillRect(0, 0, W, H, 0xE0C04020UL, OBGPU_OP_SOURCE_OVER);
    bothImage(30.0, 20.0, 255);
    bothImage(52.0, 30.0, 128);
    compare("a circle clip, a circle clipped out, fill and pictures", 0, 48);
    check(masksLive == 1, "two clips at one level: one mask");
    begin(all);
    bothSave();
    check(bothClipPath(1, 40, 30, 22.3, 0, 0), "an outer circle clip");
    bothSave();
    check(bothClipPath(1, 55, 30, 15, 0, 0), "an inner circle clip");
    bothFillRect(0, 0, W, H, 0xFF00FF00UL, OBGPU_OP_COPY);
    bothRestore();
    bothFillRect(0, 50, W, 14, 0xFFFF0000UL, OBGPU_OP_SOURCE_OVER);
    bothRestore();
    check(masksLive == 0, "nested masks freed at their restores");
    bothFillRect(0, 0, 10, 10, 0xFF0000FFUL, OBGPU_OP_SOURCE_OVER);
    compare("nested circle clips, copy, restores", 0, 48);
    /* an empty clip path, and one outside the target */
    begin(all);
    npts = 4;
    pts[0][0] = 200; pts[0][1] = 200; pts[1][0] = 210; pts[1][1] = 200; pts[2][0] = 210; pts[2][1] = 210; pts[3][0] = 200; pts[3][1] = 210;
    check(bothClipPath(0, 0, 0, 0, 0, 0), "a clip outside the target");
    bothFillRect(0, 0, W, H, 0xFF00FF00UL, OBGPU_OP_SOURCE_OVER);
    compare("a clip outside the target: nothing drawn", 1, 0);
    check(masksLive == 0, "no mask for a clip with no area here");

    printf("copy and clear on paths:\n");
    begin(all);
    npts = 4;
    pts[0][0] = 10; pts[0][1] = 10; pts[1][0] = 40; pts[1][1] = 10; pts[2][0] = 40; pts[2][1] = 40; pts[3][0] = 10; pts[3][1] = 40;
    bothFillPath(0, 0, 0, 0, 0x80204060UL, OBGPU_OP_COPY, 1);
    pts[0][0] = 50; pts[1][0] = 80; pts[2][0] = 80; pts[3][0] = 50;
    bothFillPath(0, 0, 0, 0, 0, OBGPU_OP_CLEAR, 1);
    compare("copy and clear of rectangle paths on whole pixels", 1, 0);
    begin(all);
    bothFillPath(1, 30, 30, 20.4, 0x80204060UL, OBGPU_OP_COPY, 1);
    bothFillPath(1, 65, 30, 18.7, 0, OBGPU_OP_CLEAR, 1);
    compare("copy and clear of circles", 0, 48);
    begin(all);
    bothClipRect(15.5, 8.5, 60, 40);
    bothFillRect(0, 0, W, H, 0x00000000UL, OBGPU_OP_COPY);
    bothFillRect(20, 12, 30, 20, 0x80800000UL, OBGPU_OP_COPY);
    compare("copy through a rectangle mask (cairo's lerp)", 1, 0);

    printf("without anti-aliasing:\n");
    begin(all);
    npts = 5;
    pts[0][0] = 10.3; pts[0][1] = 5.7; pts[1][0] = 70.6; pts[1][1] = 12.2; pts[2][0] = 60.1; pts[2][1] = 55.9;
    pts[3][0] = 30.4; pts[3][1] = 40.4; pts[4][0] = 5.5; pts[4][1] = 50.5;
    bothFillPath(0, 0, 0, 0, 0xFF3060A0UL, OBGPU_OP_SOURCE_OVER, 0);
    compare("a polygon without anti-aliasing", 0, 255);
    begin(all);
    bothFillRect(0, 0, 0, 0, 0, OBGPU_OP_SOURCE_OVER);
    obgpu_set_antialias(&G, 0);
    cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);
    bothFillRect(10.5, 10.25, 40.5, 20.75, 0xFF3060A0UL, OBGPU_OP_SOURCE_OVER);
    compare("a rectangle off whole pixels without anti-aliasing", 1, 0);
    begin(OBGPU_CAN_PD | OBGPU_CAN_PATH);
    obgpu_set_antialias(&G, 0);
    check(obgpu_path_begin(&G) && obgpu_path_rect(&G, fx(10.5), fx(10.5), fx(20), fx(20)) && !obgpu_fill_path(&G, 0xFF000000UL, 0, 0, OBGPU_OP_SOURCE_OVER),
        "without OBGPU_CAN_ALIASED a path without anti-aliasing goes to cairo");
    obgpu_clip(&G, fx(4.5), fx(4.5), fx(20), fx(20));
    check(!obgpu_drawable(&G), "without OBGPU_CAN_ALIASED a clip off whole pixels is cairo's");

    obgpu_release_masks(&G);
    printf("test_opengpu_clip: %d checks, %d failures\n", checks, failures);
    return failures != 0;
}
