/* GFOpenJSON's core: json.library's work in lean C++, no Amiga headers.
 * Copyright (c) 2026 AmiCradle Team. MIT.
 *
 * Our own code, written from RFC 8259 (JSON), RFC 6901 (JSON Pointer),
 * RFC 3629 (UTF-8), IEEE 754 (binary64) and ECMA-262's Number::toString.
 *
 * Lean C++ (DalsinAI/gfopengcc docs/lean-cxx.md, the library profile):
 * no exceptions, no RTTI, no static constructors, no function-local
 * statics, no std headers, no libstdc++; the calls are extern "C". The only
 * tables are const (.rodata). No struct is copied by assignment, and no
 * loop is left for the compiler to turn into memcpy or memset: the core
 * calls nothing but libgcc's arithmetic helpers and the env.
 *
 * No floating point: decimal text to IEEE 754 double and back is done with
 * integers (a small bignum), correctly rounded both ways, so the library
 * runs the same with or without an FPU and needs no maths library.
 *
 * Sections: helpers; UTF-8; the in-memory parser (two passes over the
 * text, one to measure and one to fill, so a document is one exact
 * allocation); reading a block; numbers; the writer; the streaming
 * reader; checking a block; error strings.
 */
#include "json_core.h"

namespace {

using u8 = oj_u8;
using u32 = oj_u32;
using s32 = oj_s32;
using u64 = unsigned long long;
using s64 = long long;

/* ---- Helpers ------------------------------------------------------------ */

inline void copy_bytes(u8 *d, const u8 *s, u32 n)
{
    while (n--)
        *d++ = *s++;
}

inline u32 cstr_len(const char *s)
{
    const char *p = s;
    while (*p)
        p++;
    return (u32)(p - s);
}

inline bool is_digit(u32 c) { return c - '0' < 10; }
inline bool is_ws(u32 c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }

inline int clz32(u32 x) { return x ? __builtin_clz(x) : 32; }
inline int clz64(u64 x)
{
    u32 hi = (u32)(x >> 32);
    return hi ? clz32(hi) : 32 + clz32((u32)x);
}

/* UTF-8 length of a code point (up to U+10FFFF) and its bytes */
inline u32 utf8_len(u32 cp) { return cp < 0x80 ? 1 : cp < 0x800 ? 2 : cp < 0x10000 ? 3 : 4; }
u32 utf8_put(u8 *d, u32 cp)
{
    if (cp < 0x80) {
        d[0] = (u8)cp;
        return 1;
    }
    if (cp < 0x800) {
        d[0] = (u8)(0xC0 | cp >> 6);
        d[1] = (u8)(0x80 | (cp & 0x3F));
        return 2;
    }
    if (cp < 0x10000) {
        d[0] = (u8)(0xE0 | cp >> 12);
        d[1] = (u8)(0x80 | (cp >> 6 & 0x3F));
        d[2] = (u8)(0x80 | (cp & 0x3F));
        return 3;
    }
    d[0] = (u8)(0xF0 | cp >> 18);
    d[1] = (u8)(0x80 | (cp >> 12 & 0x3F));
    d[2] = (u8)(0x80 | (cp >> 6 & 0x3F));
    d[3] = (u8)(0x80 | (cp & 0x3F));
    return 4;
}

/* RFC 3629: given a lead byte at or above 0x80, how many continuation
 * bytes follow and the range the first of them must be in (the others are
 * all 80-BF). 0 for a byte that cannot start a sequence. */
inline u32 utf8_lead(u32 c, u32 *lo, u32 *hi)
{
    *lo = 0x80;
    *hi = 0xBF;
    if (c < 0xC2) return 0;
    if (c < 0xE0) return 1;
    if (c < 0xF0) {
        if (c == 0xE0) *lo = 0xA0;
        else if (c == 0xED) *hi = 0x9F;     /* no surrogates */
        return 2;
    }
    if (c < 0xF5) {
        if (c == 0xF0) *lo = 0x90;
        else if (c == 0xF4) *hi = 0x8F;     /* nothing past U+10FFFF */
        return 3;
    }
    return 0;
}

/* One UTF-8 sequence at p (p < end, *p >= 0x80): its length, 0 if bad */
inline u32 utf8_check(const u8 *p, const u8 *end)
{
    u32 lo, hi, n = utf8_lead(*p, &lo, &hi);
    if (!n || (u32)(end - p) <= n) return 0;
    if (p[1] < lo || p[1] > hi) return 0;
    for (u32 i = 2; i <= n; i++)
        if ((p[i] & 0xC0) != 0x80) return 0;
    return n + 1;
}

/* Decode the code point of a sequence utf8_check accepted */
inline u32 utf8_decode(const u8 *p, u32 n)
{
    if (n == 2) return (u32)(p[0] & 0x1F) << 6 | (p[1] & 0x3F);
    if (n == 3) return (u32)(p[0] & 0x0F) << 12 | (u32)(p[1] & 0x3F) << 6 | (p[2] & 0x3F);
    return (u32)(p[0] & 0x07) << 18 | (u32)(p[1] & 0x3F) << 12 | (u32)(p[2] & 0x3F) << 6 | (p[3] & 0x3F);
}

inline s32 hex_value(u32 c)
{
    if (c - '0' < 10) return (s32)(c - '0');
    c |= 0x20;
    if (c - 'a' < 6) return (s32)(c - 'a' + 10);
    return -1;
}

constexpr u32 REPLACEMENT = 0xFFFD;     /* for an unpaired surrogate escape */

/* ---- The in-memory parser ------------------------------------------------- */

/* The text is read twice by the same code: once with FILL false to check
 * it and count the nodes and pool bytes, once with FILL true to write the
 * block. Iterative, not recursive: depth costs one bit (in an object or
 * an array) here and one parent link in the open container's own node. */
struct Parser {
    const u8 *start, *p, *end;
    u32 flags;
    u32 nodes, pool;            /* counts so far */
    oj_node *nv;                /* FILL: the node array */
    u8 *pv;                     /* FILL: the pool */
    s32 code;
    const u8 *errp;
    u8 stack[OJ_MAXDEPTH / 8];  /* bit set: an object */
};

inline bool fail(Parser &ps, s32 code, const u8 *at)
{
    ps.code = code;
    ps.errp = at;
    return false;
}

/* Whitespace, and comments with JSONF_COMMENTS */
bool skip_ws(Parser &ps)
{
    const u8 *p = ps.p, *end = ps.end;
    for (;;) {
        while (p < end && is_ws(*p))
            p++;
        if (p < end && *p == '/' && (ps.flags & OJ_F_COMMENTS)) {
            if (p + 1 >= end) {
                ps.p = end;
                return fail(ps, OJ_EOF, end);
            }
            if (p[1] == '/') {
                p += 2;
                while (p < end && *p != '\n')
                    p++;
                continue;
            }
            if (p[1] == '*') {
                p += 2;
                for (;;) {
                    if (p + 1 >= end) {
                        ps.p = end;
                        return fail(ps, OJ_EOF, end);
                    }
                    if (p[0] == '*' && p[1] == '/') break;
                    p++;
                }
                p += 2;
                continue;
            }
            return fail(ps, OJ_SYNTAX, p + 1);
        }
        break;
    }
    ps.p = p;
    return true;
}

/* A bad UTF-8 sequence at p: the byte at fault, or the end if it is cut off */
bool utf8_fail(Parser &ps, const u8 *p)
{
    u32 lo, hi, n = utf8_lead(*p, &lo, &hi);
    if (!n) return fail(ps, OJ_UTF8, p);
    for (u32 i = 1; i <= n; i++) {
        if (p + i >= ps.end) return fail(ps, OJ_EOF, ps.end);
        if (p[i] < lo || p[i] > hi) return fail(ps, OJ_UTF8, p + i);
        lo = 0x80;
        hi = 0xBF;
    }
    return fail(ps, OJ_UTF8, p);
}

/* A string at ps.p (on its opening quote). Counts or writes its UTF-8
 * and the NUL after it; *outlen gets its length. */
template <bool FILL>
bool scan_string(Parser &ps, u32 *outlen)
{
    const u8 *p = ps.p + 1, *end = ps.end;
    u8 *d = FILL ? ps.pv + ps.pool : nullptr;
    u32 n = 0;
    bool latin1 = ps.flags & OJ_F_LATIN1;
    for (;;) {
        /* the plain run: most of most strings */
        const u8 *run = p;
        while (p < end && *p >= 0x20 && *p < 0x80 && *p != '"' && *p != '\\')
            p++;
        if (FILL) copy_bytes(d + n, run, (u32)(p - run));
        n += (u32)(p - run);
        if (p >= end) return fail(ps, OJ_EOF, end);
        u32 c = *p;
        if (c == '"') break;
        if (c < 0x20) return fail(ps, OJ_CONTROL, p);
        if (c >= 0x80) {
            if (latin1) {
                if (FILL) utf8_put(d + n, c);
                n += 2;
                p++;
                continue;
            }
            u32 k = utf8_check(p, end);
            if (!k) return utf8_fail(ps, p);
            if (FILL) copy_bytes(d + n, p, k);
            n += k;
            p += k;
            continue;
        }
        /* an escape */
        if (p + 1 >= end) return fail(ps, OJ_EOF, end);
        c = p[1];
        p += 2;
        u32 cp;
        switch (c) {
        case '"': cp = '"'; break;
        case '\\': cp = '\\'; break;
        case '/': cp = '/'; break;
        case 'b': cp = 8; break;
        case 'f': cp = 12; break;
        case 'n': cp = 10; break;
        case 'r': cp = 13; break;
        case 't': cp = 9; break;
        case 'u': {
            cp = 0;
            for (int i = 0; i < 4; i++) {
                if (p + i >= end) return fail(ps, OJ_EOF, end);
                s32 h = hex_value(p[i]);
                if (h < 0) return fail(ps, OJ_ESCAPE, p + i);
                cp = cp << 4 | (u32)h;
            }
            p += 4;
            if (cp >= 0xD800 && cp < 0xDC00) {
                /* a high surrogate: a low one must follow as \uDC00-\uDFFF */
                u32 lo = 0;
                if (end - p >= 6 && p[0] == '\\' && p[1] == 'u') {
                    for (int i = 2; i < 6; i++) {
                        s32 h = hex_value(p[i]);
                        if (h < 0) {
                            lo = 0;
                            break;
                        }
                        lo = lo << 4 | (u32)h;
                    }
                }
                if (lo >= 0xDC00 && lo < 0xE000) {
                    cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                    p += 6;
                } else {
                    cp = REPLACEMENT;
                }
            } else if (cp >= 0xDC00 && cp < 0xE000) {
                cp = REPLACEMENT;
            }
            break;
        }
        default:
            return fail(ps, OJ_ESCAPE, p - 1);
        }
        if (FILL) utf8_put(d + n, cp);
        n += utf8_len(cp);
    }
    if (FILL) d[n] = 0;
    ps.p = p + 1;
    *outlen = n;
    return true;
}

/* RFC 8259's number at p: its end, or nullptr with the error set. *intval
 * and *isint get the value when it is an integer that fits a LONG ("-0"
 * does not: it stays a NUMBER, so its sign survives). */
const u8 *scan_number(Parser &ps, const u8 *p, s32 *intval, bool *isint)
{
    const u8 *end = ps.end;
    bool neg = false;
    if (*p == '-') {
        neg = true;
        p++;
    }
    if (p >= end) return fail(ps, OJ_EOF, end), nullptr;
    if (!is_digit(*p)) return fail(ps, OJ_NUMBER, p), nullptr;
    u32 v = 0;
    bool fits = true;
    if (*p == '0') {
        p++;
        if (p < end && is_digit(*p)) return fail(ps, OJ_NUMBER, p), nullptr;
    } else {
        u32 lim = neg ? 0x80000000u : 0x7FFFFFFFu;
        while (p < end && is_digit(*p)) {
            u32 dg = *p++ - '0';
            if (fits && (v > (lim - dg) / 10)) fits = false;
            else v = v * 10 + dg;
        }
    }
    bool frac = false;
    if (p < end && *p == '.') {
        p++;
        if (p >= end) return fail(ps, OJ_EOF, end), nullptr;
        if (!is_digit(*p)) return fail(ps, OJ_NUMBER, p), nullptr;
        while (p < end && is_digit(*p))
            p++;
        frac = true;
    }
    if (p < end && (*p == 'e' || *p == 'E')) {
        p++;
        if (p < end && (*p == '+' || *p == '-')) p++;
        if (p >= end) return fail(ps, OJ_EOF, end), nullptr;
        if (!is_digit(*p)) return fail(ps, OJ_NUMBER, p), nullptr;
        while (p < end && is_digit(*p))
            p++;
        frac = true;
    }
    *isint = fits && !frac && !(neg && v == 0);
    *intval = neg ? (s32)(0u - v) : (s32)v;
    return p;
}

template <bool FILL>
bool parse_text(Parser &ps)
{
    u32 depth = 0;
    u32 parent = OJ_NONE;           /* FILL: the open container's node */
    const u8 *end = ps.end;
    bool trailing = ps.flags & OJ_F_TRAILING;
    /* a UTF-8 byte order mark is allowed (RFC 8259 section 8.1) */
    if (!(ps.flags & OJ_F_LATIN1) && end - ps.p >= 3 && ps.p[0] == 0xEF && ps.p[1] == 0xBB && ps.p[2] == 0xBF)
        ps.p += 3;
    if (!skip_ws(ps)) return false;

value:
    {
        const u8 *p = ps.p;
        if (p >= end) return fail(ps, OJ_EOF, end);
        if (ps.nodes >= OJ_FIELD_MAX) return fail(ps, OJ_TOOBIG, p);
        u32 me = ps.nodes++;
        if (FILL && parent != OJ_NONE) ps.nv[parent].head++;    /* one more element or member */
        u32 c = *p;
        if (c == '{' || c == '[') {
            if (depth >= OJ_MAXDEPTH) return fail(ps, OJ_DEPTH, p);
            bool obj = c == '{';
            if (obj) ps.stack[depth >> 3] |= (u8)(1u << (depth & 7));
            else ps.stack[depth >> 3] &= (u8)~(1u << (depth & 7));
            depth++;
            if (FILL) {
                ps.nv[me].head = (u32)(obj ? OJ_T_OBJECT : OJ_T_ARRAY) << 28;
                ps.nv[me].data = parent;    /* the link back, until it closes */
                parent = me;
            }
            ps.p = p + 1;
            if (!skip_ws(ps)) return false;
            if (ps.p < end && *ps.p == (obj ? '}' : ']')) goto close;
            if (obj) goto key;
            goto value;
        }
        if (c == '"') {
            u32 n;
            if (!scan_string<FILL>(ps, &n)) return false;
            if (FILL) {
                ps.nv[me].head = (u32)OJ_T_STRING << 28 | ps.pool;
                ps.nv[me].data = n;
            }
            ps.pool += n + 1;
            if (ps.pool > OJ_FIELD_MAX) return fail(ps, OJ_TOOBIG, p);
        } else if (c == '-' || is_digit(c)) {
            s32 iv;
            bool isint;
            const u8 *q = scan_number(ps, p, &iv, &isint);
            if (!q) return false;
            if (isint) {
                if (FILL) {
                    ps.nv[me].head = (u32)OJ_T_INT << 28;
                    ps.nv[me].data = (u32)iv;
                }
            } else {
                u32 n = (u32)(q - p);
                if (FILL) {
                    ps.nv[me].head = (u32)OJ_T_NUMBER << 28 | ps.pool;
                    ps.nv[me].data = n;
                    copy_bytes(ps.pv + ps.pool, p, n);
                    ps.pv[ps.pool + n] = 0;
                }
                ps.pool += n + 1;
                if (ps.pool > OJ_FIELD_MAX) return fail(ps, OJ_TOOBIG, p);
            }
            ps.p = q;
        } else {
            const char *lit;
            u32 t;
            if (c == 't') lit = "true", t = OJ_T_TRUE;
            else if (c == 'f') lit = "false", t = OJ_T_FALSE;
            else if (c == 'n') lit = "null", t = OJ_T_NULL;
            else return fail(ps, OJ_SYNTAX, p);
            const u8 *q = p;
            for (; *lit; lit++, q++) {
                if (q >= end) return fail(ps, OJ_EOF, end);
                if (*q != (u8)*lit) return fail(ps, OJ_SYNTAX, q);
            }
            if (FILL) {
                ps.nv[me].head = t << 28;
                ps.nv[me].data = 0;
            }
            ps.p = q;
        }
    }

after:
    if (!skip_ws(ps)) return false;
    if (depth == 0) {
        if (ps.p < end) return fail(ps, OJ_TRAILING, ps.p);
        return true;
    }
    {
        const u8 *p = ps.p;
        if (p >= end) return fail(ps, OJ_EOF, end);
        bool obj = ps.stack[(depth - 1) >> 3] >> ((depth - 1) & 7) & 1;
        if (*p == ',') {
            ps.p = p + 1;
            if (!skip_ws(ps)) return false;
            if (trailing && ps.p < end && *ps.p == (obj ? '}' : ']')) goto close;
            if (obj) goto key;
            goto value;
        }
        if (*p == (obj ? '}' : ']')) goto close;
        return fail(ps, OJ_SYNTAX, p);
    }

close:
    /* ps.p is on the closing bracket */
    ps.p++;
    depth--;
    if (FILL) {
        u32 up = ps.nv[parent].data;
        ps.nv[parent].data = ps.nodes;  /* the index past its subtree */
        parent = up;
    }
    goto after;

key:
    {
        const u8 *p = ps.p;
        if (p >= end) return fail(ps, OJ_EOF, end);
        if (*p != '"') return fail(ps, OJ_SYNTAX, p);
        if (ps.nodes >= OJ_FIELD_MAX) return fail(ps, OJ_TOOBIG, p);
        u32 me = ps.nodes++;
        u32 n;
        if (!scan_string<FILL>(ps, &n)) return false;
        if (FILL) {
            ps.nv[me].head = (u32)OJ_T_STRING << 28 | ps.pool;
            ps.nv[me].data = n;
        }
        ps.pool += n + 1;
        if (ps.pool > OJ_FIELD_MAX) return fail(ps, OJ_TOOBIG, p);
        if (!skip_ws(ps)) return false;
        if (ps.p >= end) return fail(ps, OJ_EOF, end);
        if (*ps.p != ':') return fail(ps, OJ_SYNTAX, ps.p);
        ps.p++;
        if (!skip_ws(ps)) return false;
        goto value;     /* the value's node counts the member */
    }
}

void set_error(struct oj_error *err, s32 code, const u8 *start, const u8 *at)
{
    if (!err) return;
    err->code = code;
    err->offset = err->line = err->column = 0;
    if (!start || !at) return;
    u32 line = 1;
    const u8 *ls = start;
    for (const u8 *q = start; q < at; q++)
        if (*q == '\n') {
            line++;
            ls = q + 1;
        }
    err->offset = (u32)(at - start);
    err->line = line;
    err->column = (u32)(at - ls) + 1;
}

constexpr u32 HEADER = sizeof(oj_doc);
static_assert(sizeof(oj_doc) == 32, "struct JSONDoc is 32 bytes");
static_assert(sizeof(oj_node) == 8, "struct JSONNode is 8 bytes");
static_assert(sizeof(oj_error) == 16, "struct JSONError is 16 bytes");

/* The measuring pass: nodes and pool bytes, or 0 with the error set */
bool measure(const u8 *text, u32 len, u32 flags, struct oj_error *err, u32 *nodes, u32 *pool)
{
    Parser ps;
    ps.start = ps.p = text;
    ps.end = text + len;
    ps.flags = flags;
    ps.nodes = ps.pool = 0;
    ps.nv = nullptr;
    ps.pv = nullptr;
    ps.code = OJ_OK;
    ps.errp = nullptr;
    if (!text) {
        set_error(err, OJ_EOF, nullptr, nullptr);
        return false;
    }
    if (!parse_text<false>(ps)) {
        set_error(err, ps.code, text, ps.errp);
        return false;
    }
    *nodes = ps.nodes;
    *pool = ps.pool;
    set_error(err, OJ_OK, nullptr, nullptr);
    return true;
}

inline u32 block_size(u32 nodes, u32 pool)
{
    return HEADER + nodes * 8 + ((pool + 3) & ~3u);
}

struct oj_doc *fill(const u8 *text, u32 len, u32 flags, void *mem, u32 nodes, u32 pool)
{
    oj_doc *doc = (oj_doc *)mem;
    doc->magic = OJ_MAGIC;
    doc->size = block_size(nodes, pool);
    doc->nodes = nodes;
    doc->node_off = HEADER;
    doc->pool_off = HEADER + nodes * 8;
    doc->pool_size = pool;
    doc->flags = 0;
    doc->reserved = 0;
    Parser ps;
    ps.start = ps.p = text;
    ps.end = text + len;
    ps.flags = flags;
    ps.nodes = ps.pool = 0;
    ps.nv = (oj_node *)((u8 *)mem + HEADER);
    ps.pv = (u8 *)mem + doc->pool_off;
    ps.code = OJ_OK;
    ps.errp = nullptr;
    parse_text<true>(ps);           /* cannot fail: the same text measured clean */
    for (u32 i = pool; i < ((pool + 3) & ~3u); i++)
        ps.pv[i] = 0;
    return doc;
}

/* ---- Reading a block ------------------------------------------------------ */

inline const oj_node *nodes_of(const oj_doc *doc)
{
    return (const oj_node *)((const u8 *)doc + doc->node_off);
}
inline const char *pool_of(const oj_doc *doc)
{
    return (const char *)doc + doc->pool_off;
}
inline u32 ntype(const oj_node &n) { return n.head >> 28; }
inline u32 nfield(const oj_node &n) { return n.head & OJ_FIELD_MAX; }
inline u32 nskip(const oj_node *nv, u32 i) { return ntype(nv[i]) >= OJ_T_ARRAY ? nv[i].data : i + 1; }
inline bool valid(const oj_doc *doc, u32 node) { return doc && node < doc->nodes; }

/* Does the key node match these bytes? */
inline bool key_is(const oj_doc *doc, const oj_node &k, const char *key, u32 len)
{
    if (k.data != len) return false;
    const char *s = pool_of(doc) + nfield(k);
    for (u32 i = 0; i < len; i++)
        if (s[i] != key[i]) return false;
    return true;
}

/* ---- Numbers ---------------------------------------------------------------- */

/* A double's two longs, most significant first */
inline u64 dbl_bits(const u32 v[2]) { return (u64)v[0] << 32 | v[1]; }
inline void dbl_set(u32 out[2], u64 b)
{
    out[0] = (u32)(b >> 32);
    out[1] = (u32)b;
}

/* m * 2^e2 (m != 0), plus a sticky bit for anything below m, rounded to the
 * nearest double, ties to even: subnormals, overflow to infinity. */
u64 round_to_double(u64 m, s32 e2, bool sticky)
{
    if (!m) return 0;
    int z = clz64(m);
    m <<= z;
    e2 -= z;
    s32 E = e2 + 63;                /* m * 2^e2 is in [2^E, 2^(E+1)) */
    if (E > 1023) return 0x7FF0000000000000ull;
    s32 drop = 11;
    if (E < -1022) drop += -1022 - E;
    if (drop > 64) return 0;
    u64 keep = drop < 64 ? m >> drop : 0;
    bool half = (m >> (drop - 1)) & 1;
    bool below = sticky || (drop > 1 && (m & ((1ull << (drop - 1)) - 1)));
    if (half && (below || (keep & 1))) keep++;
    u64 bits = E >= -1022 ? ((u64)(E + 1022) << 52) + keep : keep;
    if (bits >= 0x7FF0000000000000ull) return 0x7FF0000000000000ull;
    return bits;
}

/* The bignum: little-endian 32-bit words, n of them in use */
struct Big {
    u32 *w;
    u32 n, cap;
};

inline void big_trim(Big &b)
{
    while (b.n && !b.w[b.n - 1])
        b.n--;
}

void big_set(Big &b, u64 v)
{
    b.n = 0;
    while (v) {
        b.w[b.n++] = (u32)v;
        v >>= 32;
    }
}

/* b = b * m + a */
void big_muladd(Big &b, u32 m, u32 a)
{
    u64 carry = a;
    for (u32 i = 0; i < b.n; i++) {
        carry += (u64)b.w[i] * m;
        b.w[i] = (u32)carry;
        carry >>= 32;
    }
    if (carry) b.w[b.n++] = (u32)carry;
}

constexpr u32 POW10[10] = { 1, 10, 100, 1000, 10000, 100000, 1000000, 10000000, 100000000, 1000000000 };

void big_mul_pow10(Big &b, u32 k)
{
    while (k >= 9) {
        big_muladd(b, POW10[9], 0);
        k -= 9;
    }
    if (k) big_muladd(b, POW10[k], 0);
}

void big_shl(Big &b, u32 bits)
{
    if (!b.n) return;
    u32 words = bits >> 5, sh = bits & 31;
    if (sh) {
        u32 top = b.w[b.n - 1] >> (32 - sh);
        for (u32 i = b.n - 1; i > 0; i--)
            b.w[i] = b.w[i] << sh | b.w[i - 1] >> (32 - sh);
        b.w[0] <<= sh;
        if (top) b.w[b.n++] = top;
    }
    if (words) {
        for (u32 i = b.n; i-- > 0;)
            b.w[i + words] = b.w[i];
        for (u32 i = 0; i < words; i++)
            b.w[i] = 0;
        b.n += words;
    }
}

inline u32 big_bits(const Big &b)
{
    return b.n ? b.n * 32 - (u32)clz32(b.w[b.n - 1]) : 0;
}

/* Word i of (b << s) */
inline u32 big_word_shifted(const Big &b, u32 s, u32 i)
{
    u32 ws = s >> 5, bs = s & 31;
    if (i < ws) return 0;
    u32 j = i - ws;
    u32 lo = j < b.n ? b.w[j] << bs : 0;
    u32 hi = bs && j >= 1 && j - 1 < b.n ? b.w[j - 1] >> (32 - bs) : 0;
    return lo | hi;
}

/* compare a with (b << s) */
int big_cmp_shifted(const Big &a, const Big &b, u32 s)
{
    u32 bn = b.n ? b.n + ((s + 31) >> 5) + 1 : 0;
    u32 top = a.n > bn ? a.n : bn;
    for (u32 i = top; i-- > 0;) {
        u32 x = i < a.n ? a.w[i] : 0, y = big_word_shifted(b, s, i);
        if (x != y) return x < y ? -1 : 1;
    }
    return 0;
}

/* a -= (b << s), a >= b << s */
void big_sub_shifted(Big &a, const Big &b, u32 s)
{
    u64 borrow = 0;
    for (u32 i = 0; i < a.n; i++) {
        u64 d = (u64)a.w[i] - big_word_shifted(b, s, i) - borrow;
        a.w[i] = (u32)d;
        borrow = (d >> 32) & 1;
    }
    big_trim(a);
}

inline int big_cmp(const Big &a, const Big &b) { return big_cmp_shifted(a, b, 0); }

/* r = a + b (r may be a) */
void big_add(Big &r, const Big &a, const Big &b)
{
    u32 n = a.n > b.n ? a.n : b.n;
    u64 carry = 0;
    for (u32 i = 0; i < n; i++) {
        carry += (u64)(i < a.n ? a.w[i] : 0) + (i < b.n ? b.w[i] : 0);
        r.w[i] = (u32)carry;
        carry >>= 32;
    }
    r.n = n;
    if (carry) r.w[r.n++] = (u32)carry;
}

/* The pieces of a number's text, which has already passed the grammar */
struct Decimal {
    const char *d1, *d1e;   /* integer digits */
    const char *d2, *d2e;   /* fraction digits */
    s32 exp;                /* the exponent part, clamped to +-100000 */
    bool neg;
};

/* RFC 8259's grammar over exactly len bytes */
bool split_number(const char *t, u32 len, Decimal &dc)
{
    const char *p = t, *end = t + len;
    dc.neg = false;
    if (p < end && *p == '-') {
        dc.neg = true;
        p++;
    }
    if (p >= end || !is_digit((u8)*p)) return false;
    dc.d1 = p;
    if (*p == '0') p++;
    else
        while (p < end && is_digit((u8)*p))
            p++;
    dc.d1e = p;
    dc.d2 = dc.d2e = p;
    if (p < end && *p == '.') {
        p++;
        dc.d2 = p;
        while (p < end && is_digit((u8)*p))
            p++;
        dc.d2e = p;
        if (dc.d2 == dc.d2e) return false;
    }
    dc.exp = 0;
    if (p < end && (*p == 'e' || *p == 'E')) {
        p++;
        bool eneg = false;
        if (p < end && (*p == '+' || *p == '-')) eneg = *p++ == '-';
        if (p >= end || !is_digit((u8)*p)) return false;
        s32 e = 0;
        while (p < end && is_digit((u8)*p)) {
            if (e < 100000) e = e * 10 + (*p - '0');
            p++;
        }
        dc.exp = eneg ? -e : e;
    }
    return p == end;
}

/* Walks the significant digits of a Decimal, int part then fraction */
struct Digits {
    const Decimal *dc;
    const char *p;
    bool second;
};
inline void dig_start(Digits &g, const Decimal &dc, const char *from)
{
    g.dc = &dc;
    g.p = from;
    g.second = !(from >= dc.d1 && from < dc.d1e);
}
inline u32 dig_next(Digits &g)
{
    if (!g.second && g.p >= g.dc->d1e) {
        g.second = true;
        g.p = g.dc->d2;
    }
    return (u32)(*g.p++ - '0');
}

constexpr u32 MAX_DIGITS = 768;  /* enough to decide any double's rounding */

s32 decimal_to_double(struct oj_env *env, const Decimal &dc, u64 *out)
{
    u64 sign = dc.neg ? 0x8000000000000000ull : 0;
    /* the significant digits: no leading or trailing zeros */
    u32 nint = (u32)(dc.d1e - dc.d1), nfrac = (u32)(dc.d2e - dc.d2);
    u32 total = nint + nfrac;
    u32 lead = 0;
    const char *first = nullptr;
    for (u32 i = 0; i < total; i++) {
        const char *c = i < nint ? dc.d1 + i : dc.d2 + (i - nint);
        if (*c != '0') {
            first = c;
            break;
        }
        lead++;
    }
    if (!first) {
        *out = sign;
        return OJ_OK;
    }
    u32 trail = 0;
    for (u32 i = total; i-- > lead;) {
        const char *c = i < nint ? dc.d1 + i : dc.d2 + (i - nint);
        if (*c != '0') break;
        trail++;
    }
    u32 ndig = total - lead - trail;
    s32 e10 = dc.exp - (s32)nfrac + (s32)trail;   /* value = digits * 10^e10 */
    bool sticky = false;
    u32 take = ndig;
    if (ndig > MAX_DIGITS) {
        take = MAX_DIGITS;
        e10 += (s32)(ndig - MAX_DIGITS);
        sticky = true;      /* the dropped digits are not all zero: the last one isn't */
    }
    s32 mag = (s32)take + e10;     /* the value is below 10^mag */
    if (mag > 310) {
        *out = sign | 0x7FF0000000000000ull;
        return OJ_OK;
    }
    if (mag < -323) {
        *out = sign;
        return OJ_OK;
    }
    Digits g;
    dig_start(g, dc, first);
    if (take <= 19 && !sticky && e10 >= 0 && take + (u32)e10 <= 19) {
        u64 v = 0;
        for (u32 i = 0; i < take; i++)
            v = v * 10 + dig_next(g);
        for (s32 i = 0; i < e10; i++)
            v *= 10;
        *out = sign | round_to_double(v, 0, false);
        return OJ_OK;
    }
    /* Exact: the digits as a bignum N; with e10 >= 0, N * 10^e10 cut to its
     * top 64 bits; else N / 10^-e10 to 62-64 bits by long division. The
     * sticky digit is a final 1 after the 768 kept: it lies strictly
     * between the cut value and the next 768-digit one, as the text does,
     * and no rounding boundary of a double lies between those two. */
    u32 nd = take + (sticky ? 1 : 0);
    s32 ex = e10 - (sticky ? 1 : 0);
    u32 m10 = ex < 0 ? (u32)-ex : 0;
    u32 bitsN = nd * 3322 / 1000 + 2 + (ex > 0 ? (u32)ex * 3322 / 1000 + 2 : 0);
    u32 bitsS = m10 * 3322 / 1000 + 2;
    u32 bmax = bitsN > bitsS ? bitsN : bitsS;
    u32 cap = (bmax + 130) / 32 + 2;
    u32 local[2 * 14];
    u32 *mem = local;
    if (cap > 14) {
        if (!env) return OJ_NOMEM;
        mem = (u32 *)env->alloc(env, cap * 2 * 4);
        if (!mem) return OJ_NOMEM;
    }
    Big N = { mem, 0, cap }, S = { mem + cap, 0, cap };
    u32 chunk = 0, cn = 0;
    for (u32 i = 0; i < nd; i++) {
        chunk = chunk * 10 + (i < take ? dig_next(g) : 1);
        if (++cn == 9) {
            big_muladd(N, POW10[9], chunk);
            chunk = cn = 0;
        }
    }
    if (cn) big_muladd(N, POW10[cn], chunk);
    u64 bits;
    if (ex >= 0) {
        big_mul_pow10(N, (u32)ex);
        u32 L = big_bits(N);
        u64 m = 0;
        bool st = false;
        s32 e2 = 0;
        if (L <= 64) {
            for (u32 i = N.n; i-- > 0;)
                m = m << 32 | N.w[i];
        } else {
            u32 s = L - 64;
            for (u32 i = 0; i < 64; i++) {
                u32 b = s + i;
                if (N.w[b >> 5] >> (b & 31) & 1) m |= 1ull << i;
            }
            for (u32 i = 0; i < (s >> 5) && !st; i++)
                if (N.w[i]) st = true;
            if (!st && (s & 31) && (N.w[s >> 5] & ((1u << (s & 31)) - 1))) st = true;
            e2 = (s32)s;
        }
        bits = round_to_double(m, e2, st);
    } else {
        big_set(S, 1);
        big_mul_pow10(S, m10);
        s32 sh = (s32)big_bits(S) - (s32)big_bits(N) + 63;
        if (sh > 0) big_shl(N, (u32)sh);
        else if (sh < 0) big_shl(S, (u32)-sh);
        u64 q = 0;
        for (int i = 63; i >= 0; i--)
            if (big_cmp_shifted(N, S, (u32)i) >= 0) {
                big_sub_shifted(N, S, (u32)i);
                q |= 1ull << i;
            }
        bits = round_to_double(q, -sh, N.n != 0);
    }
    if (mem != local) env->free(env, mem);
    *out = sign | bits;
    return OJ_OK;
}

/* Exactly-integral numbers that fit a LONG, from the decimal digits */
s32 decimal_to_long(const Decimal &dc, s32 *out)
{
    u32 nint = (u32)(dc.d1e - dc.d1), nfrac = (u32)(dc.d2e - dc.d2);
    u32 total = nint + nfrac;
    u32 lead = 0, trail = 0;
    for (u32 i = 0; i < total; i++) {
        const char *c = i < nint ? dc.d1 + i : dc.d2 + (i - nint);
        if (*c != '0') break;
        lead++;
    }
    if (lead == total) {
        *out = 0;
        return OJ_OK;
    }
    for (u32 i = total; i-- > lead;) {
        const char *c = i < nint ? dc.d1 + i : dc.d2 + (i - nint);
        if (*c != '0') break;
        trail++;
    }
    u32 ndig = total - lead - trail;
    s32 e10 = dc.exp - (s32)nfrac + (s32)trail;
    if (e10 < 0 || (s32)ndig + e10 > 10) return OJ_RANGE;
    u64 v = 0;
    for (u32 i = lead; i < lead + ndig; i++) {
        const char *c = i < nint ? dc.d1 + i : dc.d2 + (i - nint);
        v = v * 10 + (u32)(*c - '0');
    }
    for (s32 i = 0; i < e10; i++)
        v *= 10;
    if (v > (dc.neg ? 0x80000000ull : 0x7FFFFFFFull)) return OJ_RANGE;
    *out = dc.neg ? (s32)(0u - (u32)v) : (s32)v;
    return OJ_OK;
}

u64 long_to_double(s32 v)
{
    if (!v) return 0;
    u64 sign = v < 0 ? 0x8000000000000000ull : 0;
    u32 a = v < 0 ? 0u - (u32)v : (u32)v;
    return sign | round_to_double(a, 0, false);
}

/* Unsigned decimal of v (< 10^20) into buf, no NUL: its length */
constexpr u64 P10[20] = { 1ull, 10ull, 100ull, 1000ull, 10000ull, 100000ull, 1000000ull, 10000000ull,
                          100000000ull, 1000000000ull, 10000000000ull, 100000000000ull,
                          1000000000000ull, 10000000000000ull, 100000000000000ull,
                          1000000000000000ull, 10000000000000000ull, 100000000000000000ull,
                          1000000000000000000ull, 10000000000000000000ull };
u32 u64_to_text(u64 v, char *buf)
{
    int k = 19;
    while (k > 0 && v < P10[k])
        k--;
    u32 n = 0;
    for (; k >= 0; k--) {
        char d = '0';
        while (v >= P10[k]) {
            v -= P10[k];
            d++;
        }
        buf[n++] = d;
    }
    return n;
}

/* Shortest digits (Steele and White's free-format method, as Burger and
 * Dybvig state it, in bignums): digits into buf, *k10 so that the value is
 * 0.d1d2... * 10^k10. The scratch is 5 bignums of BIGW words. */
constexpr u32 BIGW = 40;

u32 shortest_digits(u64 bits, u32 *scratch, char *buf, s32 *k10)
{
    u64 f = bits & 0xFFFFFFFFFFFFFull;
    s32 be = (s32)(bits >> 52 & 0x7FF);
    s32 e;
    if (be) {
        f |= 1ull << 52;
        e = be - 1075;
    } else {
        e = -1074;
    }
    bool even = !(f & 1);
    Big r = { scratch, 0, BIGW }, s = { scratch + BIGW, 0, BIGW }, mp = { scratch + 2 * BIGW, 0, BIGW },
        mm = { scratch + 3 * BIGW, 0, BIGW }, t = { scratch + 4 * BIGW, 0, BIGW };
    bool unequal = false;       /* the gap below is half the gap above (a power of two) */
    if (e >= 0) {
        if (f != 1ull << 52) {
            big_set(r, f);
            big_shl(r, (u32)e + 1);
            big_set(s, 2);
            big_set(mp, 1);
            big_shl(mp, (u32)e);
        } else {
            big_set(r, f);
            big_shl(r, (u32)e + 2);
            big_set(s, 4);
            big_set(mp, 1);
            big_shl(mp, (u32)e + 1);
            big_set(mm, 1);
            big_shl(mm, (u32)e);
            unequal = true;
        }
    } else {
        if (be <= 1 || f != 1ull << 52) {
            big_set(r, f << 1);
            big_set(s, 1);
            big_shl(s, (u32)(1 - e));
            big_set(mp, 1);
        } else {
            big_set(r, f << 2);
            big_set(s, 1);
            big_shl(s, (u32)(2 - e));
            big_set(mp, 2);
            big_set(mm, 1);
            unequal = true;
        }
    }
    /* k: an estimate never above the true one, then raised */
    s32 p2 = e + 63 - clz64(f);     /* floor(log2 v) */
    s32 k = (p2 * 1233) >> 12;      /* 1233 / 4096 < log10(2) */
    if (p2 < 0) k--;
    if (k >= 0) {
        big_mul_pow10(s, (u32)k);
    } else {
        big_mul_pow10(r, (u32)-k);
        big_mul_pow10(mp, (u32)-k);
        if (unequal) big_mul_pow10(mm, (u32)-k);
    }
    for (;;) {
        big_add(t, r, mp);
        int c = big_cmp(t, s);
        if (even ? c < 0 : c <= 0) break;
        big_muladd(s, 10, 0);
        k++;
    }
    Big &lo = unequal ? mm : mp;
    u32 n = 0;
    for (;;) {
        big_muladd(r, 10, 0);
        big_muladd(mp, 10, 0);
        if (unequal) big_muladd(mm, 10, 0);
        u32 d = 0;
        while (big_cmp(r, s) >= 0) {
            big_sub_shifted(r, s, 0);
            d++;
        }
        int c1 = big_cmp(r, lo);
        bool tc1 = even ? c1 <= 0 : c1 < 0;
        big_add(t, r, mp);
        int c2 = big_cmp(t, s);
        bool tc2 = even ? c2 >= 0 : c2 > 0;
        if (!tc1 && !tc2) {
            buf[n++] = (char)('0' + d);
            continue;
        }
        if (tc1 && tc2) {
            big_add(t, r, r);
            int c3 = big_cmp(t, s);
            if (c3 > 0 || (c3 == 0 && (d & 1))) d++;
        } else if (tc2) {
            d++;
        }
        buf[n++] = (char)('0' + d);
        break;
    }
    *k10 = k;
    return n;
}

/* ECMA-262 Number::toString(10) of a finite value from its digits */
u32 format_number(bool neg, const char *dg, u32 len, s32 n, char *out)
{
    u32 o = 0;
    if (neg) out[o++] = '-';
    if ((s32)len <= n && n <= 21) {
        for (u32 i = 0; i < len; i++)
            out[o++] = dg[i];
        for (s32 i = (s32)len; i < n; i++)
            out[o++] = '0';
    } else if (0 < n && n <= 21) {
        for (s32 i = 0; i < n; i++)
            out[o++] = dg[i];
        out[o++] = '.';
        for (u32 i = (u32)n; i < len; i++)
            out[o++] = dg[i];
    } else if (-6 < n && n <= 0) {
        out[o++] = '0';
        out[o++] = '.';
        for (s32 i = n; i < 0; i++)
            out[o++] = '0';
        for (u32 i = 0; i < len; i++)
            out[o++] = dg[i];
    } else {
        out[o++] = dg[0];
        if (len > 1) {
            out[o++] = '.';
            for (u32 i = 1; i < len; i++)
                out[o++] = dg[i];
        }
        out[o++] = 'e';
        s32 x = n - 1;
        out[o++] = x < 0 ? '-' : '+';
        o += u64_to_text((u64)(x < 0 ? -x : x), out + o);
    }
    out[o] = 0;
    return o;
}

/* A double's text into buf (26 bytes); scratch is 5 * BIGW words or
 * nullptr (then only exact integers below 2^53 can be done: 0 back). */
u32 double_text(u64 bits, u32 *scratch, char *buf)
{
    bool neg = bits >> 63;
    u64 a = bits & 0x7FFFFFFFFFFFFFFFull;
    if (a >= 0x7FF0000000000000ull) {
        copy_bytes((u8 *)buf, (const u8 *)"null", 5);
        return 4;
    }
    if (!a) {
        buf[0] = '0';
        buf[1] = 0;
        return 1;   /* -0 too, as JSON.stringify writes it */
    }
    s32 be = (s32)(a >> 52);
    if (be >= 1023 && be < 1023 + 53) {
        /* an integer below 2^53? */
        u32 fracbits = (u32)(1075 - be);
        u64 m = (a & 0xFFFFFFFFFFFFFull) | 1ull << 52;
        if (fracbits == 0 || !(m & ((1ull << fracbits) - 1))) {
            u64 v = m >> fracbits;
            if (v < 1000000000000000000ull) {
                u32 o = 0;
                if (neg) buf[o++] = '-';
                char tmp[20];
                u32 n = u64_to_text(v, tmp);
                /* ECMAScript: 21 digits and fewer stay plain */
                copy_bytes((u8 *)buf + o, (const u8 *)tmp, n);
                o += n;
                buf[o] = 0;
                return o;
            }
        }
    }
    if (!scratch) return 0;
    char dg[20];
    s32 k;
    u32 len = shortest_digits(a, scratch, dg, &k);
    return format_number(neg, dg, len, k, buf);
}

} /* namespace */

/* ---- The calls: parsing ---------------------------------------------------- */

extern "C" oj_u32 oj_doc_size(const oj_u8 *text, oj_u32 len, oj_u32 flags, struct oj_error *err)
{
    if (text && len == OJ_STRLEN) len = cstr_len((const char *)text);
    u32 nodes, pool;
    if (!measure(text, len, flags, err, &nodes, &pool)) return 0;
    return block_size(nodes, pool);
}

extern "C" struct oj_doc *oj_parse_into(const oj_u8 *text, oj_u32 len, oj_u32 flags, void *mem, oj_u32 size,
                                        struct oj_error *err)
{
    if (text && len == OJ_STRLEN) len = cstr_len((const char *)text);
    u32 nodes, pool;
    if (!measure(text, len, flags, err, &nodes, &pool)) return nullptr;
    if (!mem || size < block_size(nodes, pool) || ((unsigned long)mem & 1)) {
        set_error(err, OJ_BUFFER, nullptr, nullptr);
        return nullptr;
    }
    return fill(text, len, flags, mem, nodes, pool);
}

extern "C" struct oj_doc *oj_parse(struct oj_env *env, const oj_u8 *text, oj_u32 len, oj_u32 flags,
                                   struct oj_error *err)
{
    if (text && len == OJ_STRLEN) len = cstr_len((const char *)text);
    u32 nodes, pool;
    if (!measure(text, len, flags, err, &nodes, &pool)) return nullptr;
    void *mem = env->alloc(env, block_size(nodes, pool));
    if (!mem) {
        set_error(err, OJ_NOMEM, nullptr, nullptr);
        return nullptr;
    }
    oj_doc *doc = fill(text, len, flags, mem, nodes, pool);
    doc->flags |= OJ_DF_ALLOCATED;
    return doc;
}

extern "C" void oj_free_doc(struct oj_env *env, struct oj_doc *doc)
{
    if (doc && doc->magic == OJ_MAGIC && (doc->flags & OJ_DF_ALLOCATED)) {
        doc->magic = 0;
        env->free(env, doc);
    }
}

/* ---- The calls: reading ------------------------------------------------------ */

extern "C" oj_u32 oj_type(const struct oj_doc *doc, oj_u32 node)
{
    return valid(doc, node) ? ntype(nodes_of(doc)[node]) : 0;
}

extern "C" oj_u32 oj_count(const struct oj_doc *doc, oj_u32 node)
{
    if (!valid(doc, node)) return 0;
    const oj_node &n = nodes_of(doc)[node];
    return ntype(n) >= OJ_T_ARRAY ? nfield(n) : 0;
}

extern "C" oj_u32 oj_skip(const struct oj_doc *doc, oj_u32 node)
{
    return valid(doc, node) ? nskip(nodes_of(doc), node) : OJ_NONE;
}

extern "C" oj_u32 oj_index(const struct oj_doc *doc, oj_u32 node, oj_u32 i)
{
    if (!valid(doc, node)) return OJ_NONE;
    const oj_node *nv = nodes_of(doc);
    u32 t = ntype(nv[node]);
    if (t < OJ_T_ARRAY || i >= nfield(nv[node])) return OJ_NONE;
    u32 j = node + 1;
    if (t == OJ_T_ARRAY)
        while (i--)
            j = nskip(nv, j);
    else
        while (i--)
            j = nskip(nv, j + 1);
    return j;
}

extern "C" oj_u32 oj_find(const struct oj_doc *doc, oj_u32 node, const char *key, oj_u32 keylen)
{
    if (!valid(doc, node) || !key) return OJ_NONE;
    const oj_node *nv = nodes_of(doc);
    if (ntype(nv[node]) != OJ_T_OBJECT) return OJ_NONE;
    if (keylen == OJ_STRLEN) keylen = cstr_len(key);
    u32 found = OJ_NONE;   /* the last of equal keys, as JSON.parse keeps */
    u32 j = node + 1;
    for (u32 c = nfield(nv[node]); c; c--) {
        if (key_is(doc, nv[j], key, keylen)) found = j + 1;
        j = nskip(nv, j + 1);
    }
    return found;
}

extern "C" oj_u32 oj_pointer(const struct oj_doc *doc, oj_u32 node, const char *ptr)
{
    if (!valid(doc, node) || !ptr) return OJ_NONE;
    const oj_node *nv = nodes_of(doc);
    const char *p = ptr;
    while (*p) {
        if (*p != '/') return OJ_NONE;
        const char *tok = ++p;
        while (*p && *p != '/')
            p++;
        u32 tlen = (u32)(p - tok);
        u32 t = ntype(nv[node]);
        if (t == OJ_T_ARRAY) {
            /* "0" or a number without leading zeros */
            if (!tlen || (tlen > 1 && tok[0] == '0')) return OJ_NONE;
            u32 idx = 0;
            for (u32 i = 0; i < tlen; i++) {
                if (!is_digit((u8)tok[i]) || idx > 0x0FFFFFFF) return OJ_NONE;
                idx = idx * 10 + (u32)(tok[i] - '0');
            }
            node = oj_index(doc, node, idx);
            if (node == OJ_NONE) return OJ_NONE;
        } else if (t == OJ_T_OBJECT) {
            /* compare with ~1 as / and ~0 as ~, keep the last match */
            u32 found = OJ_NONE, j = node + 1;
            for (u32 c = nfield(nv[node]); c; c--) {
                const char *k = pool_of(doc) + nfield(nv[j]);
                u32 kl = nv[j].data, ki = 0, ti = 0;
                bool ok = true;
                while (ti < tlen) {
                    char ch = tok[ti++];
                    if (ch == '~') {
                        if (ti >= tlen) return OJ_NONE;
                        char x = tok[ti++];
                        if (x == '0') ch = '~';
                        else if (x == '1') ch = '/';
                        else return OJ_NONE;
                    }
                    if (ki >= kl || k[ki] != ch) {
                        ok = false;
                        break;
                    }
                    ki++;
                }
                if (ok && ki == kl) found = j + 1;
                j = nskip(nv, j + 1);
            }
            if (found == OJ_NONE) return OJ_NONE;
            node = found;
        } else {
            return OJ_NONE;
        }
    }
    return node;
}

extern "C" const char *oj_get_string(const struct oj_doc *doc, oj_u32 node, oj_u32 *len)
{
    if (!valid(doc, node)) return nullptr;
    const oj_node &n = nodes_of(doc)[node];
    if (ntype(n) != OJ_T_STRING) return nullptr;
    if (len) *len = n.data;
    return pool_of(doc) + nfield(n);
}

extern "C" oj_s32 oj_get_long(const struct oj_doc *doc, oj_u32 node, oj_s32 *out)
{
    if (!valid(doc, node)) return OJ_TYPE;
    const oj_node &n = nodes_of(doc)[node];
    if (ntype(n) == OJ_T_INT) {
        *out = (s32)n.data;
        return OJ_OK;
    }
    if (ntype(n) != OJ_T_NUMBER) return OJ_TYPE;
    return oj_text_to_long(pool_of(doc) + nfield(n), n.data, out);
}

extern "C" oj_s32 oj_get_double(struct oj_env *env, const struct oj_doc *doc, oj_u32 node, oj_u32 out[2])
{
    if (!valid(doc, node)) return OJ_TYPE;
    const oj_node &n = nodes_of(doc)[node];
    if (ntype(n) == OJ_T_INT) {
        dbl_set(out, long_to_double((s32)n.data));
        return OJ_OK;
    }
    if (ntype(n) != OJ_T_NUMBER) return OJ_TYPE;
    return oj_text_to_double(env, pool_of(doc) + nfield(n), n.data, out);
}

extern "C" oj_u32 oj_get_latin1(const struct oj_doc *doc, oj_u32 node, char *buf, oj_u32 size)
{
    if (buf && size) buf[0] = 0;
    if (!valid(doc, node)) return 0;
    const oj_node &n = nodes_of(doc)[node];
    if (ntype(n) != OJ_T_STRING) return 0;
    const u8 *s = (const u8 *)pool_of(doc) + nfield(n), *end = s + n.data;
    u32 o = 0;
    while (s < end) {
        u32 c = *s, k = 1;
        if (c >= 0x80) {
            k = utf8_check(s, end);
            if (!k) k = 1, c = '?';
            else c = utf8_decode(s, k);
            if (c > 0xFF) c = '?';
        }
        s += k;
        if (buf && o + 1 < size) buf[o] = (char)c;
        o++;
    }
    if (buf && size) buf[o < size ? o : size - 1] = 0;
    return o;
}

/* ---- The calls: numbers ----------------------------------------------------------- */

extern "C" oj_s32 oj_text_to_double(struct oj_env *env, const char *text, oj_u32 len, oj_u32 out[2])
{
    if (!text) return OJ_NUMBER;
    if (len == OJ_STRLEN) len = cstr_len(text);
    Decimal dc;
    if (!split_number(text, len, dc)) return OJ_NUMBER;
    u64 b;
    s32 rc = decimal_to_double(env, dc, &b);
    if (rc == OJ_OK) dbl_set(out, b);
    return rc;
}

extern "C" oj_s32 oj_text_to_long(const char *text, oj_u32 len, oj_s32 *out)
{
    if (!text) return OJ_NUMBER;
    if (len == OJ_STRLEN) len = cstr_len(text);
    Decimal dc;
    if (!split_number(text, len, dc)) return OJ_NUMBER;
    return decimal_to_long(dc, out);
}

extern "C" oj_u32 oj_double_to_text(struct oj_env *env, const oj_u32 val[2], char *buf)
{
    u64 bits = dbl_bits(val);
    u32 n = double_text(bits, nullptr, buf);
    if (n) return n;
    if (!env) return 0;
    u32 *scratch = (u32 *)env->alloc(env, 5 * BIGW * 4);
    if (!scratch) return 0;
    n = double_text(bits, scratch, buf);
    env->free(env, scratch);
    return n;
}

/* ---- The writer --------------------------------------------------------------------- */

struct oj_writer {
    struct oj_env *env;
    u32 flags;
    char *buf;
    u32 len, cap;
    s32 err;
    u32 depth;
    bool first;         /* nothing yet in the open container */
    bool after_key;     /* an object's key is written, its value is next */
    bool done;          /* a whole top-level value is written */
    u32 *scratch;       /* the number formatter's bignums, made when first needed */
    u32 *ends;          /* oj_write_node: the open containers' ends */
    u32 ends_cap;
    u8 stack[OJ_MAXDEPTH / 8];
};

namespace {

bool w_reserve(oj_writer *w, u32 more)
{
    if (w->err) return false;
    if (w->len + more + 1 <= w->cap) return true;
    u32 cap = w->cap ? w->cap : 256;
    while (cap < w->len + more + 1) {
        if (cap >= 0x40000000u) {
            w->err = OJ_TOOBIG;
            return false;
        }
        cap *= 2;
    }
    char *nb = (char *)w->env->alloc(w->env, cap);
    if (!nb) {
        w->err = OJ_NOMEM;
        return false;
    }
    if (w->buf) {
        copy_bytes((u8 *)nb, (const u8 *)w->buf, w->len);
        w->env->free(w->env, w->buf);
    }
    w->buf = nb;
    w->cap = cap;
    return true;
}

inline bool w_put(oj_writer *w, const char *s, u32 n)
{
    if (!w_reserve(w, n)) return false;
    copy_bytes((u8 *)w->buf + w->len, (const u8 *)s, n);
    w->len += n;
    return true;
}

inline bool w_byte(oj_writer *w, char c)
{
    if (!w_reserve(w, 1)) return false;
    w->buf[w->len++] = c;
    return true;
}

bool w_newline(oj_writer *w, u32 depth)
{
    if (!(w->flags & (OJ_W_PRETTY | OJ_W_TABS))) return true;
    bool tabs = w->flags & OJ_W_TABS;
    u32 n = 1 + depth * (tabs ? 1 : 2);
    if (!w_reserve(w, n)) return false;
    char *d = w->buf + w->len;
    *d++ = '\n';
    for (u32 i = 1; i < n; i++)
        *d++ = tabs ? '\t' : ' ';
    w->len += n;
    return true;
}

inline bool w_in_object(oj_writer *w)
{
    return w->depth && (w->stack[(w->depth - 1) >> 3] >> ((w->depth - 1) & 7) & 1);
}

inline s32 w_fail(oj_writer *w, s32 code)
{
    if (!w->err) w->err = code;
    return w->err;
}

/* Before a value: is one allowed here, and its separator */
bool w_value(oj_writer *w)
{
    if (w->err) return false;
    if (w->depth == 0) {
        if (w->done) {
            if (!(w->flags & OJ_W_MULTI)) return w_fail(w, OJ_STATE), false;
            return w_byte(w, '\n');
        }
        return true;
    }
    if (w_in_object(w)) {
        if (!w->after_key) return w_fail(w, OJ_STATE), false;
        w->after_key = false;
        return true;
    }
    if (!w->first && !w_byte(w, ',')) return false;
    w->first = false;
    return w_newline(w, w->depth);
}

inline void w_scalar_done(oj_writer *w)
{
    if (w->depth == 0) w->done = true;
}

/* A string with its quotes; latin1: the bytes are ISO-8859-1 */
bool w_string(oj_writer *w, const char *str, u32 len, bool latin1)
{
    const u8 *s = (const u8 *)str, *end = s + len;
    bool ascii = w->flags & OJ_W_ASCII;
    if (!w_byte(w, '"')) return false;
    while (s < end) {
        const u8 *run = s;
        while (s < end && *s >= 0x20 && *s < 0x80 && *s != '"' && *s != '\\')
            s++;
        if (s > run && !w_put(w, (const char *)run, (u32)(s - run))) return false;
        if (s >= end) break;
        u32 c = *s;
        char esc[12];
        u32 n = 0;
        if (c < 0x80) {
            esc[n++] = '\\';
            switch (c) {
            case '"': esc[n++] = '"'; break;
            case '\\': esc[n++] = '\\'; break;
            case 8: esc[n++] = 'b'; break;
            case 12: esc[n++] = 'f'; break;
            case 10: esc[n++] = 'n'; break;
            case 13: esc[n++] = 'r'; break;
            case 9: esc[n++] = 't'; break;
            default:
                esc[n++] = 'u';
                esc[n++] = '0';
                esc[n++] = '0';
                esc[n++] = "0123456789abcdef"[c >> 4];
                esc[n++] = "0123456789abcdef"[c & 15];
            }
            s++;
            if (!w_put(w, esc, n)) return false;
            continue;
        }
        u32 cp, k;
        if (latin1) {
            cp = c;
            k = 1;
        } else {
            k = utf8_check(s, end);
            if (!k) return w_fail(w, OJ_UTF8), false;
            cp = utf8_decode(s, k);
        }
        if (!ascii) {
            if (latin1) {
                u8 t[2];
                utf8_put(t, cp);
                if (!w_put(w, (const char *)t, 2)) return false;
            } else if (!w_put(w, (const char *)s, k)) {
                return false;
            }
            s += k;
            continue;
        }
        s += k;
        u32 units[2], nu = 0;
        if (cp >= 0x10000) {
            units[nu++] = 0xD800 + ((cp - 0x10000) >> 10);
            units[nu++] = 0xDC00 + ((cp - 0x10000) & 0x3FF);
        } else {
            units[nu++] = cp;
        }
        for (u32 i = 0; i < nu; i++) {
            esc[n++] = '\\';
            esc[n++] = 'u';
            for (int sh = 12; sh >= 0; sh -= 4)
                esc[n++] = "0123456789abcdef"[units[i] >> sh & 15];
        }
        if (!w_put(w, esc, n)) return false;
    }
    return w_byte(w, '"');
}

s32 w_key(oj_writer *w, const char *key, u32 len, bool latin1)
{
    if (w->err) return w->err;
    if (!w_in_object(w) || w->after_key) return w_fail(w, OJ_STATE);
    if (!w->first && !w_byte(w, ',')) return w->err;
    w->first = false;
    if (!w_newline(w, w->depth)) return w->err;
    if (!w_string(w, key, len, latin1)) return w->err;
    if (!w_byte(w, ':')) return w->err;
    if ((w->flags & (OJ_W_PRETTY | OJ_W_TABS)) && !w_byte(w, ' ')) return w->err;
    w->after_key = true;
    return OJ_OK;
}

s32 w_raw_value(oj_writer *w, const char *s, u32 n)
{
    if (!w_value(w)) return w->err;
    if (!w_put(w, s, n)) return w->err;
    w_scalar_done(w);
    return OJ_OK;
}

} /* namespace */

extern "C" struct oj_writer *oj_new_writer(struct oj_env *env, oj_u32 flags)
{
    oj_writer *w = (oj_writer *)env->alloc(env, sizeof(oj_writer));
    if (!w) return nullptr;
    w->env = env;
    w->flags = flags;
    w->buf = nullptr;
    w->len = w->cap = 0;
    w->scratch = nullptr;
    w->ends = nullptr;
    w->ends_cap = 0;
    oj_reset_writer(w);
    return w;
}

extern "C" void oj_free_writer(struct oj_writer *w)
{
    if (!w) return;
    oj_env *env = w->env;
    if (w->buf) env->free(env, w->buf);
    if (w->scratch) env->free(env, w->scratch);
    if (w->ends) env->free(env, w->ends);
    env->free(env, w);
}

extern "C" void oj_reset_writer(struct oj_writer *w)
{
    w->len = 0;
    w->err = OJ_OK;
    w->depth = 0;
    w->first = true;
    w->after_key = false;
    w->done = false;
}

extern "C" oj_s32 oj_write_begin(struct oj_writer *w, oj_u32 type)
{
    if (type != OJ_T_ARRAY && type != OJ_T_OBJECT) return w_fail(w, OJ_STATE);
    if (w->depth >= OJ_MAXDEPTH) return w_fail(w, OJ_DEPTH);
    if (!w_value(w)) return w->err;
    if (!w_byte(w, type == OJ_T_OBJECT ? '{' : '[')) return w->err;
    u32 d = w->depth++;
    if (type == OJ_T_OBJECT) w->stack[d >> 3] |= (u8)(1u << (d & 7));
    else w->stack[d >> 3] &= (u8)~(1u << (d & 7));
    w->first = true;
    return OJ_OK;
}

extern "C" oj_s32 oj_write_end(struct oj_writer *w)
{
    if (w->err) return w->err;
    if (!w->depth || w->after_key) return w_fail(w, OJ_STATE);
    bool obj = w_in_object(w);
    w->depth--;
    if (!w->first && !w_newline(w, w->depth)) return w->err;
    if (!w_byte(w, obj ? '}' : ']')) return w->err;
    w->first = false;
    w_scalar_done(w);
    return OJ_OK;
}

extern "C" oj_s32 oj_write_key(struct oj_writer *w, const char *key, oj_u32 len)
{
    if (!key) return w_fail(w, OJ_STATE);
    if (len == OJ_STRLEN) len = cstr_len(key);
    return w_key(w, key, len, w->flags & OJ_W_LATIN1);
}

extern "C" oj_s32 oj_write_string(struct oj_writer *w, const char *str, oj_u32 len)
{
    if (!str) return w_fail(w, OJ_STATE);
    if (len == OJ_STRLEN) len = cstr_len(str);
    if (!w_value(w)) return w->err;
    if (!w_string(w, str, len, w->flags & OJ_W_LATIN1)) return w->err;
    w_scalar_done(w);
    return OJ_OK;
}

extern "C" oj_s32 oj_write_long(struct oj_writer *w, oj_s32 val)
{
    char t[12];
    u32 n = 0;
    if (val < 0) t[n++] = '-';
    n += u64_to_text(val < 0 ? 0u - (u32)val : (u32)val, t + n);
    return w_raw_value(w, t, n);
}

extern "C" oj_s32 oj_write_double(struct oj_writer *w, const oj_u32 val[2])
{
    if (w->err) return w->err;
    char t[32];
    u64 bits = dbl_bits(val);
    u32 n = double_text(bits, nullptr, t);
    if (!n) {
        if (!w->scratch) {
            w->scratch = (u32 *)w->env->alloc(w->env, 5 * BIGW * 4);
            if (!w->scratch) return w_fail(w, OJ_NOMEM);
        }
        n = double_text(bits, w->scratch, t);
    }
    return w_raw_value(w, t, n);
}

extern "C" oj_s32 oj_write_bool(struct oj_writer *w, oj_s32 val)
{
    return val ? w_raw_value(w, "true", 4) : w_raw_value(w, "false", 5);
}

extern "C" oj_s32 oj_write_null(struct oj_writer *w)
{
    return w_raw_value(w, "null", 4);
}

extern "C" oj_s32 oj_write_number_text(struct oj_writer *w, const char *text, oj_u32 len)
{
    if (w->err) return w->err;
    if (!text) return w_fail(w, OJ_NUMBER);
    if (len == OJ_STRLEN) len = cstr_len(text);
    Decimal dc;
    if (!split_number(text, len, dc)) return w_fail(w, OJ_NUMBER);
    return w_raw_value(w, text, len);
}

extern "C" oj_s32 oj_write_node(struct oj_writer *w, const struct oj_doc *doc, oj_u32 node)
{
    if (w->err) return w->err;
    if (!valid(doc, node)) return w_fail(w, OJ_TYPE);
    const oj_node *nv = nodes_of(doc);
    const char *pool = pool_of(doc);
    u32 stop = nskip(nv, node);
    u32 open = 0;               /* containers this call opened */
    for (u32 j = node; j < stop || open;) {
        if (open && j == w->ends[open - 1]) {
            if (oj_write_end(w)) return w->err;
            open--;
            continue;
        }
        const oj_node &n = nv[j];
        u32 t = ntype(n);
        if (open && w_in_object(w) && !w->after_key) {
            if (w_key(w, pool + nfield(n), n.data, false)) return w->err;
            j++;
            continue;
        }
        s32 rc;
        switch (t) {
        case OJ_T_NULL: rc = oj_write_null(w); break;
        case OJ_T_FALSE: rc = oj_write_bool(w, 0); break;
        case OJ_T_TRUE: rc = oj_write_bool(w, 1); break;
        case OJ_T_INT: rc = oj_write_long(w, (s32)n.data); break;
        case OJ_T_NUMBER: rc = w_raw_value(w, pool + nfield(n), n.data); break;
        case OJ_T_STRING:
            rc = OJ_OK;
            if (!w_value(w) || !w_string(w, pool + nfield(n), n.data, false)) rc = w->err;
            else w_scalar_done(w);
            break;
        default:
            if (open == w->ends_cap) {
                u32 nc = w->ends_cap ? w->ends_cap * 2 : 32;
                u32 *ne = (u32 *)w->env->alloc(w->env, nc * 4);
                if (!ne) return w_fail(w, OJ_NOMEM);
                for (u32 i = 0; i < open; i++)
                    ne[i] = w->ends[i];
                if (w->ends) w->env->free(w->env, w->ends);
                w->ends = ne;
                w->ends_cap = nc;
            }
            rc = oj_write_begin(w, t);
            if (!rc) w->ends[open++] = n.data;
            break;
        }
        if (rc) return rc;
        j++;
    }
    return OJ_OK;
}

extern "C" const char *oj_writer_text(struct oj_writer *w, oj_u32 *len)
{
    if (w->err || w->depth || !w->done || !w->buf) return nullptr;
    w->buf[w->len] = 0;     /* w_reserve always keeps a byte for this */
    if (len) *len = w->len;
    return w->buf;
}

extern "C" oj_s32 oj_writer_error(struct oj_writer *w)
{
    return w->err;
}

extern "C" char *oj_stringify(struct oj_env *env, const struct oj_doc *doc, oj_u32 node, oj_u32 flags,
                              oj_u32 *len)
{
    oj_writer *w = oj_new_writer(env, flags & ~(OJ_W_LATIN1 | OJ_W_MULTI));
    if (!w) return nullptr;
    char *text = nullptr;
    if (oj_write_node(w, doc, node) == OJ_OK && oj_writer_text(w, len)) {
        text = w->buf;
        w->buf = nullptr;
    }
    oj_free_writer(w);
    return text;
}

extern "C" void oj_free_text(struct oj_env *env, char *text)
{
    if (text) env->free(env, text);
}

/* ---- The streaming reader --------------------------------------------------------------- */

/* A byte at a time, so a token may be split anywhere between feeds:
 * strings and numbers collect in tok (unescaped), which grows as needed. */
namespace {
enum RState : u8 {
    R_VALUE,            /* a value must come (top, after ':' or ',' in an array) */
    R_VALUE_OR_CLOSE,   /* after '[' (or ',' with JSONF_TRAILING) */
    R_KEY_OR_CLOSE,     /* after '{' (or ',' with JSONF_TRAILING) */
    R_KEY,              /* after ',' in an object */
    R_COLON,
    R_AFTER,            /* a value just ended */
    R_STRING,
    R_NUMBER,
    R_LITERAL,
    R_SLASH,            /* a '/': a comment starts? */
    R_LINE_COMMENT,
    R_BLOCK_COMMENT,
    R_BLOCK_STAR,
    R_DONE,             /* JSONEV_DONE given */
    R_ERROR
};
enum NState : u8 { N_SIGN, N_ZERO, N_INT, N_DOT, N_FRAC, N_E, N_ESIGN, N_EXP };
enum EState : u8 { E_NONE, E_BACKSLASH, E_HEX };
} /* namespace */

struct oj_reader {
    struct oj_env *env;
    u32 flags;
    const u8 *in, *in_end;
    bool eof;
    u8 state, ret_state;    /* ret_state: where a comment returns to */
    u8 num, esc;
    bool is_key;
    bool seen;              /* a top-level value has been read */
    u8 bom;                 /* bytes of a UTF-8 byte order mark taken */
    u32 depth;
    char *tok;
    u32 toklen, tokcap;
    u32 hex, hexn, high;    /* \u escape: value so far, digits, a pending high surrogate */
    u32 need, lo, hi;       /* UTF-8: continuation bytes still to come, the next one's range */
    const char *lit;
    u32 lit_ev;
    u32 offset, line, col;  /* of the next byte */
    oj_error err;
    u8 stack[OJ_MAXDEPTH / 8];
};

namespace {

bool r_tok_reserve(oj_reader *r, u32 more)
{
    if (r->toklen + more + 1 <= r->tokcap) return true;
    u32 cap = r->tokcap ? r->tokcap * 2 : 64;
    while (cap < r->toklen + more + 1) {
        if (cap >= 0x10000000u) return false;
        cap *= 2;
    }
    char *nt = (char *)r->env->alloc(r->env, cap);
    if (!nt) return false;
    if (r->tok) {
        copy_bytes((u8 *)nt, (const u8 *)r->tok, r->toklen);
        r->env->free(r->env, r->tok);
    }
    r->tok = nt;
    r->tokcap = cap;
    return true;
}

s32 r_fail(oj_reader *r, s32 code)
{
    r->state = R_ERROR;
    r->err.code = code;
    r->err.offset = r->offset;
    r->err.line = r->line;
    r->err.column = r->col + 1;
    return OJ_EV_ERROR;
}

/* Part of a byte order mark: its first byte cannot start a value */
s32 r_bom_fail(oj_reader *r)
{
    r_fail(r, OJ_SYNTAX);
    r->err.offset = 0;
    r->err.line = r->err.column = 1;
    return OJ_EV_ERROR;
}

inline bool r_in_object(oj_reader *r)
{
    return r->depth && (r->stack[(r->depth - 1) >> 3] >> ((r->depth - 1) & 7) & 1);
}

/* Take the byte at r->in */
inline void r_take(oj_reader *r)
{
    u8 c = *r->in++;
    r->offset++;
    if (c == '\n') {
        r->line++;
        r->col = 0;
    } else {
        r->col++;
    }
}

bool r_put_cp(oj_reader *r, u32 cp)
{
    if (!r_tok_reserve(r, 4)) return false;
    r->toklen += utf8_put((u8 *)r->tok + r->toklen, cp);
    return true;
}

/* The value starting with c (not yet taken); returns an event, or
 * OJ_EV_MORE to go on reading */
s32 r_start_value(oj_reader *r, u32 c)
{
    if (c == '{' || c == '[') {
        if (r->depth >= OJ_MAXDEPTH) return r_fail(r, OJ_DEPTH);
        u32 d = r->depth++;
        if (c == '{') r->stack[d >> 3] |= (u8)(1u << (d & 7));
        else r->stack[d >> 3] &= (u8)~(1u << (d & 7));
        r_take(r);
        r->state = c == '{' ? R_KEY_OR_CLOSE : R_VALUE_OR_CLOSE;
        return c == '{' ? OJ_EV_OBJECT : OJ_EV_ARRAY;
    }
    if (c == '"') {
        r_take(r);
        r->state = R_STRING;
        r->is_key = false;
        r->toklen = 0;
        r->esc = E_NONE;
        r->need = 0;
        r->high = 0;
        return OJ_EV_MORE;
    }
    if (c == '-' || is_digit(c)) {
        r->state = R_NUMBER;
        r->toklen = 0;
        r->num = c == '-' ? N_SIGN : c == '0' ? N_ZERO : N_INT;
        if (!r_tok_reserve(r, 1)) return r_fail(r, OJ_NOMEM);
        r->tok[r->toklen++] = (char)c;
        r_take(r);
        return OJ_EV_MORE;
    }
    if (c == 't') r->lit = "true", r->lit_ev = OJ_EV_TRUE;
    else if (c == 'f') r->lit = "false", r->lit_ev = OJ_EV_FALSE;
    else if (c == 'n') r->lit = "null", r->lit_ev = OJ_EV_NULL;
    else return r_fail(r, OJ_SYNTAX);
    r->state = R_LITERAL;
    r->lit++;
    r_take(r);
    return OJ_EV_MORE;
}

/* A value ended: what comes after it */
inline void r_value_done(oj_reader *r)
{
    r->state = R_AFTER;
    if (!r->depth) r->seen = true;
}

/* The close bracket c (not yet taken), if it closes the open container */
s32 r_close(oj_reader *r, u32 c)
{
    bool obj = r_in_object(r);
    if (!r->depth || c != (obj ? (u32)'}' : (u32)']')) return r_fail(r, OJ_SYNTAX);
    r_take(r);
    r->depth--;
    r_value_done(r);
    return obj ? OJ_EV_OBJECT_END : OJ_EV_ARRAY_END;
}

/* A number ends at c (not taken), or at the end of the text */
s32 r_number_end(oj_reader *r)
{
    if (r->num == N_SIGN || r->num == N_DOT || r->num == N_E || r->num == N_ESIGN)
        return r_fail(r, r->in >= r->in_end ? OJ_EOF : OJ_NUMBER);
    r->tok[r->toklen] = 0;
    r_value_done(r);
    return OJ_EV_NUMBER;
}

/* One byte of a string; returns an event, OJ_EV_MORE to go on */
s32 r_string_byte(oj_reader *r, u32 c)
{
    if (r->need) {
        if (c < r->lo || c > r->hi) return r_fail(r, OJ_UTF8);
        r->need--;
        r->lo = 0x80;
        r->hi = 0xBF;
        if (!r_tok_reserve(r, 1)) return r_fail(r, OJ_NOMEM);
        r->tok[r->toklen++] = (char)c;
        r_take(r);
        return OJ_EV_MORE;
    }
    if (r->esc == E_BACKSLASH) {
        u32 cp;
        switch (c) {
        case '"': cp = '"'; break;
        case '\\': cp = '\\'; break;
        case '/': cp = '/'; break;
        case 'b': cp = 8; break;
        case 'f': cp = 12; break;
        case 'n': cp = 10; break;
        case 'r': cp = 13; break;
        case 't': cp = 9; break;
        case 'u':
            r->esc = E_HEX;
            r->hex = r->hexn = 0;
            r_take(r);
            return OJ_EV_MORE;
        default:
            return r_fail(r, OJ_ESCAPE);
        }
        if (r->high) {
            if (!r_put_cp(r, REPLACEMENT)) return r_fail(r, OJ_NOMEM);
            r->high = 0;
        }
        if (!r_put_cp(r, cp)) return r_fail(r, OJ_NOMEM);
        r->esc = E_NONE;
        r_take(r);
        return OJ_EV_MORE;
    }
    if (r->esc == E_HEX) {
        s32 h = hex_value(c);
        if (h < 0) return r_fail(r, OJ_ESCAPE);
        r->hex = r->hex << 4 | (u32)h;
        r_take(r);
        if (++r->hexn < 4) return OJ_EV_MORE;
        r->esc = E_NONE;
        u32 cp = r->hex;
        if (r->high) {
            if (cp >= 0xDC00 && cp < 0xE000) {
                cp = 0x10000 + ((r->high - 0xD800) << 10) + (cp - 0xDC00);
                r->high = 0;
                if (!r_put_cp(r, cp)) return r_fail(r, OJ_NOMEM);
                return OJ_EV_MORE;
            }
            r->high = 0;
            if (!r_put_cp(r, REPLACEMENT)) return r_fail(r, OJ_NOMEM);
        }
        if (cp >= 0xD800 && cp < 0xDC00) {
            r->high = cp;
            return OJ_EV_MORE;
        }
        if (cp >= 0xDC00 && cp < 0xE000) cp = REPLACEMENT;
        if (!r_put_cp(r, cp)) return r_fail(r, OJ_NOMEM);
        return OJ_EV_MORE;
    }
    /* E_NONE */
    if (r->high && c != '\\') {
        r->high = 0;
        if (!r_put_cp(r, REPLACEMENT)) return r_fail(r, OJ_NOMEM);
    }
    if (c == '"') {
        r_take(r);
        r->tok[r->toklen] = 0;
        if (r->is_key) {
            r->state = R_COLON;
            return OJ_EV_KEY;
        }
        r_value_done(r);
        return OJ_EV_STRING;
    }
    if (c == '\\') {
        r->esc = E_BACKSLASH;
        r_take(r);
        return OJ_EV_MORE;
    }
    if (c < 0x20) return r_fail(r, OJ_CONTROL);
    if (c >= 0x80) {
        if (r->flags & OJ_F_LATIN1) {
            if (!r_put_cp(r, c)) return r_fail(r, OJ_NOMEM);
            r_take(r);
            return OJ_EV_MORE;
        }
        r->need = utf8_lead(c, &r->lo, &r->hi);
        if (!r->need) return r_fail(r, OJ_UTF8);
    }
    if (!r_tok_reserve(r, 1)) return r_fail(r, OJ_NOMEM);
    r->tok[r->toklen++] = (char)c;
    r_take(r);
    return OJ_EV_MORE;
}

/* Whitespace or a comment's start at c: true if taken */
bool r_space(oj_reader *r, u32 c)
{
    if (is_ws(c)) {
        r_take(r);
        return true;
    }
    if (c == '/' && (r->flags & OJ_F_COMMENTS)) {
        r->ret_state = r->state;
        r->state = R_SLASH;
        r_take(r);
        return true;
    }
    return false;
}

} /* namespace */

extern "C" struct oj_reader *oj_new_reader(struct oj_env *env, oj_u32 flags)
{
    oj_reader *r = (oj_reader *)env->alloc(env, sizeof(oj_reader));
    if (!r) return nullptr;
    r->env = env;
    r->flags = flags;
    r->in = r->in_end = nullptr;
    r->eof = false;
    r->state = R_VALUE;
    r->ret_state = R_VALUE;
    r->num = 0;
    r->esc = E_NONE;
    r->is_key = false;
    r->seen = false;
    r->bom = 0;
    r->depth = 0;
    r->tok = nullptr;
    r->toklen = r->tokcap = 0;
    r->hex = r->hexn = r->high = 0;
    r->need = 0;
    r->lo = 0x80;
    r->hi = 0xBF;
    r->lit = nullptr;
    r->lit_ev = 0;
    r->offset = 0;
    r->line = 1;
    r->col = 0;
    r->err.code = OJ_OK;
    r->err.offset = r->err.line = r->err.column = 0;
    if (!r_tok_reserve(r, 64)) {
        env->free(env, r);
        return nullptr;
    }
    r->tok[0] = 0;
    return r;
}

extern "C" void oj_free_reader(struct oj_reader *r)
{
    if (!r) return;
    if (r->tok) r->env->free(r->env, r->tok);
    r->env->free(r->env, r);
}

extern "C" void oj_reader_feed(struct oj_reader *r, const oj_u8 *buf, oj_u32 len)
{
    if (!len || !buf) {
        r->eof = true;
        r->in = r->in_end = nullptr;
        return;
    }
    r->in = buf;
    r->in_end = buf + len;
}

extern "C" oj_s32 oj_reader_next(struct oj_reader *r)
{
    if (r->state == R_ERROR) return OJ_EV_ERROR;
    if (r->state == R_DONE) return OJ_EV_DONE;
    for (;;) {
        if (r->in >= r->in_end) {
            if (!r->eof) return OJ_EV_MORE;
            /* the end of the text */
            switch (r->state) {
            case R_NUMBER:
                return r_number_end(r);
            case R_LINE_COMMENT:
                r->state = r->ret_state;
                continue;
            case R_SLASH:
                return r_fail(r, OJ_EOF);
            case R_AFTER:
                if (r->depth) return r_fail(r, OJ_EOF);
                r->state = R_DONE;
                return OJ_EV_DONE;
            case R_VALUE:
                if (r->bom && r->bom < 3) return r_bom_fail(r);
                if (!r->depth && (r->flags & OJ_F_MULTI)) {
                    r->state = R_DONE;
                    return OJ_EV_DONE;
                }
                return r_fail(r, OJ_EOF);
            default:
                return r_fail(r, OJ_EOF);
            }
        }
        u32 c = *r->in;
        s32 ev;
        switch (r->state) {
        case R_VALUE:
            if (r->offset == r->bom && r->bom < 3 && !(r->flags & OJ_F_LATIN1)) {
                /* a UTF-8 byte order mark is allowed (RFC 8259 section 8.1) */
                if (c == (u8)"\xEF\xBB\xBF"[r->bom]) {
                    r->bom++;
                    r_take(r);
                    continue;
                }
                if (r->bom) return r_bom_fail(r);
            }
            if (r_space(r, c)) continue;
            if (r->depth && (r->flags & OJ_F_TRAILING) && !r_in_object(r) && c == ']')
                return r_close(r, c);
            ev = r_start_value(r, c);
            if (ev) return ev;
            continue;
        case R_VALUE_OR_CLOSE:
            if (r_space(r, c)) continue;
            if (c == ']') return r_close(r, c);
            ev = r_start_value(r, c);
            if (ev) return ev;
            continue;
        case R_KEY_OR_CLOSE:
        case R_KEY:
            if (r_space(r, c)) continue;
            if (c == '}' && (r->state == R_KEY_OR_CLOSE || (r->flags & OJ_F_TRAILING))) return r_close(r, c);
            if (c != '"') return r_fail(r, OJ_SYNTAX);
            r_take(r);
            r->state = R_STRING;
            r->is_key = true;
            r->toklen = 0;
            r->esc = E_NONE;
            r->need = 0;
            r->high = 0;
            continue;
        case R_COLON:
            if (r_space(r, c)) continue;
            if (c != ':') return r_fail(r, OJ_SYNTAX);
            r_take(r);
            r->state = R_VALUE;
            continue;
        case R_AFTER:
            if (r_space(r, c)) continue;
            if (!r->depth) {
                if (!(r->flags & OJ_F_MULTI)) return r_fail(r, OJ_TRAILING);
                r->state = R_VALUE;
                continue;
            }
            if (c == ',') {
                r_take(r);
                if (r_in_object(r)) r->state = R_KEY;
                else r->state = (r->flags & OJ_F_TRAILING) ? R_VALUE_OR_CLOSE : R_VALUE;
                continue;
            }
            return r_close(r, c);
        case R_STRING:
            ev = r_string_byte(r, c);
            if (ev) return ev;
            continue;
        case R_NUMBER: {
            u8 n = r->num, next = 0xFF;
            if (is_digit(c)) {
                if (n == N_ZERO) return r_fail(r, OJ_NUMBER);
                next = n == N_SIGN ? (u8)(c == '0' ? N_ZERO : N_INT) : n == N_DOT ? (u8)N_FRAC
                     : n == N_E || n == N_ESIGN ? (u8)N_EXP : n;
            } else if (c == '.' && (n == N_ZERO || n == N_INT)) {
                next = N_DOT;
            } else if ((c == 'e' || c == 'E') && (n == N_ZERO || n == N_INT || n == N_FRAC)) {
                next = N_E;
            } else if ((c == '+' || c == '-') && n == N_E) {
                next = N_ESIGN;
            }
            if (next == 0xFF) return r_number_end(r);
            r->num = next;
            if (!r_tok_reserve(r, 1)) return r_fail(r, OJ_NOMEM);
            r->tok[r->toklen++] = (char)c;
            r_take(r);
            continue;
        }
        case R_LITERAL:
            if (c != (u8)*r->lit) return r_fail(r, OJ_SYNTAX);
            r_take(r);
            if (!*++r->lit) {
                r_value_done(r);
                return (s32)r->lit_ev;
            }
            continue;
        case R_SLASH:
            if (c == '/') r->state = R_LINE_COMMENT;
            else if (c == '*') r->state = R_BLOCK_COMMENT;
            else return r_fail(r, OJ_SYNTAX);
            r_take(r);
            continue;
        case R_LINE_COMMENT:
            if (c == '\n') r->state = r->ret_state;
            r_take(r);
            continue;
        case R_BLOCK_COMMENT:
        case R_BLOCK_STAR:
            r_take(r);
            if (c == '*') r->state = R_BLOCK_STAR;
            else if (c == '/' && r->state == R_BLOCK_STAR) r->state = r->ret_state;
            else r->state = R_BLOCK_COMMENT;
            continue;
        default:
            return r_fail(r, OJ_SYNTAX);
        }
    }
}

extern "C" const char *oj_reader_text(struct oj_reader *r, oj_u32 *len)
{
    if (len) *len = r->toklen;
    return r->tok;
}

extern "C" oj_u32 oj_reader_depth(struct oj_reader *r)
{
    return r->depth;
}

extern "C" oj_s32 oj_reader_error(struct oj_reader *r, struct oj_error *err)
{
    if (err) {
        err->code = r->err.code;
        err->offset = r->err.offset;
        err->line = r->err.line;
        err->column = r->err.column;
    }
    return r->err.code;
}

/* ---- Checking a block ---------------------------------------------------------------------- */

extern "C" oj_s32 oj_check_doc(const struct oj_doc *doc, oj_u32 size)
{
    if (!doc || ((unsigned long)doc & 1) || size < HEADER) return OJ_BADDOC;
    if (doc->magic != OJ_MAGIC || doc->size > size || doc->size < HEADER) return OJ_BADDOC;
    u32 nodes = doc->nodes;
    if (!nodes || nodes > OJ_FIELD_MAX || doc->node_off != HEADER) return OJ_BADDOC;
    if (doc->pool_off != HEADER + nodes * 8 || doc->pool_off > doc->size) return OJ_BADDOC;
    u32 pool_size = doc->pool_size;
    if (pool_size > doc->size - doc->pool_off || pool_size > OJ_FIELD_MAX + 1) return OJ_BADDOC;
    const oj_node *nv = nodes_of(doc);
    const u8 *pool = (const u8 *)pool_of(doc);
    if (nskip(nv, 0) != nodes) return OJ_BADDOC;
    for (u32 i = 0; i < nodes; i++) {
        const oj_node &n = nv[i];
        u32 t = ntype(n);
        switch (t) {
        case OJ_T_NULL:
        case OJ_T_FALSE:
        case OJ_T_TRUE:
        case OJ_T_INT:
            break;
        case OJ_T_NUMBER:
        case OJ_T_STRING: {
            u32 off = nfield(n), len = n.data;
            if (off >= pool_size || len >= pool_size - off || pool[off + len]) return OJ_BADDOC;
            const u8 *s = pool + off, *e = s + len;
            if (t == OJ_T_NUMBER) {
                Decimal dc;
                if (!split_number((const char *)s, len, dc)) return OJ_BADDOC;
            } else {
                while (s < e) {
                    if (*s < 0x80) {
                        s++;
                        continue;
                    }
                    u32 k = utf8_check(s, e + 1);   /* e is the NUL: in bounds, never a continuation */
                    if (!k || s + k > e) return OJ_BADDOC;
                    s += k;
                }
            }
            break;
        }
        case OJ_T_ARRAY:
        case OJ_T_OBJECT: {
            /* its children end exactly at its end, each skip moving on */
            u32 end = n.data;
            if (end <= i || end > nodes) return OJ_BADDOC;
            u32 j = i + 1;
            for (u32 c = nfield(n); c; c--) {
                if (t == OJ_T_OBJECT) {
                    if (j >= end || ntype(nv[j]) != OJ_T_STRING) return OJ_BADDOC;
                    j++;
                }
                if (j >= end) return OJ_BADDOC;
                u32 k = nskip(nv, j);
                if (k <= j || k > end) return OJ_BADDOC;
                j = k;
            }
            if (j != end) return OJ_BADDOC;
            break;
        }
        default:
            return OJ_BADDOC;
        }
    }
    return OJ_OK;
}

/* ---- Error strings ------------------------------------------------------------------------------ */

namespace {
constexpr const char *ERRORS[OJ_ERRORS] = {
    "no error",
    "out of memory",
    "unexpected character",
    "unexpected end of text",
    "control character in a string",
    "bad escape in a string",
    "not UTF-8",
    "malformed number",
    "nested too deep",
    "too big",
    "more text after the value",
    "buffer too small",
    "not a JSON document block",
    "writer call out of order",
    "wrong type",
    "number out of range",
};
}

extern "C" const char *oj_error_string(oj_s32 code)
{
    return code >= 0 && code < OJ_ERRORS ? ERRORS[code] : "unknown error";
}
