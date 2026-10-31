/*
 * The engine's GraphicsContextOpenGPU stream writer (WebKit patch 0076's
 * OpenGPUPaintAmiga.c, with 0077's paths) run on openamigartg's CPU core
 * (ogpu_core.c), and the pixels compared with a reference written here
 * from stream.h's Porter-Duff formulas and cairo's rules for each call:
 * fills by every operator WebKit uses, nested clips with save and restore,
 * pictures with alpha, global alpha, translate and scale, paths filled and
 * stroked (caps, joins, miter limits, dashes, dots) with colours and
 * linear and radial gradients against shapes sampled 256 times a pixel,
 * and the states and transforms that must go to cairo (the call returns 0
 * and draws nothing).
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

static void refRect(double x, double y, double w, double h, unsigned long argb, int pd);

static void tFill(const char *what, double x, double y, double w, double h, unsigned long argb, int op)
{
    struct mlevel *l = cur();
    double e[4];
    int pd = pdOf(op), want = 1, px, py, r, asPath = 0;
    r = obgpu_fill(&G, fx8(x), fx8(y), fx8(w), fx8(h), argb, op);
    if (!mdrawable() || pd < 0)
        want = 0;
    else if (!devRect(l, x, y, w, h, e) || !edgesWhole(l, e)) {
        /* off whole pixels, or turned: a path, by the operators that leave the outside alone */
        asPath = 1;
        if (!(G.caps & OBGPU_CAN_PATH) || pd == OGPU_PD_CLEAR || pd == OGPU_PD_SRC)
            want = 0;
    }
    expectResult(what, r, want);
    if (!r)
        handedOn();
    if (!want)
        return;
    if (asPath) {
        if (w && h)
            refRect(x, y, w, h, argb, pd);
        return;
    }
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

/* ---- paths: the reference ---------------------------------------------------------------- */

/* A shape in the user's space, drawn through the model's transform: the
 * union of filled contours (non-zero or even-odd), convex polygons and
 * discs and capsules (the points within r of a segment). */
#define REF_POINTS  8192
#define REF_PIECES  1024
static double refPt[REF_POINTS][2];
static int refContourAt[REF_PIECES], refContours, refPoints, refEvenOdd;
static int refConvexAt[REF_PIECES], refConvexN[REF_PIECES], refConvexes;
static double refCap[REF_PIECES][5];
static int refCaps, refCurved, refCrossing;

static void refReset(int evenodd)
{
    refContours = refPoints = refConvexes = refCaps = refCurved = 0;
    refEvenOdd = evenodd;
}

static int refPoint(double x, double y)
{
    if (refPoints >= REF_POINTS) {
        printf("FAIL the reference: too many points\n");
        exit(1);
    }
    refPt[refPoints][0] = x;
    refPt[refPoints][1] = y;
    return refPoints++;
}

static void refContourStart(void)
{
    refContourAt[refContours++] = refPoints;
}

static void refConvex(const double *p, int n)
{
    int i;
    refConvexAt[refConvexes] = refPoints;
    refConvexN[refConvexes++] = n;
    for (i = 0; i < n; i++)
        refPoint(p[2 * i], p[2 * i + 1]);
}

static void refCapsule(double x0, double y0, double x1, double y1, double r)
{
    refCap[refCaps][0] = x0; refCap[refCaps][1] = y0;
    refCap[refCaps][2] = x1; refCap[refCaps][3] = y1;
    refCap[refCaps++][4] = r;
    refCurved = 1;
}

static int refInsideUser(double x, double y)
{
    int i, j, wind = 0;
    for (i = 0; i < refContours; i++) {
        int a = refContourAt[i], b = i + 1 < refContours ? refContourAt[i + 1] : refConvexes ? refConvexAt[0] : refPoints;
        for (j = a; j < b; j++) {
            const double *p = refPt[j], *q = refPt[j + 1 < b ? j + 1 : a];
            if (p[1] <= y && q[1] > y && (q[0] - p[0]) * (y - p[1]) - (x - p[0]) * (q[1] - p[1]) > 0)
                wind++;
            else if (p[1] > y && q[1] <= y && (q[0] - p[0]) * (y - p[1]) - (x - p[0]) * (q[1] - p[1]) < 0)
                wind--;
        }
    }
    if (refEvenOdd ? (wind & 1) : wind != 0)
        return 1;
    for (i = 0; i < refConvexes; i++) {
        int a = refConvexAt[i], n = refConvexN[i], pos = 0, neg = 0;
        for (j = 0; j < n; j++) {
            const double *p = refPt[a + j], *q = refPt[a + (j + 1) % n];
            double c = (q[0] - p[0]) * (y - p[1]) - (x - p[0]) * (q[1] - p[1]);
            pos += c > 0;
            neg += c < 0;
        }
        if (!pos || !neg)
            if (n >= 3)
                return 1;
    }
    for (i = 0; i < refCaps; i++) {
        const double *c = refCap[i];
        double dx = c[2] - c[0], dy = c[3] - c[1], l2 = dx * dx + dy * dy, t = 0, ex, ey;
        if (l2 > 0) {
            t = ((x - c[0]) * dx + (y - c[1]) * dy) / l2;
            t = t < 0 ? 0 : t > 1 ? 1 : t;
        }
        ex = x - (c[0] + t * dx);
        ey = y - (c[1] + t * dy);
        if (ex * ex + ey * ey <= c[4] * c[4])
            return 1;
    }
    return 0;
}

static double refInv[6];

static int refInside(double x, double y)
{
    double ux, uy;
    devX(refInv, x, y, &ux, &uy);
    return refInsideUser(ux, uy);
}

static void inverse(const double *m, double *inv)
{
    double det = m[0] * m[3] - m[1] * m[2];
    inv[0] = m[3] / det; inv[1] = -m[1] / det; inv[2] = -m[2] / det; inv[3] = m[0] / det;
    inv[4] = -(inv[0] * m[4] + inv[2] * m[5]);
    inv[5] = -(inv[1] * m[4] + inv[3] * m[5]);
}

/* A pixel's coverage (0-255), 16 x 16 samples, and whether the shape's edge
 * crosses it (then the area is only so near). */
#define SS 16
static int refCoverage(int px, int py, int *edge)
{
    int i, j, n = 0, first = refInside(px + 0.5 / SS, py + 0.5 / SS), differ = 0;
    for (j = 0; j < SS; j++)
        for (i = 0; i < SS; i++) {
            int in = refInside(px + (i + 0.5) / SS, py + (j + 0.5) / SS);
            n += in;
            differ |= in != first;
        }
    for (i = 0; i <= SS && !differ; i++) {
        differ |= refInside(px + (double)i / SS, py) != first || refInside(px + (double)i / SS, py + 1) != first
            || refInside(px, py + (double)i / SS) != first || refInside(px + 1, py + (double)i / SS) != first;
    }
    *edge = differ;
    return (n * 255 + SS * SS / 2) / (SS * SS);
}

/* ---- gradients: the reference ------------------------------------------------------------ */

struct tgrad {
    int kind, spread, stops;
    double m[6];                /* its space to the user's */
    double geo[6];
    double offset[16];
    unsigned long argb[16];
};

static double spreadT(int spread, double t)
{
    if (spread == OBGPU_REPEAT)
        return t - floor(t);
    if (spread == OBGPU_REFLECT) {
        t = fmod(t, 2);
        if (t < 0)
            t += 2;
        return t > 1 ? 2 - t : t;
    }
    return t < 0 ? 0 : t > 1 ? 1 : t;
}

/* The gradient's colour at a point of the target; 0 where there is no t. */
static struct px gradAt(const struct tgrad *gr, double x, double y)
{
    double inv[6], ux, uy, gx, gy, t, gi[6];
    struct px o = { 0, 0, 0, 0 };
    int i;
    devX(refInv, x, y, &ux, &uy);
    inverse(gr->m, gi);
    devX(gi, ux, uy, &gx, &gy);
    (void)inv;
    if (gr->kind == OBGPU_LINEAR) {
        double dx = gr->geo[2] - gr->geo[0], dy = gr->geo[3] - gr->geo[1];
        t = ((gx - gr->geo[0]) * dx + (gy - gr->geo[1]) * dy) / (dx * dx + dy * dy);
    } else {
        double cdx = gr->geo[3] - gr->geo[0], cdy = gr->geo[4] - gr->geo[1], dr = gr->geo[5] - gr->geo[2];
        double pdx = gx - gr->geo[0], pdy = gy - gr->geo[1], r0 = gr->geo[2];
        double a = cdx * cdx + cdy * cdy - dr * dr, b = pdx * cdx + pdy * cdy + r0 * dr, c = pdx * pdx + pdy * pdy - r0 * r0;
        if (fabs(a) < 1e-9) {
            if (fabs(b) < 1e-9)
                return o;
            t = c / (2 * b);
            if (r0 + t * dr < 0)
                return o;
        } else {
            double disc = b * b - a * c, t1, t2;
            if (disc < 0)
                return o;
            t1 = (b + sqrt(disc)) / a;
            t2 = (b - sqrt(disc)) / a;
            if (t2 > t1) { double s = t1; t1 = t2; t2 = s; }
            if (r0 + t1 * dr >= 0)
                t = t1;
            else if (r0 + t2 * dr >= 0)
                t = t2;
            else
                return o;
        }
    }
    t = spreadT(gr->spread, t);
    if (t <= gr->offset[0])
        return colour(gr->argb[0]);
    for (i = 1; i < gr->stops; i++)
        if (t < gr->offset[i] || i == gr->stops - 1) {
            double f, span = gr->offset[i] - gr->offset[i - 1];
            struct px a = colour(gr->argb[i - 1]), b = colour(gr->argb[i]);
            if (t >= gr->offset[i])
                return b;
            f = span > 0 ? (t - gr->offset[i - 1]) / span : 1;
            o.a = (int)floor(a.a + (b.a - a.a) * f + 0.5);
            o.r = (int)floor(a.r + (b.r - a.r) * f + 0.5);
            o.g = (int)floor(a.g + (b.g - a.g) * f + 0.5);
            o.b = (int)floor(a.b + (b.b - a.b) * f + 0.5);
            return o;
        }
    return colour(gr->argb[gr->stops - 1]);
}

static int maxDiff(struct px a, struct px b)
{
    int d = abs(a.a - b.a), v;
    if ((v = abs(a.r - b.r)) > d) d = v;
    if ((v = abs(a.g - b.g)) > d) d = v;
    if ((v = abs(a.b - b.b)) > d) d = v;
    return d;
}

/* The writer's gradient for a test one. */
static unsigned long gradStops[32];
static struct obgpu_gradient writerGradient(const struct tgrad *t)
{
    struct obgpu_gradient g;
    int i;
    g.kind = t->kind;
    g.spread = t->spread;
    g.m[0] = fx16(t->m[0]); g.m[1] = fx16(t->m[1]); g.m[2] = fx16(t->m[2]); g.m[3] = fx16(t->m[3]);
    g.m[4] = fx8(t->m[4]); g.m[5] = fx8(t->m[5]);
    for (i = 0; i < 6; i++)
        g.geometry[i] = fx16(t->geo[i]);
    for (i = 0; i < t->stops; i++) {
        gradStops[2 * i] = (unsigned long)fx16(t->offset[i]);
        gradStops[2 * i + 1] = t->argb[i];
    }
    g.stops = t->stops;
    g.stop = gradStops;
    return g;
}

/* The expected pixels of the reference shape painted with a colour or a
 * gradient by an operator: each pixel inside the clip, its coverage times
 * the paint, combined. */
static void refPaint(unsigned long argb, const struct tgrad *gr, int pd)
{
    struct mlevel *l = cur();
    double b[4] = { 1e9, 1e9, -1e9, -1e9 }, grow = 1, x, y;
    int px, py, i;
    inverse(l->m, refInv);
    /* the shape's box on the target: only the pixels in it are sampled */
    for (i = 0; i < refPoints + 2 * refCaps; i++) {
        const double *q = i < refPoints ? refPt[i] : &refCap[(i - refPoints) / 2][(i - refPoints) % 2 * 2];
        devX(l->m, q[0], q[1], &x, &y);
        if (x < b[0]) b[0] = x;
        if (y < b[1]) b[1] = y;
        if (x > b[2]) b[2] = x;
        if (y > b[3]) b[3] = y;
        if (i >= refPoints)
            grow = fmax(grow, 1 + refCap[(i - refPoints) / 2][4] * (fabs(l->m[0]) + fabs(l->m[1]) + fabs(l->m[2]) + fabs(l->m[3])));
    }
    for (py = 0; py < H; py++)
        for (px = 0; px < W; px++) {
            int edge, cov, t = 2;
            struct px p;
            if (px + 0.5 < l->cx0 || px + 0.5 > l->cx1 || py + 0.5 < l->cy0 || py + 0.5 > l->cy1)
                continue;
            if (px + 1 < b[0] - grow || px > b[2] + grow || py + 1 < b[1] - grow || py > b[3] + grow)
                continue;
            cov = refCoverage(px, py, &edge);
            if (!cov && !edge)
                continue;
            if (gr) {
                struct px c[5];
                int k, d = 0;
                c[0] = gradAt(gr, px + 0.5, py + 0.5);
                c[1] = gradAt(gr, px + 0.25, py + 0.5);
                c[2] = gradAt(gr, px + 0.75, py + 0.5);
                c[3] = gradAt(gr, px + 0.5, py + 0.25);
                c[4] = gradAt(gr, px + 0.5, py + 0.75);
                for (k = 1; k < 5; k++)
                    if (maxDiff(c[0], c[k]) > d)
                        d = maxDiff(c[0], c[k]);
                if (d > 24) {           /* a sharp change: either side */
                    skip[py][px] = 1;
                    continue;
                }
                p = c[0];
                t = 3 + d;
            } else
                p = colour(argb);
            /* the area near an edge: sampled here, and OpenGPU's curves cut within 1/16 pixel */
            if (edge)
                t += refCurved ? 22 : 10;
            /* where a path crosses itself inside a pixel, OpenGPU's CPU core
             * finds the winding per quarter of its height */
            if (edge && refCrossing)
                t += 64;
            if (tol[py][px] < t)
                tol[py][px] = (unsigned char)t;
            store(&expect[py][px * 4], combine(pd, scale(p, cov), load(&expect[py][px * 4], 0)));
        }
}

/* ---- paths: the calls, on the writer and the reference ----------------------------------- */

/* The path as elements, in the user's space. */
struct el { int v; double p[6]; };
static struct el path[512];
static int npath;

static void pM(double x, double y) { path[npath].v = OGPU_PATH_MOVE; path[npath].p[0] = x; path[npath++].p[1] = y; }
static void pL(double x, double y) { path[npath].v = OGPU_PATH_LINE; path[npath].p[0] = x; path[npath++].p[1] = y; }
static void pQ(double x1, double y1, double x, double y)
{
    struct el *e = &path[npath++];
    e->v = OGPU_PATH_QUAD; e->p[0] = x1; e->p[1] = y1; e->p[2] = x; e->p[3] = y;
}
static void pC(double x1, double y1, double x2, double y2, double x, double y)
{
    struct el *e = &path[npath++];
    e->v = OGPU_PATH_CUBIC; e->p[0] = x1; e->p[1] = y1; e->p[2] = x2; e->p[3] = y2; e->p[4] = x; e->p[5] = y;
}
static void pZ(void) { path[npath++].v = OGPU_PATH_CLOSE; }

static void pEllipse(double cx, double cy, double rx, double ry)
{
    const double k = 0.5522847498;
    pM(cx + rx, cy);
    pC(cx + rx, cy + k * ry, cx + k * rx, cy + ry, cx, cy + ry);
    pC(cx - k * rx, cy + ry, cx - rx, cy + k * ry, cx - rx, cy);
    pC(cx - rx, cy - k * ry, cx - k * rx, cy - ry, cx, cy - ry);
    pC(cx + k * rx, cy - ry, cx + rx, cy - k * ry, cx + rx, cy);
    pZ();
}

/* The path to the writer: begin's answer, and 0 if an element did not fit. */
static int sendPath(int *fitted)
{
    int i, ok = 1;
    if (!obgpu_path_begin(&G)) {
        *fitted = 1;
        return 0;
    }
    for (i = 0; i < npath; i++) {
        const struct el *e = &path[i];
        switch (e->v) {
        case OGPU_PATH_MOVE: ok &= obgpu_path_move(&G, fx8(e->p[0]), fx8(e->p[1])); break;
        case OGPU_PATH_LINE: ok &= obgpu_path_line(&G, fx8(e->p[0]), fx8(e->p[1])); break;
        case OGPU_PATH_QUAD:
            ok &= obgpu_path_quad(&G, fx8(e->p[0]), fx8(e->p[1]), fx8(e->p[2]), fx8(e->p[3]));
            break;
        case OGPU_PATH_CUBIC:
            ok &= obgpu_path_cubic(&G, fx8(e->p[0]), fx8(e->p[1]), fx8(e->p[2]), fx8(e->p[3]), fx8(e->p[4]), fx8(e->p[5]));
            break;
        default: ok &= obgpu_path_close(&G); break;
        }
    }
    *fitted = ok;
    return 1;
}

/* The path's subpaths as polylines (curves cut into 64 lines), each closed
 * or not, as the writer reads it: a line after a close starts where the
 * closed subpath started. */
#define MAXSUB 64
static double poly[MAXSUB][1200][2];
static int polyN[MAXSUB], polyClosed[MAXSUB], polyDrawn[MAXSUB], polys;

static void polyAdd(double x, double y)
{
    poly[polys - 1][polyN[polys - 1]][0] = x;
    poly[polys - 1][polyN[polys - 1]++][1] = y;
}

static void flatten(void)
{
    double cx = 0, cy = 0, sx = 0, sy = 0;
    int i, k, closed = 0;
    polys = 0;
    for (i = 0; i < npath; i++) {
        const struct el *e = &path[i];
        if (e->v == OGPU_PATH_MOVE || ((closed || !polys) && e->v != OGPU_PATH_CLOSE)) {
            if (e->v == OGPU_PATH_MOVE) {
                sx = e->p[0];
                sy = e->p[1];
            }
            polys++;
            polyN[polys - 1] = 0;
            polyClosed[polys - 1] = 0;
            polyDrawn[polys - 1] = 0;
            cx = sx;
            cy = sy;
            polyAdd(cx, cy);
            closed = 0;
            if (e->v == OGPU_PATH_MOVE)
                continue;
        }
        polyDrawn[polys - 1] = 1;
        switch (e->v) {
        case OGPU_PATH_LINE:
            cx = e->p[0]; cy = e->p[1];
            polyAdd(cx, cy);
            break;
        case OGPU_PATH_QUAD:
            refCurved = 1;
            for (k = 1; k <= 64; k++) {
                double t = k / 64.0, u = 1 - t;
                polyAdd(u * u * cx + 2 * u * t * e->p[0] + t * t * e->p[2], u * u * cy + 2 * u * t * e->p[1] + t * t * e->p[3]);
            }
            cx = e->p[2]; cy = e->p[3];
            break;
        case OGPU_PATH_CUBIC:
            refCurved = 1;
            for (k = 1; k <= 64; k++) {
                double t = k / 64.0, u = 1 - t;
                polyAdd(u * u * u * cx + 3 * u * u * t * e->p[0] + 3 * u * t * t * e->p[2] + t * t * t * e->p[4],
                    u * u * u * cy + 3 * u * u * t * e->p[1] + 3 * u * t * t * e->p[3] + t * t * t * e->p[5]);
            }
            cx = e->p[4]; cy = e->p[5];
            break;
        case OGPU_PATH_CLOSE:
            if (polys)
                polyClosed[polys - 1] = 1;
            closed = 1;
            cx = sx; cy = sy;
            break;
        }
    }
}

/* The reference of the path filled. */
static void refFillPath(int evenodd)
{
    int s, i;
    refReset(evenodd);
    flatten();
    for (s = 0; s < polys; s++) {
        refContourStart();
        for (i = 0; i < polyN[s]; i++)
            refPoint(poly[s][i][0], poly[s][i][1]);
    }
}

/* The reference of the path stroked: each piece's segments as quads, its
 * joins and caps; dashes cut the subpaths into pieces first. */
struct tstroke { double width; int cap, join; double miter; int dashes; double dash[8]; double offset; };

static void quadOf(const double *a, const double *b, double hw, double ext0, double ext1)
{
    double dx = b[0] - a[0], dy = b[1] - a[1], l = sqrt(dx * dx + dy * dy), nx, ny, ux, uy, q[8];
    if (l <= 0)
        return;
    ux = dx / l; uy = dy / l;
    nx = -uy * hw; ny = ux * hw;
    q[0] = a[0] - ux * ext0 + nx; q[1] = a[1] - uy * ext0 + ny;
    q[2] = b[0] + ux * ext1 + nx; q[3] = b[1] + uy * ext1 + ny;
    q[4] = b[0] + ux * ext1 - nx; q[5] = b[1] + uy * ext1 - ny;
    q[6] = a[0] - ux * ext0 - nx; q[7] = a[1] - uy * ext0 - ny;
    refConvex(q, 4);
}

static void joinAt(const double *a, const double *v, const double *b, const struct tstroke *s)
{
    double hw = s->width / 2, d0x = v[0] - a[0], d0y = v[1] - a[1], d1x = b[0] - v[0], d1y = b[1] - v[1];
    double l0 = sqrt(d0x * d0x + d0y * d0y), l1 = sqrt(d1x * d1x + d1y * d1y), cross, side, n0x, n0y, n1x, n1y, t[8];
    if (l0 <= 0 || l1 <= 0)
        return;
    d0x /= l0; d0y /= l0; d1x /= l1; d1y /= l1;
    if (s->join == OBGPU_JOIN_ROUND) {
        refCapsule(v[0], v[1], v[0], v[1], hw);
        return;
    }
    cross = d0x * d1y - d0y * d1x;
    if (fabs(cross) < 1e-12)
        return;
    side = cross > 0 ? -1 : 1;      /* the outer side's normal */
    n0x = -d0y * hw * side; n0y = d0x * hw * side;
    n1x = -d1y * hw * side; n1y = d1x * hw * side;
    t[0] = v[0]; t[1] = v[1];
    t[2] = v[0] + n0x; t[3] = v[1] + n0y;
    if (s->join == OBGPU_JOIN_MITER) {
        double cosT = -(d0x * d1x + d0y * d1y), sinHalf = sqrt((1 - cosT) / 2);
        if (sinHalf > 0 && 1 / sinHalf <= s->miter) {
            /* the tip: where the two outer edges meet */
            double mx = n0x + n1x, my = n0y + n1y, ml = sqrt(mx * mx + my * my), len = hw / sinHalf;
            t[4] = v[0] + mx / ml * len; t[5] = v[1] + my / ml * len;
            t[6] = v[0] + n1x; t[7] = v[1] + n1y;
            refConvex(t, 4);
            return;
        }
    }
    t[4] = v[0] + n1x; t[5] = v[1] + n1y;
    refConvex(t, 3);
}

/* A piece: an open or closed polyline. */
static void strokePiece(double (*p)[2], int n, int closed, const struct tstroke *s)
{
    double hw = s->width / 2;
    int i, one = 1;
    for (i = 1; i < n; i++)
        one &= p[i][0] == p[0][0] && p[i][1] == p[0][1];
    if (one) {
        /* as cairo: a dot, or a square along x */
        if (s->cap == OBGPU_CAP_ROUND)
            refCapsule(p[0][0], p[0][1], p[0][0], p[0][1], hw);
        else if (s->cap == OBGPU_CAP_SQUARE) {
            double q[8];
            q[0] = p[0][0] - hw; q[1] = p[0][1] - hw; q[2] = p[0][0] + hw; q[3] = p[0][1] - hw;
            q[4] = p[0][0] + hw; q[5] = p[0][1] + hw; q[6] = p[0][0] - hw; q[7] = p[0][1] + hw;
            refConvex(q, 4);
        }
        return;
    }
    for (i = 0; i + 1 < n; i++) {
        double e0 = 0, e1 = 0;
        if (!closed && s->cap == OBGPU_CAP_SQUARE) {
            e0 = i == 0 ? hw : 0;
            e1 = i + 2 == n ? hw : 0;
        }
        quadOf(p[i], p[i + 1], hw, e0, e1);
    }
    if (closed) {
        quadOf(p[n - 1], p[0], hw, 0, 0);
        joinAt(p[n - 1], p[0], p[1], s);
        joinAt(p[n - 2], p[n - 1], p[0], s);
    }
    for (i = 1; i + 1 < n; i++)
        joinAt(p[i - 1], p[i], p[i + 1], s);
    if (!closed && s->cap == OBGPU_CAP_ROUND) {
        refCapsule(p[0][0], p[0][1], p[0][0], p[0][1], hw);
        refCapsule(p[n - 1][0], p[n - 1][1], p[n - 1][0], p[n - 1][1], hw);
    }
}

/* Dashing: the piece being cut, and a closed subpath's first piece. */
static double piece[1200][2], firstPiece[1200][2];
static int npiece, nfirst;

static void pieceAdd(double (*p)[2], int *n, double x, double y)
{
    if (*n && p[*n - 1][0] == x && p[*n - 1][1] == y)
        return;
    p[*n][0] = x;
    p[*n][1] = y;
    ++*n;
}

static void refStrokePath(const struct tstroke *s)
{
    int sp, i;
    refReset(0);
    flatten();
    for (sp = 0; sp < polys; sp++) {
        double (*p)[2] = poly[sp];
        int n = polyN[sp], closed = polyClosed[sp], di = 0, on = 1, count, startOn, cut = 0;
        double total = 0, off, rem;
        if (!polyDrawn[sp])
            continue;           /* a move alone: nothing */
        if (closed && n > 1 && p[n - 1][0] == p[0][0] && p[n - 1][1] == p[0][1])
            n--;
        if (!s->dashes) {
            strokePiece(p, n, closed, s);
            continue;
        }
        /* an odd count used twice; the pattern from the offset */
        count = s->dashes & 1 ? 2 * s->dashes : s->dashes;
        for (i = 0; i < count; i++)
            total += s->dash[i % s->dashes];
        off = fmod(s->offset, total);
        if (off < 0)
            off += total;
        while (off > 0 && off >= s->dash[di % s->dashes]) {    /* cairo's: a first dash of no length stays */
            off -= s->dash[di % s->dashes];
            di = (di + 1) % count;
            on ^= 1;
        }
        rem = s->dash[di % s->dashes] - off;
        startOn = on;
        npiece = nfirst = 0;
        if (on)
            pieceAdd(piece, &npiece, p[0][0], p[0][1]);
        for (i = 0; i < n - 1 + closed; i++) {
            const double *a = p[i], *b = p[(i + 1) % n];
            double dx = b[0] - a[0], dy = b[1] - a[1], l = sqrt(dx * dx + dy * dy), at = 0;
            while (l - at >= rem) {
                double x, y;
                at += rem;
                x = a[0] + dx * at / l;
                y = a[1] + dy * at / l;
                if (on) {
                    pieceAdd(piece, &npiece, x, y);
                    if (closed && startOn && !cut) {
                        memcpy(firstPiece, piece, sizeof piece[0] * (size_t)npiece);
                        nfirst = npiece;
                    } else
                        strokePiece(piece, npiece, 0, s);
                    npiece = 0;
                } else {
                    npiece = 0;
                    pieceAdd(piece, &npiece, x, y);
                }
                cut = 1;
                on ^= 1;
                di = (di + 1) % count;
                rem = s->dash[di % s->dashes];
            }
            rem -= l - at;
            if (on)
                pieceAdd(piece, &npiece, b[0], b[1]);
        }
        if (closed && startOn && !cut) {
            strokePiece(p, n, 1, s);            /* one dash all the way round */
            continue;
        }
        if (on && npiece && closed && startOn) {
            for (i = 0; i < nfirst; i++)        /* the last piece runs on into the first */
                pieceAdd(piece, &npiece, firstPiece[i][0], firstPiece[i][1]);
            strokePiece(piece, npiece, 0, s);
            continue;
        }
        if (on && npiece)
            strokePiece(piece, npiece, 0, s);
        if (nfirst)
            strokePiece(firstPiece, nfirst, 0, s);
    }
}

/* want: as pathDraw decides. */
static int pathWanted(int op, const struct tgrad *gr)
{
    int pd = pdOf(op);
    (void)gr;
    return mdrawable() && (G.caps & OBGPU_CAN_PATH) && pd >= 0 && pd != OGPU_PD_CLEAR && pd != OGPU_PD_SRC;
}

static void tPathFill(const char *what, int evenodd, unsigned long argb, const struct tgrad *gr, int op, int want)
{
    struct obgpu_gradient wg;
    int fitted, r;
    if (gr)
        wg = writerGradient(gr);
    sendPath(&fitted);
    r = obgpu_fill_path(&G, argb, gr ? &wg : 0, evenodd, op);
    if (want < 0)
        want = pathWanted(op, gr) && fitted;
    expectResult(what, r, want);
    if (!r)
        handedOn();
    if (!want)
        return;
    refFillPath(evenodd);
    refPaint(argb, gr, pdOf(op));
}

static void tPathStroke(const char *what, const struct tstroke *s, unsigned long argb, const struct tgrad *gr, int op, int want)
{
    struct obgpu_gradient wg;
    struct obgpu_stroke ws;
    long dash[8];
    int fitted, r, i;
    if (gr)
        wg = writerGradient(gr);
    ws.width = fx16(s->width);
    ws.cap = s->cap;
    ws.join = s->join;
    ws.miter = fx16(s->miter);
    ws.dashes = s->dashes;
    for (i = 0; i < s->dashes; i++)
        dash[i] = fx16(s->dash[i]);
    ws.dash = dash;
    ws.dash_offset = fx16(s->offset);
    sendPath(&fitted);
    r = obgpu_stroke_path(&G, argb, gr ? &wg : 0, &ws, op);
    if (want < 0)
        want = pathWanted(op, gr) && fitted;
    expectResult(what, r, want);
    if (!r)
        handedOn();
    if (!want || s->width <= 0)
        return;
    refStrokePath(s);
    refPaint(argb, gr, pdOf(op));
}

static void refRect(double x, double y, double w, double h, unsigned long argb, int pd)
{
    npath = 0;
    pM(x, y); pL(x + w, y); pL(x + w, y + h); pL(x, y + h); pZ();
    refFillPath(0);
    refPaint(argb, 0, pd);
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

static void testPathFills(void)
{
    static const int pathOps[6] = {
        OBGPU_OP_SOURCE_OVER, OBGPU_OP_SOURCE_ATOP, OBGPU_OP_DESTINATION_OVER, OBGPU_OP_DESTINATION_OUT, OBGPU_OP_XOR,
        OBGPU_OP_PLUS_LIGHTER
    };
    char what[64];
    int i, rule;
    long before;
    reset(OBGPU_CAN_PD | OBGPU_CAN_PATH, 4096, 1024);
    /* a five-pointed star crosses itself: its middle is in by non-zero, out by even-odd */
    for (rule = 0; rule < 2; rule++) {
        npath = 0;
        pM(20 + rule * 48, 2);
        pL(31.7 + rule * 48, 37.5);
        pL(1.5 + rule * 48, 15.5);
        pL(38.5 + rule * 48, 15.5);
        pL(8.3 + rule * 48, 37.5);
        pZ();
        refCrossing = 1;
        tPathFill(rule ? "a star, even-odd" : "a star, non-zero", rule, 0xFF3060C0UL, 0, OBGPU_OP_SOURCE_OVER, -1);
        refCrossing = 0;
    }
    /* curves: an ellipse of cubics, a quadratic lens, a ring of two circles */
    npath = 0;
    pEllipse(20.3, 50.2, 15.6, 9.1);
    tPathFill("an ellipse", 0, 0xC0806000UL, 0, OBGPU_OP_SOURCE_OVER, -1);
    npath = 0;
    pM(45, 44);
    pQ(55, 30, 65, 44);
    pQ(55, 58, 45, 44);
    tPathFill("a quadratic lens, left open", 0, 0x80008080UL, 0, OBGPU_OP_SOURCE_OVER, -1);
    npath = 0;
    pEllipse(82, 48, 12, 12);
    pEllipse(82, 48, 6, 6);
    tPathFill("a ring, even-odd", 1, 0xFF00A000UL, 0, OBGPU_OP_SOURCE_OVER, -1);
    verify("paths filled");
    /* every operator: those that leave the outside alone drawn, the others cairo's */
    for (i = 0; i < 14; i++) {
        sprintf(what, "a triangle by op %d", allOps[i]);
        npath = 0;
        pM(3 + i * 6.5, 3.25);
        pL(8.75 + i * 6.5, 20);
        pL(1 + i * 6.5, 28.5);
        tPathFill(what, 0, 0xA0602040UL, 0, allOps[i], -1);
    }
    for (i = 0; i < 6; i++) {
        sprintf(what, "a circle by op %d", pathOps[i]);
        npath = 0;
        pEllipse(8 + i * 15, 46, 6.5, 6.5);
        tPathFill(what, 0, i & 1 ? 0xFFFFFFFFUL : 0x60301020UL, 0, pathOps[i], -1);
    }
    verify("paths by every operator");
    /* nothing to draw: a transparent colour, an empty path, a path of moves */
    submitted = 0;
    before = (long)counts.draws;
    npath = 0;
    pEllipse(40, 30, 20, 20);
    tPathFill("a transparent colour", 0, 0, 0, OBGPU_OP_SOURCE_OVER, 1);
    npath = 0;
    tPathFill("an empty path", 0, 0xFF000000UL, 0, OBGPU_OP_SOURCE_OVER, 1);
    npath = 0;
    pM(10, 10);
    pM(30, 30);
    tPathFill("moves only", 0, 0xFF000000UL, 0, OBGPU_OP_SOURCE_OVER, 1);
    verify("paths with nothing to draw");
    expectCount("nothing to draw: draws", (long)counts.draws - before, 0);
}

static void testPathStates(void)
{
    char what[64];
    int i;
    reset(OBGPU_CAN_PD | OBGPU_CAN_PATH, 4096, 1024);
    /* under transforms: scaled, turned, flipped */
    tSave();
    tMatrix(2, 0, 0, 1.5, 3.5, 1.25);
    npath = 0;
    pEllipse(8, 8, 6, 6);
    tPathFill("a scaled circle", 0, 0xFFC02020UL, 0, OBGPU_OP_SOURCE_OVER, -1);
    tMatrix(0.8660254, 0.5, -0.5, 0.8660254, 60, 2);
    npath = 0;
    pM(0, 0); pL(24, 0); pL(24, 12); pL(0, 12); pZ();
    tPathFill("a turned rectangle", 0, 0x80204080UL, 0, OBGPU_OP_SOURCE_OVER, -1);
    tMatrix(-1, 0, 0, 1, 40, 30);
    npath = 0;
    pM(2, 2); pC(30, 0, 0, 30, 20, 20); pZ();
    tPathFill("a flipped loop", 1, 0xFF008000UL, 0, OBGPU_OP_SOURCE_OVER, -1);
    tRestore();
    verify("paths under transforms");
    /* clips: inside a rectangle clip; an empty clip; clips only cairo has */
    tSave();
    tClip(10, 10, 30, 20);
    npath = 0;
    pEllipse(25, 20, 20, 15);
    tPathFill("an ellipse in a clip", 0, 0xFF808000UL, 0, OBGPU_OP_SOURCE_OVER, -1);
    tSave();
    tClip(60, 0, 10, 10);
    npath = 0;
    pEllipse(25, 20, 20, 15);
    tPathFill("in an empty clip", 0, 0xFF000000UL, 0, OBGPU_OP_SOURCE_OVER, 1);
    tRestore();
    tSave();
    obgpu_clip_other(&G);
    cur()->other = 1;
    npath = 0;
    pEllipse(25, 20, 5, 5);
    tPathFill("under a clip path", 0, 0xFF000000UL, 0, OBGPU_OP_SOURCE_OVER, -1);
    tRestore();
    tSave();
    obgpu_group(&G);
    cur()->group = 1;
    tPathFill("in a layer", 0, 0xFF000000UL, 0, OBGPU_OP_SOURCE_OVER, -1);
    tRestore();
    tSave();
    tBadMatrix();
    tPathFill("under a transform out of range", 0, 0xFF000000UL, 0, OBGPU_OP_SOURCE_OVER, -1);
    tRestore();
    tRestore();
    /* a translation PATH_FILL's matrix can't take */
    tSave();
    tMatrix(1, 0, 0, 1, 20000, 0);
    npath = 0;
    pEllipse(-19980, 20, 5, 5);
    tPathFill("translated too far for the matrix", 0, 0xFF000000UL, 0, OBGPU_OP_SOURCE_OVER, 0);
    tRestore();
    verify("paths under clips and states");
    /* rectangles off whole pixels, or turned, are paths now */
    for (i = 0; i < 4; i++) {
        sprintf(what, "a rectangle off whole pixels %d", i);
        tFill(what, 3.25 + i * 20, 4.5, 12.6, 7.3 + i, i & 1 ? 0xFF2080A0UL : 0x80402010UL,
            i == 3 ? OBGPU_OP_PLUS_LIGHTER : OBGPU_OP_SOURCE_OVER);
    }
    tFill("copy off whole pixels: cairo's", 10.5, 30, 10, 10, 0xFF000000UL, OBGPU_OP_COPY);
    tFill("clear off whole pixels: cairo's", 30.5, 30, 10, 10, 0, OBGPU_OP_CLEAR);
    tSave();
    tMatrix(0.7071068, 0.7071068, -0.7071068, 0.7071068, 70, 20);
    tFill("a turned rectangle", 0, 0, 14, 14, 0xFF4040C0UL, OBGPU_OP_SOURCE_OVER);
    tRestore();
    verify("rectangles as paths");
    /* without PATH_FILL or work memory: cairo's */
    reset(OBGPU_CAN_PD, 4096, 1024);
    npath = 0;
    pEllipse(20, 20, 10, 10);
    tPathFill("no PATH_FILL", 0, 0xFF000000UL, 0, OBGPU_OP_SOURCE_OVER, 0);
    tFill("a fractional rectangle without PATH_FILL", 3.5, 3.5, 5, 5, 0xFF000000UL, OBGPU_OP_SOURCE_OVER);
    reset(OBGPU_CAN_PD | OBGPU_CAN_PATH, 4096, 1024);
    obgpu_work(&G, 0, 0);
    tPathFill("no work memory", 0, 0xFF000000UL, 0, OBGPU_OP_SOURCE_OVER, 0);
    verify("paths without PATH_FILL");
}

static void testStrokes(void)
{
    struct tstroke s;
    char what[80];
    int cap, join;
    reset(OBGPU_CAN_PD | OBGPU_CAN_PATH, 4096, 1024);
    memset(&s, 0, sizeof s);
    s.miter = 10;
    /* each cap on a slanted line */
    for (cap = 0; cap < 3; cap++) {
        s.width = 5.5;
        s.cap = cap;
        sprintf(what, "a line with cap %d", cap);
        npath = 0;
        pM(8 + cap * 30, 6.3);
        pL(24.5 + cap * 30, 20);
        tPathStroke(what, &s, 0xFF103070UL, 0, OBGPU_OP_SOURCE_OVER, -1);
    }
    /* each join on a zigzag, open, butt caps */
    for (join = 0; join < 3; join++) {
        s.width = 4;
        s.cap = OBGPU_CAP_BUTT;
        s.join = join;
        sprintf(what, "a zigzag with join %d", join);
        npath = 0;
        pM(6 + join * 30, 58);
        pL(14 + join * 30, 30);
        pL(20 + join * 30, 52);
        pL(28 + join * 30, 36);
        tPathStroke(what, &s, 0xC0602000UL, 0, OBGPU_OP_SOURCE_OVER, -1);
    }
    verify("caps and joins");
    /* closed: a rectangle and a triangle, every join; a sharp V past the miter limit */
    for (join = 0; join < 3; join++) {
        s.width = 3;
        s.join = join;
        sprintf(what, "a closed rectangle with join %d", join);
        npath = 0;
        pM(5 + join * 30, 5); pL(25 + join * 30, 5); pL(25 + join * 30, 22); pL(5 + join * 30, 22); pZ();
        tPathStroke(what, &s, 0xFF008040UL, 0, OBGPU_OP_SOURCE_OVER, -1);
        sprintf(what, "a closed triangle with join %d", join);
        npath = 0;
        pM(8 + join * 30, 30); pL(26 + join * 30, 36); pL(12 + join * 30, 46); pZ();
        tPathStroke(what, &s, 0x80800040UL, 0, OBGPU_OP_SOURCE_OVER, -1);
    }
    s.width = 3;
    s.join = OBGPU_JOIN_MITER;
    s.miter = 4;
    npath = 0;
    pM(10, 62); pL(20, 50); pL(30, 62);
    tPathStroke("a wide V within the miter limit", &s, 0xFF000000UL, 0, OBGPU_OP_SOURCE_OVER, -1);
    npath = 0;
    pM(50, 62); pL(56, 50); pL(62, 62);
    tPathStroke("a sharp V past the miter limit", &s, 0xFF000000UL, 0, OBGPU_OP_SOURCE_OVER, -1);
    s.miter = 10;
    verify("closed strokes and the miter limit");
    /* a stroke under a scale: the width scales; a round curve */
    tSave();
    tMatrix(2, 0, 0, 2, 1, 1);
    s.width = 1.5;
    s.cap = OBGPU_CAP_SQUARE;
    s.join = OBGPU_JOIN_BEVEL;
    npath = 0;
    pM(4, 4); pL(20, 4); pL(12, 14);
    tPathStroke("scaled", &s, 0xFF404040UL, 0, OBGPU_OP_SOURCE_OVER, -1);
    tRestore();
    s.width = 2.5;
    s.cap = OBGPU_CAP_ROUND;
    s.join = OBGPU_JOIN_ROUND;
    npath = 0;
    pM(50, 40); pC(60, 10, 80, 60, 90, 30);
    tPathStroke("a round curve", &s, 0xA0A00000UL, 0, OBGPU_OP_SOURCE_OVER, -1);
    verify("strokes scaled and curved");
    /* subpaths that never leave their start: cairo's dots and squares along x */
    s.width = 4;
    s.cap = OBGPU_CAP_ROUND;
    npath = 0;
    pM(10, 10); pL(10, 10);
    pM(20, 10); pZ();
    pM(30, 10); pL(30, 10); pC(30, 10, 30, 10, 30, 10);
    pM(40, 10);
    pM(50, 10); pL(50, 10); pZ();
    tPathStroke("round caps on subpaths of one point", &s, 0xFF000080UL, 0, OBGPU_OP_SOURCE_OVER, -1);
    s.cap = OBGPU_CAP_SQUARE;
    npath = 0;
    pM(10.5, 30); pL(10.5, 30);
    pM(25, 30.5); pZ();
    tPathStroke("square caps on subpaths of one point", &s, 0xFF008000UL, 0, OBGPU_OP_SOURCE_OVER, -1);
    s.cap = OBGPU_CAP_BUTT;
    npath = 0;
    pM(60, 30); pL(60, 30);
    tPathStroke("butt caps on a subpath of one point: nothing", &s, 0xFF800000UL, 0, OBGPU_OP_SOURCE_OVER, -1);
    verify("subpaths of one point");
    /* no width: nothing; operators cairo bounds by coverage: cairo's */
    s.width = 0;
    npath = 0;
    pM(5, 5); pL(50, 50);
    tPathStroke("no width", &s, 0xFF000000UL, 0, OBGPU_OP_SOURCE_OVER, 1);
    s.width = 2;
    tPathStroke("a stroke by copy", &s, 0xFF000000UL, 0, OBGPU_OP_COPY, 0);
    verify("strokes with nothing to draw");
}

static void testDashes(void)
{
    struct tstroke s;
    reset(OBGPU_CAN_PD | OBGPU_CAN_PATH, 4096, 1024);
    memset(&s, 0, sizeof s);
    s.miter = 10;
    /* WebKit's dashed and dotted borders, and canvas's patterns */
    s.width = 3;
    s.cap = OBGPU_CAP_BUTT;
    s.dashes = 2; s.dash[0] = 9; s.dash[1] = 6;
    npath = 0;
    pM(2, 4.5); pL(90, 4.5);
    tPathStroke("dashes", &s, 0xFF202080UL, 0, OBGPU_OP_SOURCE_OVER, -1);
    s.offset = 4.5;
    npath = 0;
    pM(2, 12.5); pL(90, 12.5);
    tPathStroke("dashes from an offset", &s, 0xFF202080UL, 0, OBGPU_OP_SOURCE_OVER, -1);
    s.offset = -7;
    npath = 0;
    pM(2, 20.5); pL(90, 20.5);
    tPathStroke("dashes from a negative offset", &s, 0xFF202080UL, 0, OBGPU_OP_SOURCE_OVER, -1);
    s.offset = 0;
    s.dashes = 3; s.dash[0] = 8; s.dash[1] = 3; s.dash[2] = 2;
    npath = 0;
    pM(2, 28.5); pL(90, 28.5);
    tPathStroke("an odd count of dashes", &s, 0xFF208020UL, 0, OBGPU_OP_SOURCE_OVER, -1);
    s.cap = OBGPU_CAP_ROUND;
    s.dashes = 2; s.dash[0] = 0; s.dash[1] = 6;
    npath = 0;
    pM(4, 37); pL(90, 37);
    tPathStroke("dots: dashes of no length, round", &s, 0xFF800000UL, 0, OBGPU_OP_SOURCE_OVER, -1);
    s.cap = OBGPU_CAP_SQUARE;
    s.dashes = 2; s.dash[0] = 5; s.dash[1] = 7;
    npath = 0;
    pM(4, 45.5); pL(40, 45.5); pL(40, 62);
    tPathStroke("square-capped dashes round a corner", &s, 0xC0400040UL, 0, OBGPU_OP_SOURCE_OVER, -1);
    s.cap = OBGPU_CAP_BUTT;
    s.join = OBGPU_JOIN_MITER;
    s.dashes = 2; s.dash[0] = 14; s.dash[1] = 4;
    s.offset = 3;
    npath = 0;
    pM(50, 44); pL(90, 44); pL(90, 62); pL(50, 62); pZ();
    tPathStroke("a dashed closed rectangle, on at both ends", &s, 0xFF006060UL, 0, OBGPU_OP_SOURCE_OVER, -1);
    verify("dashes");
}

static void testGradients(void)
{
    struct tgrad gr;
    char what[64];
    int spread;
    reset(OBGPU_CAN_PD | OBGPU_CAN_PATH, 4096, 1024);
    memset(&gr, 0, sizeof gr);
    gr.m[0] = gr.m[3] = 1;
    /* linear, by each spread, across rectangles */
    for (spread = 0; spread < 3; spread++) {
        gr.kind = OBGPU_LINEAR;
        gr.spread = spread;
        gr.geo[0] = 10; gr.geo[1] = 0; gr.geo[2] = 30; gr.geo[3] = 6;
        gr.stops = 3;
        gr.offset[0] = 0; gr.argb[0] = 0xFFFF0000UL;
        gr.offset[1] = 0.4; gr.argb[1] = 0xFF00FF00UL;
        gr.offset[2] = 1; gr.argb[2] = 0xFF0000FFUL;
        sprintf(what, "linear, spread %d", spread);
        npath = 0;
        pM(0, spread * 10); pL(W, spread * 10); pL(W, spread * 10 + 9); pL(0, spread * 10 + 9); pZ();
        tPathFill(what, 0, 0, &gr, OBGPU_OP_SOURCE_OVER, -1);
    }
    /* translucent stops, and a sharp change */
    gr.spread = OBGPU_PAD;
    gr.stops = 4;
    gr.offset[0] = 0; gr.argb[0] = 0x80800000UL;
    gr.offset[1] = 0.5; gr.argb[1] = 0x80008000UL;
    gr.offset[2] = 0.5; gr.argb[2] = 0x80000080UL;
    gr.offset[3] = 1; gr.argb[3] = 0x80808080UL;
    npath = 0;
    pEllipse(48, 36, 40, 5);
    tPathFill("translucent stops with a sharp change", 0, 0, &gr, OBGPU_OP_SOURCE_OVER, -1);
    verify("linear gradients");
    /* radial: two circles, a focus inside, a cone; under a transform and its own matrix */
    for (spread = 0; spread < 3; spread++) {
        gr.kind = OBGPU_RADIAL;
        gr.spread = spread;
        gr.geo[0] = 16 + spread * 32; gr.geo[1] = 16; gr.geo[2] = 0;
        gr.geo[3] = 16 + spread * 32; gr.geo[4] = 16; gr.geo[5] = 9;
        gr.stops = 2;
        gr.offset[0] = 0; gr.argb[0] = 0xFFFFFF00UL;
        gr.offset[1] = 1; gr.argb[1] = 0xFF0000A0UL;
        sprintf(what, "radial, spread %d", spread);
        npath = 0;
        pM(spread * 32, 0); pL(spread * 32 + 32, 0); pL(spread * 32 + 32, 32); pL(spread * 32, 32); pZ();
        tPathFill(what, 0, 0, &gr, OBGPU_OP_SOURCE_OVER, -1);
    }
    gr.spread = OBGPU_PAD;
    gr.geo[0] = 12; gr.geo[1] = 44; gr.geo[2] = 2; gr.geo[3] = 18; gr.geo[4] = 48; gr.geo[5] = 14;
    npath = 0;
    pM(0, 33); pL(32, 33); pL(32, 64); pL(0, 64); pZ();
    tPathFill("radial, the focus off centre", 0, 0, &gr, OBGPU_OP_SOURCE_OVER, -1);
    gr.geo[0] = 40; gr.geo[1] = 48; gr.geo[2] = 2; gr.geo[3] = 56; gr.geo[4] = 48; gr.geo[5] = 6;
    npath = 0;
    pM(33, 33); pL(64, 33); pL(64, 64); pL(33, 64); pZ();
    tPathFill("radial, a cone (no t outside it)", 0, 0, &gr, OBGPU_OP_SOURCE_OVER, -1);
    tSave();
    tMatrix(1.5, 0, 0, 1, 66, 33);
    gr.m[0] = 1; gr.m[1] = 0; gr.m[2] = 0; gr.m[3] = 0.5; gr.m[4] = 0; gr.m[5] = 15;
    gr.geo[0] = 10; gr.geo[1] = 2; gr.geo[2] = 0; gr.geo[3] = 10; gr.geo[4] = 2; gr.geo[5] = 18;
    npath = 0;
    pM(0, 0); pL(20, 0); pL(20, 31); pL(0, 31); pZ();
    tPathFill("radial, scaled, with its own matrix", 0, 0, &gr, OBGPU_OP_SOURCE_OVER, -1);
    tRestore();
    verify("radial gradients");
    /* linear, turned, with its own matrix moved and turned */
    tSave();
    tMatrix(0.8660254, 0.5, -0.5, 0.8660254, 40, 2);
    gr.kind = OBGPU_LINEAR;
    gr.spread = OBGPU_REFLECT;
    gr.m[0] = 0.5; gr.m[1] = -0.8660254; gr.m[2] = 0.8660254; gr.m[3] = 0.5; gr.m[4] = 6; gr.m[5] = 14;
    gr.geo[0] = 0; gr.geo[1] = 0; gr.geo[2] = 7; gr.geo[3] = 3;
    gr.stops = 2;
    gr.offset[0] = 0; gr.argb[0] = 0xFF00C0C0UL;
    gr.offset[1] = 1; gr.argb[1] = 0xFFC00060UL;
    npath = 0;
    pM(0, 0); pL(36, 0); pL(36, 30); pL(0, 30); pZ();
    tPathFill("linear, turned, its matrix moved and turned", 0, 0, &gr, OBGPU_OP_SOURCE_OVER, -1);
    tRestore();
    verify("gradients turned");
    /* a stroke with a gradient; gradients the writer refuses */
    memset(&gr, 0, sizeof gr);
    gr.m[0] = gr.m[3] = 1;
    gr.kind = OBGPU_LINEAR;
    gr.geo[0] = 0; gr.geo[1] = 0; gr.geo[2] = W; gr.geo[3] = H;
    gr.stops = 2;
    gr.offset[0] = 0; gr.argb[0] = 0xFF000000UL;
    gr.offset[1] = 1; gr.argb[1] = 0xFFFFFFFFUL;
    {
        struct tstroke s;
        memset(&s, 0, sizeof s);
        s.width = 4;
        s.miter = 10;
        s.cap = OBGPU_CAP_ROUND;
        s.join = OBGPU_JOIN_ROUND;
        npath = 0;
        pM(6, 6); pL(90, 30); pL(10, 58);
        tPathStroke("a stroke with a gradient", &s, 0, &gr, OBGPU_OP_SOURCE_OVER, -1);
    }
    gr.offset[1] = -0.5;
    npath = 0;
    pEllipse(48, 32, 10, 10);
    tPathFill("stops out of order", 0, 0, &gr, OBGPU_OP_SOURCE_OVER, 0);
    gr.offset[1] = 1;
    gr.kind = 3;
    tPathFill("a conic gradient", 0, 0, &gr, OBGPU_OP_SOURCE_OVER, 0);
    gr.kind = OBGPU_LINEAR;
    gr.stops = 0;
    tPathFill("no stops", 0, 0, &gr, OBGPU_OP_SOURCE_OVER, 0);
    verify("gradients on strokes, and refused");
}

static void testPathBatches(void)
{
    char what[64];
    int i, fitted;
    long before;
    /* short scratch: the batch before a path goes when the path needs the room */
    reset(OBGPU_CAN_PD | OBGPU_CAN_PATH, 4096, 160);
    for (i = 0; i < 12; i++) {
        sprintf(what, "ellipse %d, short scratch", i);
        npath = 0;
        pEllipse(8 + (i % 6) * 16, 12 + (i / 6) * 30, 7, 9);
        tPathFill(what, 0, i & 1 ? 0x80FF0000UL : 0xFF0040FFUL, 0, OBGPU_OP_SOURCE_OVER, -1);
    }
    verify("paths with short scratch");
    checks++;
    if (submitted < 3) {
        printf("FAIL paths with short scratch: %ld batches\n", submitted);
        failures++;
    }
    /* a short stream */
    reset(OBGPU_CAN_PD | OBGPU_CAN_PATH, 64, 1024);
    for (i = 0; i < 10; i++) {
        sprintf(what, "triangle %d of a short stream", i);
        npath = 0;
        pM(2 + i * 9, 10); pL(10 + i * 9, 12); pL(5 + i * 9, 40.5);
        tPathFill(what, 0, 0xFF000000UL | (unsigned long)i * 0x101010UL, 0, OBGPU_OP_SOURCE_OVER, -1);
    }
    verify("paths in a short stream");
    /* a path larger than all of scratch: cairo's, and the next one drawn */
    reset(OBGPU_CAN_PD | OBGPU_CAN_PATH, 4096, 160);
    tFill("a fill before", 0, 0, 10, 10, 0xFF808080UL, OBGPU_OP_COPY);
    npath = 0;
    for (i = 0; i < 60; i++)
        pL(10 + (i & 7) * 4, 2 + i);
    before = submitted;
    sendPath(&fitted);
    expectResult("too large: an element refused", fitted, 0);
    expectResult("too large: cairo's", obgpu_fill_path(&G, 0xFF000000UL, 0, 0, OBGPU_OP_SOURCE_OVER), 0);
    handedOn();
    expectCount("too large: scratch given back", G.scratch_used, 0);
    expectCount("too large: the fill before sent", submitted - before, 1);
    npath = 0;
    pEllipse(60, 30, 10, 10);
    tPathFill("after a path too large", 0, 0xFF00FF00UL, 0, OBGPU_OP_SOURCE_OVER, -1);
    verify("a path too large");
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
    testPathFills();
    testPathStates();
    testStrokes();
    testDashes();
    testGradients();
    testPathBatches();
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
