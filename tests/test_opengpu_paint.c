/*
 * The engine's GraphicsContextOpenGPU stream writer (WebKit patch 0076's
 * OpenGPUPaintAmiga.c) run on openamigartg's CPU core (ogpu_core.c), and
 * the pixels compared with a reference written here from stream.h's
 * Porter-Duff formulas and cairo's rules for each call: fills by every
 * operator WebKit uses, nested clips with save and restore, pictures with
 * alpha, global alpha, translate and scale, and the states and transforms
 * that must go to cairo (the call returns 0 and draws nothing).
 *   test_opengpu_paint
 * MIT, Copyright (c) 2026 AmigaChrome Team.
 * SPDX-License-Identifier: MIT
 */
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
#define BPR         (W * 4 + 32)
#define TARGET      0x1000UL
#define PICTURES    0x40000UL       /* picture k at PICTURES + k * PICTURE_BYTES */
#define PICTURE_BYTES 0x10000UL
#define NPICTURES   20
#define SCRATCH     0x200000UL
#define WORK        0x300000UL
#define WORK_BYTES  ((W + 4) * 32 * 4)

static unsigned char arena[ARENA];
static unsigned char expect[H][BPR];
static unsigned char streamBuffer[4096 * 4];
static struct ogpu_core core;
static struct obgpu G;
static struct obgpu_counts counts;
static long submitted, coreErrors;
static int failures, checks;

static ogpu_u8 *map(void *user, ogpu_u32 address, ogpu_u32 length)
{
    (void)user;
    if (address >= ARENA || length > (ogpu_u32)ARENA - address)
        return 0;
    return arena + address;
}

static long submit(void *user, const unsigned char *stream, long words)
{
    long r;
    (void)user;
    submitted++;
    ogpu_core_run(&core, stream, words);
    r = core.last_error;
    if (r)
        coreErrors++;
    return r;
}

static unsigned rndState = 0x2026;
static unsigned rnd(void)
{
    rndState = rndState * 1103515245u + 12345u;
    return (rndState >> 8) & 0xFFFFFFu;
}

/* ---- the reference ---------------------------------------------------------------------- */

struct px { int a, r, g, b; };

static int m255(int x, int y) { int t = x * y + 128; return (t + (t >> 8)) >> 8; }
static int sat(int v) { return v > 255 ? 255 : v; }

/* stream.h's table: d = s Fa + d Fb */
static struct px combine(int pd, struct px s, struct px d)
{
    int fa = 0, fb = 0;
    struct px o;
    if (pd == OGPU_PD_ADD) {
        o.a = sat(s.a + d.a); o.r = sat(s.r + d.r); o.g = sat(s.g + d.g); o.b = sat(s.b + d.b);
        return o;
    }
    switch (pd) {
    case OGPU_PD_SRC: fa = 255; fb = 0; break;
    case OGPU_PD_OVER: fa = 255; fb = 255 - s.a; break;
    case OGPU_PD_OVER_REVERSE: fa = 255 - d.a; fb = 255; break;
    case OGPU_PD_OUT_REVERSE: fa = 0; fb = 255 - s.a; break;
    case OGPU_PD_ATOP: fa = d.a; fb = 255 - s.a; break;
    case OGPU_PD_XOR: fa = 255 - d.a; fb = 255 - s.a; break;
    default: break;     /* CLEAR */
    }
    o.a = sat(m255(s.a, fa) + m255(d.a, fb));
    o.r = sat(m255(s.r, fa) + m255(d.r, fb));
    o.g = sat(m255(s.g, fa) + m255(d.g, fb));
    o.b = sat(m255(s.b, fa) + m255(d.b, fb));
    return o;
}

static struct px scale(struct px s, int m)
{
    s.a = m255(s.a, m); s.r = m255(s.r, m); s.g = m255(s.g, m); s.b = m255(s.b, m);
    return s;
}

static struct px load(const unsigned char *p, int rgb)
{
    struct px o;
    o.a = rgb ? 255 : p[0]; o.r = p[1]; o.g = p[2]; o.b = p[3];
    return o;
}

static void store(unsigned char *p, struct px c)
{
    p[0] = (unsigned char)c.a; p[1] = (unsigned char)c.r; p[2] = (unsigned char)c.g; p[3] = (unsigned char)c.b;
}

static struct px colour(unsigned long argb)
{
    struct px c;
    c.a = (int)(argb >> 24) & 255; c.r = (int)(argb >> 16) & 255; c.g = (int)(argb >> 8) & 255; c.b = (int)argb & 255;
    return c;
}

/* WebCore's operator to stream.h's, where the writer draws it; -1 cairo's. */
static int pdOf(int op)
{
    switch (op) {
    case OBGPU_OP_CLEAR: return OGPU_PD_CLEAR;
    case OBGPU_OP_COPY: return OGPU_PD_SRC;
    case OBGPU_OP_SOURCE_OVER: return OGPU_PD_OVER;
    case OBGPU_OP_SOURCE_ATOP: return OGPU_PD_ATOP;
    case OBGPU_OP_DESTINATION_OVER: return OGPU_PD_OVER_REVERSE;
    case OBGPU_OP_DESTINATION_OUT: return OGPU_PD_OUT_REVERSE;
    case OBGPU_OP_XOR: return OGPU_PD_XOR;
    case OBGPU_OP_PLUS_LIGHTER: return OGPU_PD_ADD;
    default: return -1;
    }
}

/* The model's state, in doubles, beside the writer's. */
struct mlevel { double m[6]; double cx0, cy0, cx1, cy1; int bad, other, group; };
static struct mlevel ms[OBGPU_DEPTH + 16];
static int mdepth, mlost;

static struct mlevel *cur(void) { return &ms[mdepth]; }

static int isWhole(double v) { return fabs(v - floor(v + 0.5)) < 1e-9; }

static void devX(const double *m, double x, double y, double *ox, double *oy)
{
    *ox = m[0] * x + m[2] * y + m[4];
    *oy = m[1] * x + m[3] * y + m[5];
}

/* An upright rectangle on the target, sorted: 0 if the transform turns it. */
static int devRect(const struct mlevel *l, double x, double y, double w, double h, double *e)
{
    double t;
    if (l->m[1] != 0 || l->m[2] != 0)
        return 0;
    devX(l->m, x, y, &e[0], &e[1]);
    devX(l->m, x + w, y + h, &e[2], &e[3]);
    if (e[0] > e[2]) { t = e[0]; e[0] = e[2]; e[2] = t; }
    if (e[1] > e[3]) { t = e[1]; e[1] = e[3]; e[3] = t; }
    return 1;
}

/* Every edge the clip leaves visible on a whole pixel (none when it is
 * wholly outside). */
static int edgesWhole(const struct mlevel *l, const double *e)
{
    if (e[2] <= l->cx0 || e[0] >= l->cx1 || e[3] <= l->cy0 || e[1] >= l->cy1 || l->cx1 <= l->cx0 || l->cy1 <= l->cy0)
        return 1;
    return (e[0] <= l->cx0 || isWhole(e[0])) && (e[1] <= l->cy0 || isWhole(e[1]))
        && (e[2] >= l->cx1 || isWhole(e[2])) && (e[3] >= l->cy1 || isWhole(e[3]));
}

static int mdrawable(void)
{
    return !G.broken && !mlost && !cur()->bad && !cur()->other && !cur()->group;
}

/* ---- the calls, on the writer and on the model ------------------------------------------- */

static long fx16(double v) { return lround(v * 65536.0); }
static long fx8(double v) { return lround(v * 256.0); }

static void reset(int caps, long streamWords, long scratchWords)
{
    int y;
    ogpu_core_init(&core);
    core.map = map;
    memset(&counts, 0, sizeof counts);
    obgpu_init(&G, streamBuffer, streamWords, arena + SCRATCH, SCRATCH, scratchWords, submit, 0, caps, &counts);
    obgpu_target(&G, TARGET, BPR, W, H);
    obgpu_work(&G, WORK, WORK_BYTES);
    for (y = 0; y < H; y++) {
        int x;
        for (x = 0; x < W; x++) {
            struct px c;
            unsigned k = rnd() % 4;
            c.a = k == 0 ? 0 : k == 1 ? 255 : (int)(rnd() & 255);
            c.r = c.a ? (int)(rnd() % (unsigned)(c.a + 1)) : 0;
            c.g = c.a ? (int)(rnd() % (unsigned)(c.a + 1)) : 0;
            c.b = c.a ? (int)(rnd() % (unsigned)(c.a + 1)) : 0;
            store(&expect[y][x * 4], c);
        }
        memcpy(arena + TARGET + (unsigned long)y * BPR, expect[y], BPR);
    }
    memset(ms, 0, sizeof ms);
    mdepth = mlost = 0;
    ms[0].m[0] = ms[0].m[3] = 1;
    ms[0].cx1 = W;
    ms[0].cy1 = H;
    submitted = coreErrors = 0;
}

static void tSave(void)
{
    obgpu_save(&G);
    if (mlost || mdepth + 1 >= OBGPU_DEPTH) {
        mlost++;
        return;
    }
    ms[mdepth + 1] = ms[mdepth];
    mdepth++;
}

static void tRestore(void)
{
    obgpu_restore(&G);
    if (mlost)
        mlost--;
    else if (mdepth)
        mdepth--;
}

static void tMatrix(double a, double b, double c, double d, double e, double f)
{
    long m[6];
    m[0] = fx16(a); m[1] = fx16(b); m[2] = fx16(c); m[3] = fx16(d); m[4] = fx8(e); m[5] = fx8(f);
    obgpu_set_matrix(&G, m);
    if (mlost)
        return;
    cur()->m[0] = a; cur()->m[1] = b; cur()->m[2] = c; cur()->m[3] = d; cur()->m[4] = e; cur()->m[5] = f;
    cur()->bad = 0;
}

static void tBadMatrix(void)
{
    obgpu_set_matrix(&G, 0);
    if (!mlost)
        cur()->bad = 1;
}

static void tClip(double x, double y, double w, double h)
{
    struct mlevel *l = cur();
    double e[4];
    obgpu_clip(&G, fx8(x), fx8(y), fx8(w), fx8(h));
    if (mlost || l->other)
        return;
    if (l->bad || !devRect(l, x, y, w, h, e) || !edgesWhole(l, e)) {
        l->other = 1;
        return;
    }
    if (e[0] > l->cx0) l->cx0 = e[0];
    if (e[1] > l->cy0) l->cy0 = e[1];
    if (e[2] < l->cx1) l->cx1 = e[2];
    if (e[3] < l->cy1) l->cy1 = e[3];
    if (l->cx1 < l->cx0) l->cx1 = l->cx0;
    if (l->cy1 < l->cy0) l->cy1 = l->cy0;
}

static void expectResult(const char *what, int got, int want)
{
    checks++;
    if (got != want) {
        printf("FAIL %s: returned %d, expected %d\n", what, got, want);
        failures++;
    }
}

/* As GraphicsContextOpenGPU: a call cairo draws sends the batch first. */
static void handedOn(void)
{
    obgpu_flush(&G);
}

static void tFill(const char *what, double x, double y, double w, double h, unsigned long argb, int op)
{
    struct mlevel *l = cur();
    double e[4];
    int pd = pdOf(op), want = 1, px, py, r;
    r = obgpu_fill(&G, fx8(x), fx8(y), fx8(w), fx8(h), argb, op);
    if (!mdrawable() || pd < 0 || !devRect(l, x, y, w, h, e) || !edgesWhole(l, e))
        want = 0;
    expectResult(what, r, want);
    if (!r)
        handedOn();
    if (!want)
        return;
    for (py = 0; py < H; py++)
        for (px = 0; px < W; px++) {
            double cx = px + 0.5, cy = py + 0.5;
            if (cx < l->cx0 || cx > l->cx1 || cy < l->cy0 || cy > l->cy1)
                continue;
            if (cx < e[0] || cx > e[2] || cy < e[1] || cy > e[3])
                continue;
            store(&expect[py][px * 4], combine(pd, colour(argb), load(&expect[py][px * 4], 0)));
        }
}

/* Pictures: random premultiplied ARGB, or RGB. */
static struct obgpu_source pic[NPICTURES];

static void makePicture(int k, int w, int h, int format)
{
    struct obgpu_source *s = &pic[k];
    int x, y;
    s->address = PICTURES + (unsigned long)k * PICTURE_BYTES;
    s->bpr = w * 4 + 8;
    s->w = w;
    s->h = h;
    s->format = format;
    for (y = 0; y < h; y++)
        for (x = 0; x < w; x++) {
            unsigned char *p = arena + s->address + (unsigned long)y * (unsigned long)s->bpr + (unsigned long)x * 4;
            struct px c;
            unsigned q = rnd() % 4;
            c.a = q == 0 ? 0 : q == 1 ? 255 : (int)(rnd() & 255);
            if (format == OBGPU_RGB) {
                c.a = (int)(rnd() & 255);      /* the unused byte: never read */
                c.r = (int)(rnd() & 255); c.g = (int)(rnd() & 255); c.b = (int)(rnd() & 255);
            } else {
                c.r = c.a ? (int)(rnd() % (unsigned)(c.a + 1)) : 0;
                c.g = c.a ? (int)(rnd() % (unsigned)(c.a + 1)) : 0;
                c.b = c.a ? (int)(rnd() % (unsigned)(c.a + 1)) : 0;
            }
            store(p, c);
        }
}

static struct px picPixel(const struct obgpu_source *s, int x, int y)
{
    return load(arena + s->address + (unsigned long)y * (unsigned long)s->bpr + (unsigned long)x * 4, s->format == OBGPU_RGB);
}

/* Each pixel's tolerance (0 exact) and whether it is compared at all. */
static unsigned char tol[H][W];
static unsigned char skip[H][W];

static double overlap(double a0, double a1, double b0, double b1)
{
    double lo = a0 > b0 ? a0 : b0, hi = a1 < b1 ? a1 : b1;
    return hi > lo ? hi - lo : 0;
}

/* want: 1 drawn, 0 handed on; pixel: 1 pixel for pixel (exact), 2 through a
 * pattern (nearest exact where the edges are whole, else within 3). */
static void tImage(const char *what, int k, double dx, double dy, double dw, double dh, double sx, double sy, double sw,
    double sh, int op, int alpha, int bilinear, int want)
{
    const struct obgpu_source *s = &pic[k];
    struct mlevel *l = cur();
    long dst[4], src[4];
    double inv[6], det, e[4], psx, psy, psw, psh;
    int r, pd = pdOf(op), px, py, upright;
    dst[0] = fx8(dx); dst[1] = fx8(dy); dst[2] = fx8(dw); dst[3] = fx8(dh);
    src[0] = fx8(sx); src[1] = fx8(sy); src[2] = fx8(sw); src[3] = fx8(sh);
    r = obgpu_image(&G, s, dst, src, op, (unsigned)alpha, bilinear);
    expectResult(what, r, want);
    if (!r)
        handedOn();
    if (want != 1 || dw < 0.5 || dh < 0.5 || !alpha)
        return;
    det = l->m[0] * l->m[3] - l->m[1] * l->m[2];
    inv[0] = l->m[3] / det; inv[1] = -l->m[1] / det; inv[2] = -l->m[2] / det; inv[3] = l->m[0] / det;
    inv[4] = -(inv[0] * l->m[4] + inv[2] * l->m[5]);
    inv[5] = -(inv[1] * l->m[4] + inv[3] * l->m[5]);
    upright = devRect(l, dx, dy, dw, dh, e);
    psx = floor(sx); psy = floor(sy);
    psw = ceil(sx + sw) - psx; psh = ceil(sy + sh) - psy;
    for (py = 0; py < H; py++)
        for (px = 0; px < W; px++) {
            double cx = px + 0.5, cy = py + 0.5, ux, uy, qx, qy, cov;
            struct px p;
            if (cx < l->cx0 || cx > l->cx1 || cy < l->cy0 || cy > l->cy1)
                continue;
            if (upright) {
                cov = overlap(px, px + 1, e[0], e[2]) * overlap(py, py + 1, e[1], e[3]);
                if (cov <= 0)
                    continue;
                if (cov < 1)
                    tol[py][px] = 3;
            } else {
                /* turned: only the pixels wholly inside, and none outside the box */
                int c, in = 0;
                double bx0 = 1e9, by0 = 1e9, bx1 = -1e9, by1 = -1e9;
                for (c = 0; c < 4; c++) {
                    double ox, oy, vx, vy;
                    devX(l->m, dx + (c & 1 ? dw : 0), dy + (c & 2 ? dh : 0), &ox, &oy);
                    if (ox < bx0) bx0 = ox;
                    if (ox > bx1) bx1 = ox;
                    if (oy < by0) by0 = oy;
                    if (oy > by1) by1 = oy;
                    devX(inv, px + (c & 1), py + (c >> 1), &vx, &vy);
                    in += vx >= dx && vx <= dx + dw && vy >= dy && vy <= dy + dh;
                }
                if (px + 1 <= bx0 || px >= bx1 || py + 1 <= by0 || py >= by1)
                    continue;
                if (in < 4) {
                    skip[py][px] = 1;
                    continue;
                }
                cov = 1;
            }
            /* the pixel's centre in the picture */
            devX(inv, cx, cy, &ux, &uy);
            qx = sx + (ux - dx) * sw / dw - psx;
            qy = sy + (uy - dy) * sh / dh - psy;
            if (bilinear == 1) {
                double fx0 = qx - 0.5, fy0 = qy - 0.5, fx, fy, ch[4];
                int x0 = (int)floor(fx0), y0 = (int)floor(fy0), i, xs[2], ys[2];
                struct px q[4];
                fx = fx0 - x0; fy = fy0 - y0;
                for (i = 0; i < 2; i++) {
                    xs[i] = x0 + i < 0 ? 0 : x0 + i >= psw ? (int)psw - 1 : x0 + i;
                    ys[i] = y0 + i < 0 ? 0 : y0 + i >= psh ? (int)psh - 1 : y0 + i;
                }
                for (i = 0; i < 4; i++)
                    q[i] = picPixel(s, (int)psx + xs[i & 1], (int)psy + ys[i >> 1]);
                for (i = 0; i < 4; i++) {
                    double v[4];
                    int j;
                    for (j = 0; j < 4; j++)
                        v[j] = i == 0 ? q[j].a : i == 1 ? q[j].r : i == 2 ? q[j].g : q[j].b;
                    ch[i] = (v[0] * (1 - fx) + v[1] * fx) * (1 - fy) + (v[2] * (1 - fx) + v[3] * fx) * fy;
                }
                p.a = (int)(ch[0] + 0.5); p.r = (int)(ch[1] + 0.5); p.g = (int)(ch[2] + 0.5); p.b = (int)(ch[3] + 0.5);
                if (tol[py][px] < 3)
                    tol[py][px] = 3;
            } else {
                int ix = (int)floor(qx), iy = (int)floor(qy);
                ix = ix < 0 ? 0 : ix >= psw ? (int)psw - 1 : ix;
                iy = iy < 0 ? 0 : iy >= psh ? (int)psh - 1 : iy;
                p = picPixel(s, (int)psx + ix, (int)psy + iy);
            }
            p = scale(p, alpha);
            if (cov < 1)
                p = scale(p, (int)(cov * 255 + 0.5));
            store(&expect[py][px * 4], combine(pd, p, load(&expect[py][px * 4], 0)));
        }
}

/* Sends the batch and compares every pixel. */
static void verify(const char *what)
{
    int x, y, bad = 0;
    obgpu_flush(&G);
    checks++;
    if (coreErrors) {
        printf("FAIL %s: the core answered an error\n", what);
        failures++;
    }
    for (y = 0; y < H; y++)
        for (x = 0; x < W; x++) {
            const unsigned char *g = arena + TARGET + (unsigned long)y * BPR + (unsigned long)x * 4, *e = &expect[y][x * 4];
            int i;
            if (skip[y][x])
                continue;
            for (i = 0; i < 4; i++)
                if (abs(g[i] - e[i]) > tol[y][x]) {
                    if (bad++ < 4)
                        printf("FAIL %s: pixel %d,%d is %02x%02x%02x%02x, expected %02x%02x%02x%02x\n", what, x, y,
                            g[0], g[1], g[2], g[3], e[0], e[1], e[2], e[3]);
                    break;
                }
        }
    if (bad)
        failures++;
    /* the pixels as drawn: the next test starts from them */
    for (y = 0; y < H; y++)
        memcpy(expect[y], arena + TARGET + (unsigned long)y * BPR, BPR);
    memset(tol, 0, sizeof tol);
    memset(skip, 0, sizeof skip);
}

static void expectCount(const char *what, long got, long want)
{
    checks++;
    if (got != want) {
        printf("FAIL %s: %ld, expected %ld\n", what, got, want);
        failures++;
    }
}

/* ---- the tests ---------------------------------------------------------------------------- */

static const int allOps[14] = {
    OBGPU_OP_CLEAR, OBGPU_OP_COPY, OBGPU_OP_SOURCE_OVER, OBGPU_OP_SOURCE_IN, OBGPU_OP_SOURCE_OUT,
    OBGPU_OP_SOURCE_ATOP, OBGPU_OP_DESTINATION_OVER, OBGPU_OP_DESTINATION_IN, OBGPU_OP_DESTINATION_OUT,
    OBGPU_OP_DESTINATION_ATOP, OBGPU_OP_XOR, OBGPU_OP_PLUS_DARKER, OBGPU_OP_PLUS_LIGHTER, OBGPU_OP_DIFFERENCE
};

static void testFills(void)
{
    static const unsigned long colours[4] = { 0xFF2040C0UL, 0x80402000UL, 0x00000000UL, 0x40404040UL };
    char what[64];
    int i, c;
    reset(OBGPU_CAN_PD | OBGPU_CAN_PATH, 4096, 1024);
    for (i = 0; i < 14; i++)
        for (c = 0; c < 4; c++) {
            sprintf(what, "fill op %d colour %d", allOps[i], c);
            tFill(what, 3 + i * 6, 2 + c * 14, 5, 12, colours[c], allOps[i]);
        }
    verify("fills by every operator");
    /* a batch sent before each operator cairo draws, when one is waiting */
    expectCount("fills' batches", submitted, 5);
    /* a whole-target opaque fill, and a clear */
    tFill("whole fill", 0, 0, W, H, 0xFF102030UL, OBGPU_OP_SOURCE_OVER);
    tFill("clearRect", 10, 10, 20, 20, 0x12345678UL, OBGPU_OP_CLEAR);
    tFill("empty", 10, 10, 0, 20, 0xFF000000UL, OBGPU_OP_COPY);
    tFill("negative size", 50, 40, -10, -8, 0xFF00FF00UL, OBGPU_OP_COPY);
    tFill("partly off the target", -10, -10, 20, 30, 0x80800000UL, OBGPU_OP_SOURCE_OVER);
    tFill("off the target", W + 10, 0, 20, 30, 0xFF800000UL, OBGPU_OP_COPY);
    verify("fills, sizes and edges");
    expectCount("draws counted", (long)counts.draws, 4 * 8 - 3 + 4);   /* zero colours by over, add, dest-over draw nothing */
    /* nothing pending: no batch */
    submitted = 0;
    obgpu_flush(&G);
    expectCount("an empty flush", submitted, 0);
}

static void testTransforms(void)
{
    reset(OBGPU_CAN_PD | OBGPU_CAN_PATH, 4096, 1024);
    tSave();
    tMatrix(1, 0, 0, 1, 7, 5);
    tFill("translated", 1, 1, 10, 10, 0xFF00FF00UL, OBGPU_OP_COPY);
    tMatrix(2, 0, 0, 2, 0.5, 0);
    tFill("scaled, a half-pixel move", 2, 10, 4.25, 3, 0xFF0000FFUL, OBGPU_OP_COPY);
    tFill("scaled, its edges then whole", 2.25, 10, 4, 3, 0xC0600000UL, OBGPU_OP_SOURCE_OVER);
    tMatrix(-1, 0, 0, 0.5, 90, 40);
    tFill("flipped", 10, 10, 20, 20, 0x80008000UL, OBGPU_OP_XOR);
    tMatrix(1, 0, 0, 1, 0.25, 0);
    tFill("a quarter-pixel move", 30, 30, 10, 10, 0xFF000000UL, OBGPU_OP_COPY);
    tFill("its fractional edges off the target", -20.25, -3.25, W + 50, 20.25, 0x30303030UL, OBGPU_OP_PLUS_LIGHTER);
    tMatrix(0.8660254, 0.5, -0.5, 0.8660254, 20, 0);
    tFill("turned", 10, 10, 20, 20, 0xFF000000UL, OBGPU_OP_COPY);
    tMatrix(0, 1, -1, 0, 50, 0);
    tFill("a quarter turn", 10, 10, 20, 20, 0xFF000000UL, OBGPU_OP_COPY);
    tBadMatrix();
    tFill("a transform out of range", 10, 10, 20, 20, 0xFF000000UL, OBGPU_OP_COPY);
    tRestore();
    tFill("back to the identity", 60, 2, 8, 8, 0xFFFFFFFFUL, OBGPU_OP_COPY);
    verify("transforms");
}

static void testClips(void)
{
    reset(OBGPU_CAN_PD | OBGPU_CAN_PATH, 4096, 1024);
    tSave();
    tClip(4, 4, 60, 50);
    tFill("in the first clip", 0, 0, W, H, 0x80102030UL, OBGPU_OP_SOURCE_OVER);
    tSave();
    tMatrix(2, 0, 0, 2, 10, 0);
    tClip(5, 5, 20, 10);           /* 20..60, 10..30 on the target */
    tFill("in the nested clip", 0, 0, 100, 100, 0xFF00FF00UL, OBGPU_OP_COPY);
    tFill("a fractional fill inside the clip", 7.25, 6, 4, 4, 0xFF0000FFUL, OBGPU_OP_COPY);
    tFill("its fractional edges clipped away", 4.75, 4.75, 21, 11, 0x40000040UL, OBGPU_OP_SOURCE_OVER);
    tSave();
    tClip(0, 0, 100, 100);          /* wider: no change */
    tFill("a clip that changes nothing", 0, 0, 100, 100, 0x20202020UL, OBGPU_OP_PLUS_LIGHTER);
    tRestore();
    tSave();
    tClip(6, 6, 2.5, 3);            /* a half-pixel edge on the target? 2.5 x 2 = 5: whole */
    tFill("a clip on whole pixels after the scale", 0, 0, 100, 100, 0xFF808080UL, OBGPU_OP_COPY);
    tClip(6, 6, 2.25, 3);           /* 4.5 pixels: cairo's */
    tFill("under a fractional clip", 0, 0, 100, 100, 0xFF808080UL, OBGPU_OP_COPY);
    tClip(6, 6, 2, 2);
    tFill("still under it", 0, 0, 100, 100, 0xFF808080UL, OBGPU_OP_COPY);
    tRestore();
    tFill("after the restore", 0, 0, 100, 100, 0x80800080UL, OBGPU_OP_SOURCE_ATOP);
    tSave();
    tClip(30, 30, 1, 1);            /* outside: empty */
    tFill("in an empty clip", 0, 0, 100, 100, 0xFF808080UL, OBGPU_OP_COPY);
    tFill("between pixels in an empty clip", 0.5, 0.5, 100, 100, 0xFF808080UL, OBGPU_OP_COPY);
    tRestore();
    tFill("between pixels, wholly outside the clip", 30.5, 0, 3, 3, 0xFF808080UL, OBGPU_OP_COPY);
    tSave();
    tClip(40.5, 0, 3, 3);           /* between pixels, wholly outside: empty */
    tFill("in a clip between pixels outside the last", 0, 0, 100, 100, 0xFF808080UL, OBGPU_OP_COPY);
    tRestore();
    tSave();
    tMatrix(0, 1, -1, 0, 50, 0);
    tClip(0, 0, 10, 10);
    tMatrix(1, 0, 0, 1, 0, 0);
    tFill("under a turned clip", 0, 0, 100, 100, 0xFF808080UL, OBGPU_OP_COPY);
    tRestore();
    tRestore();
    tFill("back in the first clip", 0, 0, W, H, 0x40004000UL, OBGPU_OP_DESTINATION_OVER);
    tSave();
    obgpu_clip_other(&G);
    cur()->other = 1;
    tFill("under a clip path", 0, 0, W, H, 0xFF000000UL, OBGPU_OP_COPY);
    tRestore();
    tSave();
    obgpu_group(&G);
    cur()->group = 1;
    tFill("in a transparency layer", 0, 0, W, H, 0xFF000000UL, OBGPU_OP_COPY);
    obgpu_reset_clip(&G);
    cur()->cx0 = cur()->cy0 = 0; cur()->cx1 = W; cur()->cy1 = H;
    tFill("still in it after a reset clip", 0, 0, W, H, 0xFF000000UL, OBGPU_OP_COPY);
    tRestore();
    tFill("out of the layer", 2, 2, 5, 5, 0xFF000000UL, OBGPU_OP_DESTINATION_OUT);
    tRestore();
    tFill("no clip", 0, 60, W, 4, 0xFFFF0000UL, OBGPU_OP_COPY);
    verify("clips, save and restore");
}

static void testDepth(void)
{
    int i;
    reset(OBGPU_CAN_PD | OBGPU_CAN_PATH, 4096, 1024);
    tClip(1, 1, 50, 50);
    for (i = 0; i < OBGPU_DEPTH + 6; i++) {
        tSave();
        tMatrix(1, 0, 0, 1, i, 0);
    }
    tFill("past the saves kept", 0, 0, 4, 4, 0xFF000000UL, OBGPU_OP_COPY);
    for (i = 0; i < 7; i++)
        tRestore();
    tFill("back within them", 0, 0, 4, 4, 0xFF000000UL, OBGPU_OP_COPY);
    for (i = 0; i < OBGPU_DEPTH; i++)
        tRestore();
    tRestore();     /* one too many: nothing */
    tFill("at the bottom", 0, 0, 4, 4, 0xFF00FF00UL, OBGPU_OP_COPY);
    verify("saves past the depth");
}

static void testImages(void)
{
    char what[64];
    int i;
    reset(OBGPU_CAN_PD | OBGPU_CAN_PATH, 4096, 1024);
    makePicture(0, 24, 20, OBGPU_ARGB);
    makePicture(1, 16, 12, OBGPU_RGB);
    makePicture(2, 9, 7, OBGPU_ARGB);
    for (i = 0; i < 14; i++) {
        int pd = pdOf(allOps[i]);
        sprintf(what, "picture by op %d", allOps[i]);
        tImage(what, 0, (i % 4) * 24, (i / 4) * 16, 24, 16, 0, 2, 24, 16, allOps[i], 255, 1,
            pd >= 0 && pd != OGPU_PD_CLEAR);
    }
    verify("pictures pixel for pixel, every operator");
    tImage("global alpha", 0, 2, 2, 20, 18, 1, 1, 20, 18, OBGPU_OP_SOURCE_OVER, 128, 1, 1);
    tImage("global alpha, xor", 0, 40, 2, 20, 18, 1, 1, 20, 18, OBGPU_OP_XOR, 77, 0, 1);
    tImage("copy with global alpha", 0, 2, 2, 20, 18, 1, 1, 20, 18, OBGPU_OP_COPY, 128, 1, 0);
    tImage("alpha 0", 0, 2, 2, 20, 18, 1, 1, 20, 18, OBGPU_OP_SOURCE_OVER, 0, 1, 1);
    tImage("RGB", 1, 70, 40, 16, 12, 0, 0, 16, 12, OBGPU_OP_SOURCE_OVER, 255, 1, 1);
    tImage("RGB with alpha", 1, 70, 20, 16, 12, 0, 0, 16, 12, OBGPU_OP_SOURCE_OVER, 200, 1, 1);
    tImage("RGB copy", 1, 50, 40, 16, 12, 0, 0, 16, 12, OBGPU_OP_COPY, 255, 1, 1);
    tImage("partly off the target", 0, -5, 50, 24, 20, 0, 0, 24, 20, OBGPU_OP_SOURCE_OVER, 255, 1, 1);
    tImage("source outside the picture", 0, 0, 0, 24, 20, 2, 0, 24, 20, OBGPU_OP_SOURCE_OVER, 255, 1, 0);
    tImage("flipped destination", 0, 30, 0, -24, 20, 0, 0, 24, 20, OBGPU_OP_SOURCE_OVER, 255, 1, 0);
    tImage("under half a pixel", 0, 30, 0, 0.4, 20, 0, 0, 24, 20, OBGPU_OP_SOURCE_OVER, 255, 1, 1);
    verify("pictures with alpha, edges and fall-backs");
    tSave();
    tClip(10, 10, 50, 40);
    tMatrix(1, 0, 0, 1, 3, 4);
    tImage("translated in a clip", 0, 0, 0, 24, 20, 0, 0, 24, 20, OBGPU_OP_SOURCE_OVER, 255, 1, 1);
    tMatrix(2, 0, 0, 2, 20, 10);
    tImage("2x nearest", 2, 0, 0, 9, 7, 0, 0, 9, 7, OBGPU_OP_SOURCE_OVER, 255, 0, 1);
    tRestore();
    verify("pictures translated and doubled");
    tMatrix(3, 0, 0, 3, 1, 1);
    tImage("3x nearest, with alpha", 2, 0, 0, 9, 7, 0, 0, 9, 7, OBGPU_OP_SOURCE_OVER, 180, 0, 1);
    tMatrix(1, 0, 0, 1, 0, 0);
    tImage("a third, nearest", 0, 60, 10, 8, 6, 0, 0, 24, 18, OBGPU_OP_SOURCE_OVER, 255, 0, 1);
    tImage("a third, add", 0, 60, 30, 8, 6, 0, 0, 24, 18, OBGPU_OP_PLUS_LIGHTER, 255, 0, 1);
    tImage("scaled with no filter given", 0, 60, 10, 8, 6, 0, 0, 24, 18, OBGPU_OP_SOURCE_OVER, 255, -1, 0);
    tImage("scaled copy", 0, 60, 10, 8, 6, 0, 0, 24, 18, OBGPU_OP_COPY, 255, 0, 0);
    verify("pictures scaled, nearest");
    tImage("2x bilinear", 2, 4, 4, 18, 14, 0, 0, 9, 7, OBGPU_OP_SOURCE_OVER, 255, 1, 1);
    tImage("bilinear, a fractional source", 0, 30, 4, 30, 25, 2.5, 1.25, 15, 12.5, OBGPU_OP_SOURCE_OVER, 220, 1, 1);
    tImage("bilinear, atop", 2, 70, 40, 20, 20, 1, 1, 6, 4, OBGPU_OP_SOURCE_ATOP, 255, 1, 1);
    verify("pictures scaled, bilinear");
    tImage("fractional destination", 2, 3.25, 40.25, 9, 7, 0, 0, 9, 7, OBGPU_OP_SOURCE_OVER, 255, 0, 1);
    tImage("fractional, scaled", 2, 30.75, 40.5, 13.5, 10, 0, 0, 9, 7, OBGPU_OP_DESTINATION_OUT, 255, 1, 1);
    verify("pictures between pixels");
    tMatrix(0, 1, -1, 0, 60, 4);
    tImage("a quarter turn", 2, 0, 0, 9, 7, 0, 0, 9, 7, OBGPU_OP_SOURCE_OVER, 255, 0, 1);
    tMatrix(0.8660254, 0.5, -0.5, 0.8660254, 30, 2);
    tImage("turned 30 degrees", 0, 0, 0, 24, 20, 0, 0, 24, 20, OBGPU_OP_SOURCE_OVER, 255, 0, 1);
    tMatrix(1, 0, 0, 1, 0, 0);
    verify("pictures turned");
    /* no PATH_FILL: only pixel for pixel */
    reset(OBGPU_CAN_PD, 4096, 1024);
    tImage("pixel for pixel without paths", 0, 0, 0, 24, 20, 0, 0, 24, 20, OBGPU_OP_SOURCE_OVER, 255, 1, 1);
    tImage("scaled without paths", 0, 30, 0, 48, 40, 0, 0, 24, 20, OBGPU_OP_SOURCE_OVER, 255, 1, 0);
    verify("an OpenGPU with no paths");
}

static void testBatches(void)
{
    char what[64];
    int i;
    long before;
    /* a short stream: many batches, all drawn in order */
    reset(OBGPU_CAN_PD | OBGPU_CAN_PATH, 64, 1024);
    for (i = 0; i < 40; i++) {
        sprintf(what, "fill %d of a short stream", i);
        tFill(what, (i * 7) % (W - 8), (i * 5) % (H - 8), 8, 8, i & 1 ? 0x80402010UL : 0xFF000000UL | (unsigned long)i * 0x060504UL,
            OBGPU_OP_SOURCE_OVER);
    }
    verify("a short stream");
    checks++;
    if (submitted < 5) {
        printf("FAIL a short stream: %ld batches\n", submitted);
        failures++;
    }
    /* more pictures than slots: bound again in the batch */
    reset(OBGPU_CAN_PD | OBGPU_CAN_PATH, 4096, 1024);
    for (i = 0; i < NPICTURES; i++)
        makePicture(i, 6, 5, OBGPU_ARGB);
    for (i = 0; i < 3 * NPICTURES; i++) {
        int k = (i * 7) % NPICTURES;
        sprintf(what, "picture %d of many", k);
        tImage(what, k, (i % 12) * 8, (i / 12) * 8 + (i & 1), 6, 5, 0, 0, 6, 5, OBGPU_OP_SOURCE_OVER, i & 1 ? 255 : 150, 0, 1);
    }
    verify("more pictures than slots");
    expectCount("more pictures than slots: batches", submitted, 1);
    /* scratch for two patterns only: a batch each two */
    reset(OBGPU_CAN_PD | OBGPU_CAN_PATH, 4096, 48);
    makePicture(0, 8, 8, OBGPU_ARGB);
    for (i = 0; i < 6; i++) {
        sprintf(what, "scaled picture %d, short scratch", i);
        tImage(what, 0, i * 16, 4, 16, 16, 0, 0, 8, 8, OBGPU_OP_SOURCE_OVER, 255, 0, 1);
    }
    verify("short scratch");
    expectCount("short scratch: batches", submitted, 3);
    /* a failed batch: everything to cairo after it */
    reset(OBGPU_CAN_PD | OBGPU_CAN_PATH, 4096, 1024);
    obgpu_work(&G, ARENA - 64, WORK_BYTES);     /* work memory the core can't map */
    makePicture(0, 8, 8, OBGPU_ARGB);
    before = submitted;
    obgpu_image(&G, &pic[0], (const long[]){ 0, 0, 16 * 256, 16 * 256 }, (const long[]){ 0, 0, 8 * 256, 8 * 256 },
        OBGPU_OP_SOURCE_OVER, 255, 0);
    obgpu_flush(&G);
    expectCount("a failed batch sent", submitted - before, 1);
    expectResult("after a failed batch", obgpu_fill(&G, 0, 0, 256, 256, 0xFF000000UL, OBGPU_OP_COPY), 0);
    expectResult("drawable after a failed batch", obgpu_drawable(&G), 0);
    /* no COMPOSITE_PD: nothing drawn here */
    reset(0, 4096, 1024);
    expectResult("no COMPOSITE_PD", obgpu_fill(&G, 0, 0, 256, 256, 0xFF000000UL, OBGPU_OP_COPY), 0);
    coreErrors = 0;
}

int main(void)
{
    int i;
    testFills();
    testTransforms();
    testClips();
    testDepth();
    testImages();
    testBatches();
    for (i = 0; i < OBGPU_KINDS; i++)
        if (!obgpu_kind_name(i)[0])
            failures++;
    if (failures) {
        printf("test_opengpu_paint: %d of %d checks failed\n", failures, checks);
        return 1;
    }
    printf("test_opengpu_paint: %d checks passed\n", checks);
    return 0;
}
