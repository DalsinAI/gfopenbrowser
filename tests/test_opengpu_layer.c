/*
 * WebKit patch 0081's drawing inside transparency layers: cairo pushes a
 * group (an ARGB32 surface over the clip's extents), GraphicsContextOpenGPU
 * draws into its pixels through the writer (obgpu_group_surface: the same
 * transforms and clips, moved onto the group's pixels), and cairo puts the
 * group on the window by the layer's opacity. Here the writer draws into a
 * group in gfopenrtg's CPU core's memory, cairo puts it on a copy of the
 * window, and that is compared with cairo drawing all of it itself.
 *   test_opengpu_layer          (needs cairo's headers and library)
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

static void writePathCairoOnly(double cx, double cy, double r)
{
    double k = 0.5522847498 * r;
    cairo_move_to(cr, fx(cx + r) / 256.0, fx(cy) / 256.0);
    cairo_curve_to(cr, fx(cx + r) / 256.0, fx(cy + k) / 256.0, fx(cx + k) / 256.0, fx(cy + r) / 256.0, fx(cx) / 256.0, fx(cy + r) / 256.0);
    cairo_curve_to(cr, fx(cx - k) / 256.0, fx(cy + r) / 256.0, fx(cx - r) / 256.0, fx(cy + k) / 256.0, fx(cx - r) / 256.0, fx(cy) / 256.0);
    cairo_curve_to(cr, fx(cx - r) / 256.0, fx(cy - k) / 256.0, fx(cx - k) / 256.0, fx(cy - r) / 256.0, fx(cx) / 256.0, fx(cy - r) / 256.0);
    cairo_curve_to(cr, fx(cx + k) / 256.0, fx(cy - r) / 256.0, fx(cx + r) / 256.0, fx(cy - k) / 256.0, fx(cx + r) / 256.0, fx(cy) / 256.0);
    cairo_close_path(cr);
}

#define GROUP       0x200000UL

static unsigned long pm(double r, double g, double b, double a)
{
#define CH(v) ((unsigned long)((v) * 65535.0 + 0.5) >> 8)
    return CH(a) << 24 | CH(r * a) << 16 | CH(g * a) << 8 | CH(b * a);
#undef CH
}

/* A cairo surface of arena bytes (A, R, G, B in memory), and back */
static cairo_surface_t *fromArena(unsigned long address, long bpr, int w, int h)
{
    cairo_surface_t *s = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
    unsigned int *c = (unsigned int *)cairo_image_surface_get_data(s);
    int x, y, stride = cairo_image_surface_get_stride(s) / 4;
    cairo_surface_flush(s);
    for (y = 0; y < h; y++)
        for (x = 0; x < w; x++) {
            const unsigned char *p = arena + address + y * bpr + x * 4;
            c[y * stride + x] = (unsigned int)p[0] << 24 | (unsigned int)p[1] << 16 | (unsigned int)p[2] << 8 | p[3];
        }
    cairo_surface_mark_dirty(s);
    return s;
}

static void toArena(cairo_surface_t *s, unsigned long address, long bpr)
{
    unsigned int *c = (unsigned int *)cairo_image_surface_get_data(s);
    int x, y, stride = cairo_image_surface_get_stride(s) / 4, w = cairo_image_surface_get_width(s), h = cairo_image_surface_get_height(s);
    cairo_surface_flush(s);
    for (y = 0; y < h; y++)
        for (x = 0; x < w; x++) {
            unsigned char *p = arena + address + y * bpr + x * 4;
            unsigned int v = c[y * stride + x];
            p[0] = (unsigned char)(v >> 24); p[1] = (unsigned char)(v >> 16); p[2] = (unsigned char)(v >> 8); p[3] = (unsigned char)v;
        }
}

static void shapes(int gpu, double shift)
{
    if (gpu) {
        check(obgpu_fill(&G, fx(12 + shift), fx(10), fx(40), fx(30), pm(0.9, 0.2, 0.1, 1), OBGPU_OP_SOURCE_OVER), "a fill in the layer");
        check(obgpu_path_begin(&G), "a path in the layer");
        writePath(1, 50 + shift, 34, 18);
        check(obgpu_fill_path(&G, pm(0.1, 0.3, 0.9, 0.8), 0, 0, OBGPU_OP_SOURCE_OVER), "a path fill in the layer");
        return;
    }
    cairo_set_source_rgba(cr, 0.9, 0.2, 0.1, 1);
    cairo_rectangle(cr, 12 + shift, 10, 40, 30);
    cairo_fill(cr);
    cairo_new_path(cr);
    writePathCairoOnly(50 + shift, 34, 18);
    cairo_set_source_rgba(cr, 0.1, 0.3, 0.9, 0.8);
    cairo_fill(cr);
}

/* One layer over the clip (x0, y0, w, h, whole pixels) at opacity, under a transform. */
static void layerCase(const char *what, int x0, int y0, int w, int h, double opacity, const double *m, int innerClip)
{
    cairo_surface_t *group, *window;
    cairo_t *c2;
    long gbpr = (long)w * 4;
    begin(OBGPU_CAN_PD | OBGPU_CAN_PATH | OBGPU_CAN_ALIASED);
    /* cairo, all of it */
    cairo_save(cr);
    cairo_rectangle(cr, x0, y0, w, h);
    cairo_clip(cr);
    if (m) {
        cairo_matrix_t cm;
        cairo_matrix_init(&cm, m[0], m[1], m[2], m[3], m[4], m[5]);
        cairo_set_matrix(cr, &cm);
    }
    cairo_push_group(cr);
    if (innerClip) {
        cairo_new_path(cr);
        writePathCairoOnly(40, 30, 22);
        cairo_clip(cr);
    }
    shapes(0, 0);
    cairo_pop_group_to_source(cr);
    cairo_paint_with_alpha(cr, opacity);
    cairo_restore(cr);
    /* the writer into the group, cairo putting it on the window */
    memset(arena + GROUP, 0, (size_t)gbpr * h);
    obgpu_save(&G);
    obgpu_clip(&G, fx(x0), fx(y0), fx(w), fx(h));
    if (m) {
        long lm[6] = { lround(m[0] * 65536), lround(m[1] * 65536), lround(m[2] * 65536), lround(m[3] * 65536), fx(m[4]), fx(m[5]) };
        obgpu_set_matrix(&G, lm);
    }
    obgpu_save(&G);
    check(obgpu_group_surface(&G, GROUP, gbpr, w, h, x0, y0), "the group's surface is drawn into here");
    if (innerClip) {
        check(obgpu_path_begin(&G), "a clip path in the layer");
        writePath(1, 40, 30, 22);
        check(obgpu_clip_path(&G, 0), "a clip path in the layer is a mask");
    }
    shapes(1, 0);
    obgpu_restore(&G);
    obgpu_restore(&G);
    obgpu_flush(&G);
    check(G.target == TARGET && G.ox == 0 && G.oy == 0, "the window is the target again");
    group = fromArena(GROUP, gbpr, w, h);
    window = fromArena(TARGET, BPR, W, H);
    c2 = cairo_create(window);
    cairo_rectangle(c2, x0, y0, w, h);
    cairo_clip(c2);
    cairo_set_source_surface(c2, group, x0, y0);
    cairo_paint_with_alpha(c2, opacity);
    cairo_destroy(c2);
    toArena(window, TARGET, BPR);
    cairo_surface_destroy(group);
    cairo_surface_destroy(window);
    compare(what, 0, 24);   /* anti-aliased edges: the two rasterisers */
}

int main(void)
{
    static const double scaled[6] = { 1.25, 0, 0, 1.25, -6.5, -4.25 }, turned[6] = { 0.9659, 0.2588, -0.2588, 0.9659, 8, -6 };
    printf("drawing inside transparency layers (cairo's groups)\n");
    layerCase("a layer over (10, 8) 70 x 50, opacity 0.6", 10, 8, 70, 50, 0.6, 0, 0);
    layerCase("a layer at the window's corner, opacity 1", 0, 0, 96, 64, 1, 0, 0);
    layerCase("scaled 1.25 and moved", 6, 4, 80, 56, 0.5, scaled, 0);
    layerCase("turned 15 degrees", 6, 4, 80, 56, 0.75, turned, 0);
    layerCase("a clip path inside the layer", 10, 8, 70, 50, 0.6, 0, 1);
    {   /* a clip mask from outside the layer leaves it to cairo */
        begin(OBGPU_CAN_PD | OBGPU_CAN_PATH | OBGPU_CAN_ALIASED);
        obgpu_save(&G);
        check(obgpu_path_begin(&G), "an outer clip path");
        writePath(1, 40, 30, 20);
        check(obgpu_clip_path(&G, 0), "the outer clip is a mask");
        {
            struct obgpu_glyphs wh;
            check(obgpu_glyphs_here(&G, &wh) && wh.mask && wh.mask_w > 0 && wh.mask_x >= 19 && wh.mask_x <= 20,
                "glyphs: the clip's A8 mask goes with the run");
        }
        obgpu_save(&G);
        check(!obgpu_group_surface(&G, GROUP, 4 * 50, 50, 40, 10, 10), "a group under an outer mask stays cairo's");
        check(!obgpu_drawable(&G), "nothing is drawn here in it");
        {
            struct obgpu_glyphs wh;
            check(!obgpu_glyphs_here(&G, &wh), "glyphs: none here either");
        }
        obgpu_restore(&G);
        check(obgpu_drawable(&G), "drawn here again after it");
        obgpu_restore(&G);
        cairo_new_path(cr);
    }
    {   /* nested */
        begin(OBGPU_CAN_PD | OBGPU_CAN_PATH | OBGPU_CAN_ALIASED);
        obgpu_save(&G);
        check(obgpu_group_surface(&G, GROUP, 4 * 60, 60, 40, 5, 5), "an outer group");
        {   /* where gfopenfont.library draws a glyph run now (0082) */
            struct obgpu_glyphs wh;
            long tx = 0, ty = 0;
            long m[6] = { 0x20000, 0, 0, 0x20000, 10 * 256, 7 * 256 };
            obgpu_set_matrix(&G, m);
            check(obgpu_glyphs_here(&G, &wh) && wh.target == GROUP && wh.bpr == 4 * 60 && wh.w == 60 && wh.h == 40
                && wh.cx0 == 0 && wh.cy0 == 0 && wh.cx1 == 60 && wh.cy1 == 40 && !wh.mask && wh.m[0] == 0x20000 && wh.m[3] == 0x20000,
                "glyphs: the group's surface, its clip, the transform's scale");
            check(obgpu_map(&G, 3 * 256, 2 * 256, &tx, &ty) && tx == (6 + 10 - 5) * 256 && ty == (4 + 7 - 5) * 256,
                "glyphs: a pen mapped onto the group's pixels");
        }
        obgpu_save(&G);
        check(obgpu_group_surface(&G, GROUP + 0x20000, 4 * 30, 30, 20, 20, 15), "an inner group");
        check(G.ox == 20 && G.oy == 15 && G.level[G.depth].m[4] == (10 - 20) * 256, "the inner group's origin");
        obgpu_restore(&G);
        check(G.ox == 5 && G.oy == 5 && G.target == GROUP, "back in the outer group");
        obgpu_restore(&G);
        check(G.ox == 0 && G.target == TARGET, "back on the window");
    }
    obgpu_release_masks(&G);
    check(masksLive == 0, "every mask freed");
    printf("test_opengpu_layer: %d checks, %d failed\n", checks, failures);
    return failures != 0;
}
