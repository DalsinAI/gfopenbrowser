/*
 * Patch 0082b's drop shadows (OpenGPUPaintAmiga.c's obgpu_set_shadow) on
 * gfopenrtg's CPU core, against a reference made here as WebKit's cairo
 * port casts them: ShadowBlur's layer (the shape, moved by the offset,
 * filled by cairo with the shadow's mask alpha), blurred by ShadowBlur's
 * blurLayerImage with calculateLobes' box sizes (canvas: 2/3 of the blur;
 * CSS: sigma = blur / 2 times 3 sqrt(2 pi) / 4 times 0.88), ported here
 * line for line, then the shadow colour times the global alpha through it,
 * and the shape drawn by cairo over it. The writer asks GFOpenGPU's BLUR for
 * the same box sizes (obgpu_blur_radius_for_box).
 *   test_opengpu_shadow          (needs cairo's headers and library)
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


static unsigned long pm(double r, double g, double b, double a)
{
#define CH(v) ((unsigned long)((v) * 65535.0 + 0.5) >> 8)
    return CH(a) << 24 | CH(r * a) << 16 | CH(g * a) << 8 | CH(b * a);
#undef CH
}

/* ShadowBlur's calculateLobes */
static void calculateLobes(int lobes[3][2], float blurRadius, int ignoreTransforms)
{
    int diameter;
    if (ignoreTransforms)
        diameter = (int)floorf((2 / 3.f) * blurRadius);
    else {
        float stdDev = blurRadius / 2;
        const float gaussianKernelFactor = 3 / 4.f * sqrtf(2 * (float)M_PI);
        const float fudgeFactor = 0.88f;
        diameter = (int)floorf(stdDev * gaussianKernelFactor * fudgeFactor + 0.5f);
    }
    if (diameter < 2)
        diameter = 2;
    if (diameter & 1) {
        int lobeSize = (diameter - 1) / 2;
        lobes[0][0] = lobes[0][1] = lobes[1][0] = lobes[1][1] = lobes[2][0] = lobes[2][1] = lobeSize;
    } else {
        int lobeSize = diameter / 2;
        lobes[0][0] = lobeSize; lobes[0][1] = lobeSize - 1;
        lobes[1][0] = lobeSize - 1; lobes[1][1] = lobeSize;
        lobes[2][0] = lobeSize; lobes[2][1] = lobeSize;
    }
}

static int boxSize(float blurRadius, int ignoreTransforms)
{
    int lobes[3][2];
    calculateLobes(lobes, blurRadius, ignoreTransforms);
    return lobes[0][0] + lobes[0][1] + 1;            /* the first pass's box */
}

/* ShadowBlur's blurLayerImage: alpha in an ARGB32 image (native words),
 * three sliding boxes each way, sums times a rounded-up reciprocal */
static void blurLayerImage(unsigned char *imageData, int width, int height, int rowStride, float radius, int ignoreTransforms)
{
    static const int blurSumShift = 15;
    const int channels[4] = { 3, 0, 1, 3 };
    int lobes[3][2], stride = 4, delta = rowStride, final = height, dim = width, pass;
    calculateLobes(lobes, radius, ignoreTransforms);
    for (pass = 0; pass < 2; ++pass) {
        unsigned char *pixels = imageData;
        int j;
        for (j = 0; j < final; ++j, pixels += delta) {
            int step;
            for (step = 0; step < 3; ++step) {
                int side1 = lobes[step][0], side2 = lobes[step][1], pixelCount = side1 + 1 + side2;
                int invCount = ((1 << blurSumShift) + pixelCount - 1) / pixelCount, ofs = 1 + side2;
                int alpha1 = pixels[channels[step]], alpha2 = pixels[(dim - 1) * stride + channels[step]];
                unsigned char *ptr = pixels + channels[step + 1], *prev = pixels + stride + channels[step];
                unsigned char *next = pixels + ofs * stride + channels[step];
                int i, sum = side1 * alpha1 + alpha1, limit = dim < side2 + 1 ? dim : side2 + 1;
                for (i = 1; i < limit; ++i, prev += stride)
                    sum += *prev;
                if (limit <= side2)
                    sum += (side2 - limit + 1) * alpha2;
                limit = side1 < dim ? side1 : dim;
                for (i = 0; i < limit; ptr += stride, next += stride, ++i, ++ofs) {
                    *ptr = (unsigned char)((sum * invCount) >> blurSumShift);
                    sum += ((ofs < dim) ? *next : alpha2) - alpha1;
                }
                prev = pixels + channels[step];
                for (; ofs < dim; ptr += stride, prev += stride, next += stride, ++i, ++ofs) {
                    *ptr = (unsigned char)((sum * invCount) >> blurSumShift);
                    sum += (*next) - (*prev);
                }
                for (; i < dim; ptr += stride, prev += stride, ++i) {
                    *ptr = (unsigned char)((sum * invCount) >> blurSumShift);
                    sum += alpha2 - (*prev);
                }
            }
        }
        stride = rowStride;
        delta = 4;
        final = width;
        dim = height;
    }
}

/* on a little-endian host the alpha of a native ARGB32 word is byte 3 */
static int hostAlphaByte(void)
{
    unsigned int v = 0xFF000000u;
    return ((unsigned char *)&v)[3] == 0xFF ? 3 : 0;
}

/* The shape: a rectangle (kind 0), a circle (1), or a circle stroked 4 wide (2) */
static void cairoShape(cairo_t *c, int kind, double ox, double oy)
{
    if (kind == 0)
        cairo_rectangle(c, 20 + ox, 14 + oy, 34, 22);
    else {
        double cx = 46 + ox, cy = 30 + oy, r = 16, k = 0.5522847498 * r;
        cairo_move_to(c, fx(cx + r) / 256.0, fx(cy) / 256.0);
        cairo_curve_to(c, fx(cx + r) / 256.0, fx(cy + k) / 256.0, fx(cx + k) / 256.0, fx(cy + r) / 256.0, fx(cx) / 256.0, fx(cy + r) / 256.0);
        cairo_curve_to(c, fx(cx - k) / 256.0, fx(cy + r) / 256.0, fx(cx - r) / 256.0, fx(cy + k) / 256.0, fx(cx - r) / 256.0, fx(cy) / 256.0);
        cairo_curve_to(c, fx(cx - r) / 256.0, fx(cy - k) / 256.0, fx(cx - k) / 256.0, fx(cy - r) / 256.0, fx(cx) / 256.0, fx(cy - r) / 256.0);
        cairo_curve_to(c, fx(cx + k) / 256.0, fx(cy - r) / 256.0, fx(cx + r) / 256.0, fx(cy - k) / 256.0, fx(cx + r) / 256.0, fx(cy) / 256.0);
        cairo_close_path(c);
    }
}

/* One case on both: shape kind, fill colour (unpremultiplied r, g, b, a),
 * shadow colour, offset, blur, canvas or CSS sizes, global alpha, the
 * mask alpha rule (1: ShadowBlur's rectangle shadow, 255; 0: the paint's) */
static void shadowCase(const char *what, int kind, const double *c, const double *sc, double ox, double oy, float blur,
    int canvas, double ga, int rectShadow, int clipCircle, int bound)
{
    struct obgpu_shadow s;
    int box = blur > 0 ? boxSize(blur > 128 ? 128 : blur, canvas) : 0;
    unsigned maskAlpha = rectShadow ? 255 : (unsigned)(pm(0, 0, 0, c[3]) >> 24);
    cairo_surface_t *layer;
    cairo_t *lc;
    int pad = 80, lw = W + 2 * pad, lh = H + 2 * pad;
    begin(OBGPU_CAN_PD | OBGPU_CAN_PATH | OBGPU_CAN_ALIASED);
    if (clipCircle) {
        obgpu_save(&G);
        cairo_save(cr);
        check(bothClipPath(1, 48, 32, 30, 0, 0), "a clip path");
    }
    /* the writer */
    s.dx = fx(ox);
    s.dy = fx(oy);
    s.box_x = s.box_y = box;
    s.argb = pm(sc[0], sc[1], sc[2], sc[3] * ga);
    s.mask_alpha = maskAlpha;
    obgpu_set_shadow(&G, &s);
    check(obgpu_path_begin(&G), "the shape's path");
    if (kind == 0)
        obgpu_path_rect(&G, fx(20), fx(14), fx(34), fx(22));
    else
        writePath(1, 46, 30, 16);
    if (kind == 2) {
        struct obgpu_stroke st;
        memset(&st, 0, sizeof st);
        st.width = 4 << 16;
        st.miter = 10 << 16;
        check(obgpu_stroke_path(&G, pm(c[0], c[1], c[2], c[3] * ga), 0, &st, OBGPU_OP_SOURCE_OVER), "a shadowed stroke is drawn here");
    } else
        check(obgpu_fill_path(&G, pm(c[0], c[1], c[2], c[3] * ga), 0, 0, OBGPU_OP_SOURCE_OVER), "a shadowed fill is drawn here");
    obgpu_set_shadow(&G, 0);
    /* the reference: ShadowBlur's layer, blurred, the colour through it, then the shape */
    layer = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, lw, lh);
    lc = cairo_create(layer);
    cairo_translate(lc, pad, pad);
    cairoShape(lc, kind, ox, oy);
    cairo_set_source_rgba(lc, 0, 0, 0, maskAlpha / 255.0);
    if (kind == 2) {
        cairo_set_line_width(lc, 4);
        cairo_stroke(lc);
    } else
        cairo_fill(lc);
    cairo_destroy(lc);
    cairo_surface_flush(layer);
    if (box >= 2) {
        /* blurLayerImage blurs the alpha byte; the colour comes after by SourceIn */
        unsigned char *d = cairo_image_surface_get_data(layer);
        int x, y, a = hostAlphaByte(), st = cairo_image_surface_get_stride(layer);
        if (a == 3) {
            blurLayerImage(d, lw, lh, st, blur > 128 ? 128 : blur, canvas);
        } else {
            /* big-endian host: alpha is byte 0; move it to byte 3 and back */
            for (y = 0; y < lh; y++)
                for (x = 0; x < lw; x++)
                    d[y * st + x * 4 + 3] = d[y * st + x * 4];
            blurLayerImage(d, lw, lh, st, blur > 128 ? 128 : blur, canvas);
            for (y = 0; y < lh; y++)
                for (x = 0; x < lw; x++)
                    d[y * st + x * 4] = d[y * st + x * 4 + 3];
        }
        cairo_surface_mark_dirty(layer);
    }
    cairo_set_source_rgba(cr, sc[0], sc[1], sc[2], sc[3] * ga);
    cairo_mask_surface(cr, layer, -pad, -pad);
    cairo_surface_destroy(layer);
    cairo_new_path(cr);
    cairoShape(cr, kind, 0, 0);
    cairo_set_source_rgba(cr, c[0], c[1], c[2], c[3] * ga);
    if (kind == 2) {
        cairo_set_line_width(cr, 4);
        cairo_stroke(cr);
    } else
        cairo_fill(cr);
    if (clipCircle) {
        obgpu_restore(&G);
        cairo_restore(cr);
    }
    compare(what, 0, bound);
}

int main(void)
{
    static const double red[4] = { 0.85, 0.1, 0.1, 1 }, blueHalf[4] = { 0.1, 0.2, 0.9, 0.5 }, black[4] = { 0, 0, 0, 0.75 },
        navy[4] = { 0.0, 0.1, 0.4, 1 };
    int k;
    printf("ShadowBlur's box sizes, and GFOpenGPU's for the radius asked\n");
    {
        static const float radii[] = { 1, 2, 3, 4, 5, 7.5f, 10, 13, 20, 33, 64, 128 };
        int i, bad = 0;
        for (i = 0; i < (int)(sizeof radii / sizeof radii[0]); i++)
            for (k = 0; k < 2; k++) {
                int want = boxSize(radii[i], k);
                unsigned long r = obgpu_blur_radius_for_box(want);
                /* the core's box_size, as ogpu_core.c works it out */
                int got = (int)((((long long)r << 11) * 123207 + 0x80000000LL) >> 32);
                if (got != want)
                    bad++;
            }
        check(!bad, "each ShadowBlur box size asked of BLUR comes back the same");
        printf("  blur 10: canvas box %d, CSS box %d (GFOpenGPU's own for 10 would be %d)\n", boxSize(10, 1), boxSize(10, 0),
            (int)(((((long long)160) << 11) * 123207 + 0x80000000LL) >> 32));
    }
    printf("shadows against ShadowBlur's\n");
    shadowCase("CSS: a rectangle, offset (4, 3), blur 6 (rectangle shadow)", 0, red, black, 4, 3, 6, 0, 1, 1, 0, 3);
    shadowCase("CSS: a circle, translucent, offset (-3.5, 5), blur 10", 1, blueHalf, black, -3.5, 5, 10, 0, 1, 0, 0, 12);
    shadowCase("canvas: a circle, offset (6, 6), blur 9, global alpha 0.6", 1, red, navy, 6, 6, 9, 1, 0.6, 0, 0, 12);
    shadowCase("canvas: a stroked circle, offset (2, 4), blur 4", 2, red, black, 2, 4, 4, 1, 1, 0, 0, 12);
    shadowCase("no blur: a rectangle moved (5.25, 2.5)", 0, red, black, 5.25, 2.5, 0, 0, 1, 1, 0, 12);
    shadowCase("CSS: a large blur (40) over the window's edge", 0, red, black, 10, 8, 40, 0, 1, 1, 0, 3);
    shadowCase("under a clip path", 1, red, black, 4, 4, 8, 0, 1, 0, 1, 12);
    {   /* a shadow with another operator, or a gradient, goes to cairo */
        struct obgpu_shadow s;
        memset(&s, 0, sizeof s);
        s.dx = fx(3);
        s.box_x = s.box_y = 4;
        s.argb = 0xC0000000UL;
        s.mask_alpha = 255;
        begin(OBGPU_CAN_PD | OBGPU_CAN_PATH | OBGPU_CAN_ALIASED);
        obgpu_set_shadow(&G, &s);
        check(obgpu_path_begin(&G) && obgpu_path_rect(&G, fx(10), fx(10), fx(20), fx(20)), "a rectangle");
        check(!obgpu_fill_path(&G, 0xFF000000UL, 0, 0, OBGPU_OP_COPY), "a shadow with copy goes to cairo");
        check(!obgpu_pending(&G), "nothing added for it");
        obgpu_set_shadow(&G, 0);
    }
    obgpu_release_masks(&G);
    check(masksLive == 0, "every mask and shadow layer freed");
    printf("test_opengpu_shadow: %d checks, %d failed\n", checks, failures);
    return failures != 0;
}
