/*
 * weblayout: UTF-8 to ISO-8859-1 (see wl_text.h). The converter GFOpenMail's
 * oam_text.c has (oam_utf8_to_latin1), brought into weblayout as its own so
 * the library needs nothing of GFOpenMail.
 * MIT, Copyright (c) 2026 AmigaChrome Team.
 */
#include "wl_text.h"

/* U+0100-U+017F (Latin Extended-A) to its base letter, for Latin-1 fonts
 * (generated from Unicode's decompositions; GFOpenMail's oam_charsets.h) */
static const char latin_ext_a_base[129] =
    "AaAaAaCcCcCcCcDd"
    "DdEeEeEeEeEeGgGg"
    "GgGgHhHhIiIiIiIi"
    "IiIiJjKkkLlLlLlL"
    "lLlNnNnNnnNnOoOo"
    "OoOoRrRrRrSsSsSs"
    "SsTtTtTtUuUuUuUu"
    "UuUuWwYyYZzZzZzs";

/* one character from UTF-8: its length in *n, or 0xFFFD and 1 for a bad byte */
static unsigned long get_utf8(const unsigned char *p, size_t left, size_t *n)
{
    unsigned long c = p[0], min;
    size_t len, i;
    if (c < 0x80) { *n = 1; return c; }
    if (c >= 0xc2 && c < 0xe0) { len = 2; c &= 0x1f; min = 0x80; }
    else if (c >= 0xe0 && c < 0xf0) { len = 3; c &= 0x0f; min = 0x800; }
    else if (c >= 0xf0 && c < 0xf5) { len = 4; c &= 0x07; min = 0x10000; }
    else { *n = 1; return 0xfffd; }
    if (len > left) { *n = 1; return 0xfffd; }
    for (i = 1; i < len; i++) {
        if ((p[i] & 0xc0) != 0x80) { *n = 1; return 0xfffd; }
        c = c << 6 | (p[i] & 63);
    }
    if (c < min || c > 0x10ffff || (c >= 0xd800 && c < 0xe000)) { *n = 1; return 0xfffd; }
    *n = len;
    return c;
}

/* the Latin-1 for c, at most 4 bytes, in out; its length */
static size_t latin1_for(unsigned long c, char *out)
{
    const char *s = NULL;
    size_t n = 0;
    if (c < 0x80 || (c >= 0xa0 && c < 0x100)) { out[0] = (char)c; return 1; }
    if (c >= 0x100 && c < 0x180) {
        if (c == 0x152) s = "OE";
        else if (c == 0x153) s = "oe";
        else if (c == 0x132) s = "IJ";
        else if (c == 0x133) s = "ij";
        else { out[0] = latin_ext_a_base[c - 0x100]; return 1; }
    } else {
        switch (c) {
        case 0x2018: case 0x2019: case 0x201a: case 0x2032: case 0x02bc: s = "'"; break;
        case 0x201c: case 0x201d: case 0x201e: case 0x2033: s = "\""; break;
        case 0x2013: case 0x2014: case 0x2212: case 0x2010: case 0x2011: s = "-"; break;
        case 0x2026: s = "..."; break;
        case 0x20ac: s = "EUR"; break;
        case 0x2022: out[0] = (char)0xb7; return 1;
        case 0x2122: s = "(TM)"; break;
        case 0x2039: s = "<"; break;
        case 0x203a: s = ">"; break;
        case 0x0178: s = "Y"; break;
        case 0x2009: case 0x200a: case 0x2002: case 0x2003: case 0x202f: s = " "; break;
        case 0x200b: case 0x200c: case 0x200d: case 0xfeff: case 0x00ad: return 0;  /* invisible: dropped */
        default: s = "?"; break;
        }
    }
    while (s[n]) { out[n] = s[n]; n++; }
    return n;
}

char *wl_utf8_to_latin1(wl_pool *pool, const char *utf8, size_t length)
{
    const unsigned char *p = (const unsigned char *)utf8;
    size_t left = utf8 ? length : 0, w = 0;
    /* each character is at least one byte and at most four out ("(TM)" is
     * three bytes in) */
    char *out = (char *)wl_alloc(pool, left * 2 + 1);
    if (!out) return NULL;
    while (left) {
        size_t n;
        unsigned long c = get_utf8(p, left, &n);
        p += n;
        left -= n;
        w += latin1_for(c, out + w);
    }
    out[w] = 0;
    return out;
}
