/*
 * WebKit patch 0079's drawing (OpenGPUPaintAmiga.c: pictures as paint for
 * drawPattern and pattern brushes, conic gradients, stops of different
 * alphas) on openamigartg's CPU core, against cairo drawing the same calls
 * as WebKit's cairo port does (CairoUtilities' drawPatternToCairoContext,
 * GradientCairo's conic mesh, pixman's gradients):
 *  - patterns: the same pixels away from the shape's edges, which each
 *    rasteriser anti-aliases (bilinear sampling within a bound);
 *  - conic gradients: cairo draws WebKit's mesh of Bezier sectors, GFOpenGPU
 *    the angle itself, so within a bound;
 *  - stops of different alphas: GFOpenGPU mixes them premultiplied (CSS),
 *    pixman unpremultiplied: checked against the premultiplied mix worked
 *    out here, and the difference from cairo printed; stops of one alpha
 *    against cairo.
 *   test_opengpu_pattern          (needs cairo's headers and library)
 * MIT, Copyright (c) 2026 AmigaChrome Team.
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

/* ---- patterns ----------------------------------------------------------------------------- */

/* drawPatternToCairoContext's cairo side: the tile (a whole-pixel part of
 * the picture copied out, as cairo does when it isn't the whole picture)
 * repeated under tileToUser, filled over the rectangle. */
static void cairoPattern(int tx, int ty, int tw, int th, const double *m, int bilinear, double x, double y, double w, double h,
    int op)
{
    cairo_surface_t *tile = pic;
    cairo_pattern_t *p;
    cairo_matrix_t pm;
    if (tx || ty || tw != 20 || th != 16) {
        cairo_t *c2;
        tile = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, tw, th);
        c2 = cairo_create(tile);
        cairo_set_source_surface(c2, pic, -tx, -ty);
        cairo_paint(c2);
        cairo_destroy(c2);
    }
    p = cairo_pattern_create_for_surface(tile);
    cairo_pattern_set_filter(p, bilinear ? CAIRO_FILTER_BILINEAR : CAIRO_FILTER_FAST);
    cairo_pattern_set_extend(p, CAIRO_EXTEND_REPEAT);
    cairo_matrix_init(&pm, m[0], m[1], m[2], m[3], m[4], m[5]);
    cairo_matrix_invert(&pm);
    cairo_pattern_set_matrix(p, &pm);
    cairo_set_operator(cr, cairoOp(op));
    cairo_set_source(cr, p);
    cairo_rectangle(cr, x, y, w, h);
    cairo_fill(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
    cairo_pattern_destroy(p);
    if (tile != pic)
        cairo_surface_destroy(tile);
}

static void ourPattern(struct obgpu_pattern *p, int tx, int ty, int tw, int th, const double *m, int bilinear)
{
    int i;
    memset(p, 0, sizeof *p);
    p->source.address = PICTURE;
    p->source.bpr = 80;
    p->source.w = 20;
    p->source.h = 16;
    p->source.format = OBGPU_ARGB;
    p->x = tx; p->y = ty; p->w = tw; p->h = th;
    for (i = 0; i < 4; i++)
        p->m[i] = lround(m[i] * 65536);
    p->m[4] = fx(m[4]);
    p->m[5] = fx(m[5]);
    p->edge = OBGPU_PATTERN_REPEAT;
    p->bilinear = bilinear;
    p->alpha = 255;
}

static void bothPatternRect(int tx, int ty, int tw, int th, const double *m, int bilinear, double x, double y, double w, double h,
    int op)
{
    struct obgpu_pattern p;
    ourPattern(&p, tx, ty, tw, th, m, bilinear);
    check(obgpu_path_begin(&G) && obgpu_path_rect(&G, fx(x), fx(y), fx(w), fx(h)), "a pattern's rectangle");
    check(obgpu_fill_path_pattern(&G, &p, 0, op), "a pattern fill is drawn here");
    cairoPattern(tx, ty, tw, th, m, bilinear, x, y, w, h, op);
}

/* ---- gradients ---------------------------------------------------------------------------- */

static unsigned long stopsBuf[64];

/* both: a rectangle filled with a gradient given unpremultiplied stops (offset, r, g, b, a) */
static void bothGradientRect(int kind, const double *geo, int n, const double (*st)[5], double x, double y, double w, double h,
    int cairoToo)
{
    struct obgpu_gradient gr;
    cairo_pattern_t *p = 0;
    int i;
    memset(&gr, 0, sizeof gr);
    gr.kind = kind;
    gr.spread = OBGPU_PAD;
    gr.m[0] = gr.m[3] = 0x10000;
    for (i = 0; i < 6; i++)
        gr.geometry[i] = lround(geo[i] * 65536);
    for (i = 0; i < n; i++) {
        double a = st[i][4];
        stopsBuf[2 * i] = (unsigned long)lround(st[i][0] * 65536);
        stopsBuf[2 * i + 1] = (unsigned long)lround(a * 255) << 24 | (unsigned long)lround(st[i][1] * a * 255) << 16
            | (unsigned long)lround(st[i][2] * a * 255) << 8 | (unsigned long)lround(st[i][3] * a * 255);
    }
    gr.stops = n;
    gr.stop = stopsBuf;
    check(obgpu_path_begin(&G) && obgpu_path_rect(&G, fx(x), fx(y), fx(w), fx(h)), "a gradient's rectangle");
    check(obgpu_fill_path(&G, 0, &gr, 0, OBGPU_OP_SOURCE_OVER), "a gradient fill is drawn here");
    if (!cairoToo)
        return;
    if (kind == OBGPU_LINEAR) {
        p = cairo_pattern_create_linear(geo[0], geo[1], geo[2], geo[3]);
        for (i = 0; i < n; i++)
            cairo_pattern_add_color_stop_rgba(p, st[i][0], st[i][1], st[i][2], st[i][3], st[i][4]);
    }
    if (p) {
        cairo_set_source(cr, p);
        cairo_rectangle(cr, x, y, w, h);
        cairo_fill(cr);
        cairo_pattern_destroy(p);
    }
}

/* WebKit's GradientCairo createConic and addConicSector, for cairo's side */
static void conicSector(cairo_pattern_t *g, double cx, double cy, double r, double angle, const double *from, const double *to)
{
    double a0 = (from[0] - 0.25) * 2 * M_PI + angle, a1 = (to[0] - 0.25) * 2 * M_PI + angle, f, n0;
    double ox = 0, oy = 0;
    n0 = fmod(a0, 2 * M_PI);
    if (n0 < 0)
        n0 += 2 * M_PI;
    if (n0 >= M_PI / 2 && n0 < M_PI) ox = -1;
    else if (n0 >= M_PI && n0 < 3 * M_PI / 2) { ox = -1; oy = -1; }
    else if (n0 >= 3 * M_PI / 2) oy = -1;
    cx += ox * 0.1;
    cy += oy * 0.1;
    f = 4 * tan((a1 - a0) / 4) / 3;
    cairo_mesh_pattern_begin_patch(g);
    cairo_mesh_pattern_move_to(g, cx, cy);
    cairo_mesh_pattern_line_to(g, cx + r * cos(a0), cy + r * sin(a0));
    cairo_mesh_pattern_curve_to(g, cx + r * cos(a0) - f * r * sin(a0), cy + r * sin(a0) + f * r * cos(a0),
        cx + r * cos(a1) + f * r * sin(a1), cy + r * sin(a1) - f * r * cos(a1), cx + r * cos(a1), cy + r * sin(a1));
    cairo_mesh_pattern_set_corner_color_rgba(g, 0, from[1], from[2], from[3], from[4]);
    cairo_mesh_pattern_set_corner_color_rgba(g, 1, from[1], from[2], from[3], from[4]);
    cairo_mesh_pattern_set_corner_color_rgba(g, 2, to[1], to[2], to[3], to[4]);
    cairo_mesh_pattern_set_corner_color_rgba(g, 3, to[1], to[2], to[3], to[4]);
    cairo_mesh_pattern_end_patch(g);
}

/* a conic gradient (WebCore's angle: from the top) on both; stops at 0 and 1 included, as createConic makes them */
static void bothConic(double cx, double cy, double angle, int n, const double (*st)[5], double x, double y, double w, double h)
{
    double geo[6] = { cx, cy, 0, 0, 0, 0 }, turns = angle / (2 * M_PI) - 0.25;
    cairo_pattern_t *p = cairo_pattern_create_mesh();
    int i;
    geo[2] = turns - floor(turns);
    bothGradientRect(OBGPU_CONIC, geo, n, st, x, y, w, h, 0);
    for (i = 0; i + 1 < n; i++)
        conicSector(p, cx, cy, 4096, angle, st[i], st[i + 1]);
    cairo_set_source(cr, p);
    cairo_rectangle(cr, x, y, w, h);
    cairo_fill(cr);
    cairo_pattern_destroy(p);
}

/* the premultiplied mix of a linear gradient along x over the background, worked out here */
static void premultipliedCheck(const char *what, double x0, double x1, int n, const double (*st)[5], int y0, int y1, int bound)
{
    int x, y, worst = 0, k;
    char msg[160];
    obgpu_flush(&G);
    for (y = y0; y < y1; y++)
        for (x = (int)x0; x < (int)x1; x++) {
            double t = (x + 0.5 - x0) / (x1 - x0), c[4] = { 0, 0, 0, 0 }, inv;
            int i = 0;
            unsigned long bg = background(x, y);
            const unsigned char *p = arena + TARGET + y * BPR + x * 4;
            while (i + 1 < n - 1 && t > st[i + 1][0])
                i++;
            {
                double u = (t - st[i][0]) / (st[i + 1][0] - st[i][0]);
                if (u < 0) u = 0;
                if (u > 1) u = 1;
                for (k = 0; k < 3; k++)
                    c[k + 1] = (1 - u) * st[i][k + 1] * st[i][4] + u * st[i + 1][k + 1] * st[i + 1][4];
                c[0] = (1 - u) * st[i][4] + u * st[i + 1][4];
            }
            inv = 1 - c[0];
            for (k = 0; k < 4; k++) {
                double d = ((bg >> (24 - 8 * k)) & 255) / 255.0, v = (c[k] + d * inv) * 255;
                int diff = abs((int)lround(v) - p[k]);
                if (diff > worst)
                    worst = diff;
            }
        }
    printf("  %-58s largest difference %d from the premultiplied mix\n", what, worst);
    snprintf(msg, sizeof msg, "%s: the premultiplied mix (CSS), within %d", what, bound);
    check(worst <= bound, msg);
}

/* the conic gradient worked out here, as CSS defines it (the angle from
 * the start, clockwise, premultiplied mix), over the background; pixels
 * within a pixel of the centre or the start line are left out, where
 * a pixel's colour isn't one angle's */
static void conicCheck(const char *what, double cx, double cy, double angle, const double *m, int n, const double (*st)[5],
    int x0, int y0, int x1, int y1, int bound)
{
    int x, y, worst = 0, k;
    char msg[160];
    obgpu_flush(&G);
    for (y = y0; y < y1; y++)
        for (x = x0; x < x1; x++) {
            /* the pixel's centre in the user's space (m: user to target, a translation here) */
            double ux = x + 0.5 - m[4], uy = y + 0.5 - m[5], dx = ux - cx, dy = uy - cy, a, t, c[4], u, inv;
            int i = 0;
            unsigned long bg = background(x, y);
            const unsigned char *p = arena + TARGET + y * BPR + x * 4;
            if (dx * dx + dy * dy < 4)
                continue;
            a = atan2(dy, dx) - (angle - M_PI / 2);
            t = a / (2 * M_PI);
            t -= floor(t);
            if (t * 2 * M_PI * sqrt(dx * dx + dy * dy) < 1.5 || (1 - t) * 2 * M_PI * sqrt(dx * dx + dy * dy) < 1.5)
                continue;
            while (i + 1 < n - 1 && t > st[i + 1][0])
                i++;
            u = (t - st[i][0]) / (st[i + 1][0] - st[i][0]);
            u = u < 0 ? 0 : u > 1 ? 1 : u;
            for (k = 0; k < 3; k++)
                c[k + 1] = (1 - u) * st[i][k + 1] * st[i][4] + u * st[i + 1][k + 1] * st[i + 1][4];
            c[0] = (1 - u) * st[i][4] + u * st[i + 1][4];
            inv = 1 - c[0];
            for (k = 0; k < 4; k++) {
                double d = ((bg >> (24 - 8 * k)) & 255) / 255.0, v = (c[k] + d * inv) * 255;
                int diff = abs((int)lround(v) - p[k]);
                if (diff > worst)
                    worst = diff;
            }
        }
    printf("  %-58s largest difference %d from the angle's colour\n", what, worst);
    snprintf(msg, sizeof msg, "%s: each pixel the colour of its angle (CSS), within %d", what, bound);
    check(worst <= bound, msg);
}

/* stops every 1/24 turn along conicStops' colours, so cairo's Bezier
 * sectors follow the angle closely (each sector's colour goes by its
 * curve's parameter, not its angle) */
static double dense[25][5];
static void makeDense(int n, const double (*st)[5])
{
    int j, k;
    for (j = 0; j <= 24; j++) {
        double t = j / 24.0, u;
        int i = 0;
        while (i + 1 < n - 1 && t > st[i + 1][0])
            i++;
        u = (t - st[i][0]) / (st[i + 1][0] - st[i][0]);
        dense[j][0] = t;
        for (k = 1; k < 5; k++)
            dense[j][k] = (1 - u) * st[i][k] + u * st[i + 1][k];
    }
}

int main(void)
{
    const int all = OBGPU_CAN_PD | OBGPU_CAN_PATH | OBGPU_CAN_ALIASED;
    static const double one[6] = { 1, 0, 0, 1, 0, 0 }, shifted[6] = { 1, 0, 0, 1, 3.25, 1.5 },
        twice[6] = { 2, 0, 0, 2, -5.5, 2.25 }, turned[6] = { 0.8660254, 0.5, -0.5, 0.8660254, 10, -4 },
        stretched[6] = { 1.5, 0, 0, 0.75, 7, 3 };
    static const double sameAlpha[3][5] = { { 0, 1, 0, 0, 0.8 }, { 0.4, 0, 0.5, 1, 0.8 }, { 1, 1, 1, 0, 0.8 } };
    static const double mixedAlpha[3][5] = { { 0, 1, 0, 0, 1 }, { 0.5, 0, 0, 1, 0.1 }, { 1, 0, 1, 0, 0.6 } };
    static const double conicStops[4][5] = { { 0, 1, 0, 0, 1 }, { 0.3, 0, 0, 1, 1 }, { 0.7, 0, 1, 0, 1 }, { 1, 1, 0, 0, 1 } };
    makePicture();

    printf("drawPattern: a picture tiled over a rectangle\n");
    begin(all);
    bothPatternRect(0, 0, 20, 16, one, 0, 4, 3, 70, 50, OBGPU_OP_SOURCE_OVER);
    compare("whole picture, from the origin, nearest", 1, 0);
    begin(all);
    bothPatternRect(0, 0, 20, 16, shifted, 1, 4, 3, 70, 50, OBGPU_OP_SOURCE_OVER);
    compare("phase (3.25, 1.5), bilinear", 0, 3);
    begin(all);
    bothPatternRect(0, 0, 20, 16, shifted, 0, 4.5, 3.25, 70, 50, OBGPU_OP_SOURCE_OVER);
    compare("phase, nearest, rectangle off whole pixels", 0, 3);
    begin(all);
    bothPatternRect(0, 0, 20, 16, twice, 0, 2, 2, 90, 60, OBGPU_OP_SOURCE_OVER);
    compare("scaled 2x from (-5.5, 2.25), nearest", 0, 3);
    begin(all);
    bothPatternRect(3, 2, 9, 7, one, 0, 0, 0, 96, 64, OBGPU_OP_SOURCE_OVER);
    compare("a tile inside the picture (3, 2, 9 x 7)", 1, 0);
    begin(all);
    bothPatternRect(0, 0, 20, 16, twice, 1, 2, 2, 90, 60, OBGPU_OP_SOURCE_OVER);
    compare("scaled 2x, bilinear", 0, 3);
    begin(all);
    bothPatternRect(0, 0, 20, 16, stretched, 1, 2, 2, 90, 60, OBGPU_OP_SOURCE_OVER);
    compare("stretched 1.5 x 0.75, bilinear", 0, 3);
    begin(all);
    bothPatternRect(0, 0, 20, 16, turned, 1, 2, 2, 90, 60, OBGPU_OP_SOURCE_OVER);
    compare("turned 30 degrees, bilinear", 0, 3);
    begin(all);
    bothMatrix(1.25, 0, 0, 1.25, 3, 2);
    bothPatternRect(0, 0, 20, 16, shifted, 1, 0, 0, 60, 40, OBGPU_OP_SOURCE_OVER);
    compare("under a transform (1.25, moved), bilinear", 0, 3);
    begin(all);
    bothPatternRect(0, 0, 20, 16, one, 0, 10, 10, 50, 40, OBGPU_OP_COPY);
    compare("copy", 1, 0);
    begin(all);
    bothPatternRect(0, 0, 20, 16, shifted, 1, 10, 10, 50, 40, OBGPU_OP_DESTINATION_OUT);
    compare("destination-out, bilinear", 0, 3);
    begin(all);
    bothSave();
    check(bothClipPath(1, 48, 32, 25, 0, 0), "a clip path for a pattern");
    bothPatternRect(0, 0, 20, 16, shifted, 1, 0, 0, 96, 64, OBGPU_OP_SOURCE_OVER);
    bothRestore();
    compare("under a clip path (an A8 mask), bilinear", 0, 8);
    begin(all);
    {   /* a stroke painted with the picture */
        struct obgpu_pattern p;
        struct obgpu_stroke s;
        cairo_pattern_t *cp = cairo_pattern_create_for_surface(pic);
        cairo_matrix_t pm;
        memset(&s, 0, sizeof s);
        s.width = 6 << 16;
        s.cap = OBGPU_CAP_ROUND;
        s.join = OBGPU_JOIN_ROUND;
        s.miter = 10 << 16;
        ourPattern(&p, 0, 0, 20, 16, shifted, 1);
        check(obgpu_path_begin(&G), "a stroke's path");
        writePath(1, 48, 32, 22);
        check(obgpu_stroke_path_pattern(&G, &p, &s, OBGPU_OP_SOURCE_OVER), "a pattern stroke is drawn here");
        cairo_pattern_set_filter(cp, CAIRO_FILTER_BILINEAR);
        cairo_pattern_set_extend(cp, CAIRO_EXTEND_REPEAT);
        cairo_matrix_init(&pm, 1, 0, 0, 1, 3.25, 1.5);
        cairo_matrix_invert(&pm);
        cairo_pattern_set_matrix(cp, &pm);
        cairo_set_source(cr, cp);
        cairo_set_line_width(cr, 6);
        cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
        cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
        cairo_stroke(cr);
        cairo_pattern_destroy(cp);
    }
    compare("a stroke painted with the picture", 0, 40);
    {
        struct obgpu_pattern p;
        begin(all);
        ourPattern(&p, 15, 0, 10, 16, one, 0);
        check(obgpu_path_begin(&G) && obgpu_path_rect(&G, 0, 0, fx(10), fx(10)), "a rectangle");
        check(!obgpu_fill_path_pattern(&G, &p, 0, OBGPU_OP_SOURCE_OVER), "a tile past the picture goes to cairo");
        ourPattern(&p, 0, 0, 20, 16, one, 0);
        p.source.format = OBGPU_A8;
        check(obgpu_path_begin(&G) && obgpu_path_rect(&G, 0, 0, fx(10), fx(10)), "a rectangle");
        check(!obgpu_fill_path_pattern(&G, &p, 0, OBGPU_OP_SOURCE_OVER), "an A8 picture goes to cairo");
        check(!obgpu_pending(&G), "nothing was added for them");
    }

    printf("gradients: stops of one alpha against cairo, of different alphas as CSS\n");
    {
        double geo[6] = { 8, 0, 88, 0, 0, 0 };
        begin(all);
        bothGradientRect(OBGPU_LINEAR, geo, 3, sameAlpha, 8, 4, 80, 56, 1);
        compare("linear, stops of one alpha (0.8)", 0, 1);
        begin(all);
        bothGradientRect(OBGPU_LINEAR, geo, 3, mixedAlpha, 8, 4, 80, 56, 1);
        compare("linear, alphas 1, 0.1, 0.6: GFOpenGPU's mix against pixman's", 0, 255);
        premultipliedCheck("linear, alphas 1, 0.1, 0.6", 8, 88, 3, mixedAlpha, 4, 60, 2);
    }

    printf("conic gradients: against the angle's colour, and WebKit's cairo mesh\n");
    {
        static const double id[6] = { 1, 0, 0, 1, 0, 0 }, moved[6] = { 1, 0, 0, 1, 5.5, -3.25 };
        begin(all);
        bothConic(48, 32, 0, 4, conicStops, 0, 0, 96, 64);
        conicCheck("conic from the top, four stops", 48, 32, 0, id, 4, conicStops, 0, 0, 96, 64, 2);
        compare("  and cairo's mesh of four sectors (Bezier, not the angle)", 0, 255);
        begin(all);
        bothConic(40, 30, 1.0, 4, conicStops, 4, 4, 80, 50);
        conicCheck("conic from 1 radian", 40, 30, 1.0, id, 4, conicStops, 4, 4, 84, 54, 2);
        begin(all);
        bothMatrix(1, 0, 0, 1, 5.5, -3.25);
        bothConic(40, 34, 2.5, 4, conicStops, 0, 4, 90, 56);
        conicCheck("conic from 2.5 radians, under a transform", 40, 34, 2.5, moved, 4, conicStops, 6, 1, 95, 56, 2);
        makeDense(4, conicStops);
        begin(all);
        bothConic(48, 32, 0, 25, (const double (*)[5])dense, 0, 0, 96, 64);
        exX = 48; exY = 32; exR = 6;
        compare("conic, 25 stops, against cairo's mesh (6 px from the centre)", 0, 16);
        begin(all);
        bothMatrix(1, 0, 0, 1, 5.5, -3.25);
        bothConic(40, 34, 2.5, 25, (const double (*)[5])dense, 0, 4, 90, 56);
        exX = 45.5; exY = 30.75;
        compare("conic, 25 stops, under a transform, against cairo's mesh", 0, 16);
        exR = 0;
    }

    obgpu_release_masks(&G);
    check(masksLive == 0, "every mask freed");
    printf("test_opengpu_pattern: %d checks, %d failed\n", checks, failures);
    return failures != 0;
}
