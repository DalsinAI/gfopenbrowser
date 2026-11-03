/* unicode.library, our own code: text breaking (PLAN.md step 7, piece 2).
 * Copyright (c) 2026 AmiCradle Team. MIT.
 *
 * The ubrk_* calls: grapheme clusters, words and sentences (UAX #29), lines
 * (UAX #14, with the strict, normal and loose variants of CSS line-break,
 * chosen by the locale's "lb" keyword), and title-case segments. Written
 * from the Unicode 17 standards; where ICU's break rules differ from them
 * (word rule status values, letters of scripts written without spaces,
 * kana and Han), they behave as ICU 78's do, because the engine and
 * JavaScriptCore were written against ICU.
 *
 * Every break position is decided from the text around it, without state,
 * so following(), preceding() and isBoundary() cost the same anywhere in
 * the text. Positions are UTF-16 offsets; a UText (WebKit's text
 * providers) is copied into UTF-16 once by ubrk_setUText.
 */
#include <stdlib.h>
#include <string.h>

#include <unicode/ubrk.h>
#include <unicode/utext.h>

#include "uo_brk_data.h"
#include "uo_own.h"

#define UO_BI_MAGIC 0x55424b31      /* "UBK1" */
#define LB_NSX UO_LB_COUNT          /* loose: the iteration marks, nonstarters no more */

enum { NOBRK = 0, BRK = 1, HARD = 2 };

struct uo_bi {
    uint32_t magic;
    int type;                   /* UBreakIteratorType */
    int lbv;                    /* line: 0 strict, 1 normal, 2 loose */
    const UChar *text;
    int32_t len;
    int32_t pos;
    UChar *own;                 /* a UText's copy, or NULL */
    int32_t *map;               /* UTF-16 offset -> native index (len + 1), or NULL */
    int32_t nativeLen;
};

/* ---- the text ----------------------------------------------------------------- */

static inline UChar32 cp_at(const struct uo_bi *b, int32_t i)
{
    UChar32 c = b->text[i];
    if ((c & 0xFC00) == 0xD800 && i + 1 < b->len && (b->text[i + 1] & 0xFC00) == 0xDC00)
        c = (c << 10) + b->text[i + 1] - ((0xD800 << 10) + 0xDC00 - 0x10000);
    return c;
}

static inline int32_t next_i(const struct uo_bi *b, int32_t i)
{
    if ((b->text[i] & 0xFC00) == 0xD800 && i + 1 < b->len && (b->text[i + 1] & 0xFC00) == 0xDC00)
        return i + 2;
    return i + 1;
}

static inline int32_t prev_i(const struct uo_bi *b, int32_t i)
{
    if ((b->text[i - 1] & 0xFC00) == 0xDC00 && i >= 2 && (b->text[i - 2] & 0xFC00) == 0xD800)
        return i - 2;
    return i - 1;
}

/* an offset moved back to the start of its code point, inside [0, len] */
static int32_t snap(const struct uo_bi *b, int32_t i)
{
    if (i <= 0)
        return 0;
    if (i >= b->len)
        return b->len;
    if ((b->text[i] & 0xFC00) == 0xDC00 && (b->text[i - 1] & 0xFC00) == 0xD800)
        return i - 1;
    return i;
}

static inline const struct uo_brk *REC(UChar32 c)
{
    return UO_BRK(c);
}

/* the code point at *j, and *j past it */
static inline UChar32 cp_step(const struct uo_bi *b, int32_t *j)
{
    UChar32 c = b->text[*j];
    if ((c & 0xFC00) == 0xD800 && *j + 1 < b->len && (b->text[*j + 1] & 0xFC00) == 0xDC00) {
        c = (c << 10) + b->text[*j + 1] - ((0xD800 << 10) + 0xDC00 - 0x10000);
        *j += 2;
    } else
        *j += 1;
    return c;
}

/* ---- grapheme clusters (UAX #29) ------------------------------------------------- */

static int gcb_of(UChar32 c) { return REC(c)->gcb_incb_title & 15; }
static int incb_of(UChar32 c) { return REC(c)->gcb_incb_title >> 4 & 3; }

static int char_break(const struct uo_bi *b, int32_t i)
{
    int32_t pi = prev_i(b, i);
    UChar32 pc = cp_at(b, pi), cc = cp_at(b, i);
    int p = gcb_of(pc), c = gcb_of(cc);
    if (p == UO_GCB_CR && c == UO_GCB_LF)
        return NOBRK;                                           /* GB3 */
    if (p == UO_GCB_CR || p == UO_GCB_LF || p == UO_GCB_CONTROL)
        return BRK;                                             /* GB4 */
    if (c == UO_GCB_CR || c == UO_GCB_LF || c == UO_GCB_CONTROL)
        return BRK;                                             /* GB5 */
    if (p == UO_GCB_L && (c == UO_GCB_L || c == UO_GCB_V || c == UO_GCB_LV || c == UO_GCB_LVT))
        return NOBRK;                                           /* GB6 */
    if ((p == UO_GCB_LV || p == UO_GCB_V) && (c == UO_GCB_V || c == UO_GCB_T))
        return NOBRK;                                           /* GB7 */
    if ((p == UO_GCB_LVT || p == UO_GCB_T) && c == UO_GCB_T)
        return NOBRK;                                           /* GB8 */
    if (c == UO_GCB_EXTEND || c == UO_GCB_ZWJ || c == UO_GCB_SPACINGMARK || p == UO_GCB_PREPEND)
        return NOBRK;                                           /* GB9, 9a, 9b */
    if (incb_of(cc) == UO_INCB_CONSONANT) {                     /* GB9c */
        int32_t j = i;
        int linker = 0;
        while (j > 0) {
            int32_t k = prev_i(b, j);
            int v = incb_of(cp_at(b, k));
            if (v == UO_INCB_LINKER)
                linker = 1;
            else if (v != UO_INCB_EXTEND)
                break;
            j = k;
        }
        if (linker && j > 0 && incb_of(cp_at(b, prev_i(b, j))) == UO_INCB_CONSONANT)
            return NOBRK;
    }
    if (p == UO_GCB_ZWJ && REC(cc)->flags & UO_F_EXTPICT) {     /* GB11 */
        int32_t j = pi;
        while (j > 0) {
            int32_t k = prev_i(b, j);
            UChar32 x = cp_at(b, k);
            if (gcb_of(x) == UO_GCB_EXTEND) {
                j = k;
                continue;
            }
            if (REC(x)->flags & UO_F_EXTPICT)
                return NOBRK;
            break;
        }
    }
    if (p == UO_GCB_REGIONAL_INDICATOR && c == UO_GCB_REGIONAL_INDICATOR) {   /* GB12, 13 */
        int n = 0;
        int32_t j = i;
        while (j > 0) {
            int32_t k = prev_i(b, j);
            if (gcb_of(cp_at(b, k)) != UO_GCB_REGIONAL_INDICATOR)
                break;
            n++;
            j = k;
        }
        if (n & 1)
            return NOBRK;
    }
    return BRK;                                                 /* GB999 */
}

/* ---- words (UAX #29, as ICU's rules) --------------------------------------------- */

static int wb_of(UChar32 c) { return REC(c)->wb; }

static inline int is_exfm(int w) { return w == UO_WB_EXTEND || w == UO_WB_FORMAT || w == UO_WB_ZWJ; }
static inline int is_ahl(int w) { return w == UO_WB_ALETTER || w == UO_WB_HEBREW_LETTER; }
static inline int is_midl(int w) { return w == UO_WB_MIDLETTER || w == UO_WB_MIDNUMLET || w == UO_WB_SINGLE_QUOTE; }
static inline int is_midn(int w) { return w == UO_WB_MIDNUM || w == UO_WB_MIDNUMLET || w == UO_WB_SINGLE_QUOTE; }
static inline int is_hangul(UChar32 c) { return c >= 0xAC00 && c <= 0xD7A3; }

/* the base character before i (Extend, Format and ZWJ skipped): its index, or -1 */
static int32_t w_base_before(const struct uo_bi *b, int32_t i)
{
    while (i > 0) {
        int32_t k = prev_i(b, i);
        if (!is_exfm(wb_of(cp_at(b, k))))
            return k;
        i = k;
    }
    return -1;
}

/* the base character after the one at i: its index, or -1 */
static int32_t w_base_after(const struct uo_bi *b, int32_t i)
{
    i = next_i(b, i);
    while (i < b->len) {
        if (!is_exfm(wb_of(cp_at(b, i))))
            return i;
        i = next_i(b, i);
    }
    return -1;
}

static int word_rules(const struct uo_bi *b, int32_t i)
{
    int32_t pi = prev_i(b, i);
    UChar32 pc = cp_at(b, pi), cc = cp_at(b, i);
    int p = wb_of(pc), c = wb_of(cc);
    int32_t bi, ai;
    int B;
    if (p == UO_WB_CR && c == UO_WB_LF)
        return NOBRK;                                           /* WB3 */
    if (p == UO_WB_CR || p == UO_WB_LF || p == UO_WB_NEWLINE)
        return BRK;                                             /* WB3a */
    if (c == UO_WB_CR || c == UO_WB_LF || c == UO_WB_NEWLINE)
        return BRK;                                             /* WB3b */
    if (p == UO_WB_ZWJ && REC(cc)->flags & UO_F_EXTPICT)
        return NOBRK;                                           /* WB3c */
    if (p == UO_WB_WSEGSPACE && c == UO_WB_WSEGSPACE)
        return NOBRK;                                           /* WB3d */
    if (is_exfm(c))
        return NOBRK;                                           /* WB4 */
    bi = w_base_before(b, i);
    if (bi < 0)
        return BRK;                     /* only Extend and Format before: a segment of its own */
    B = wb_of(cp_at(b, bi));
    if (B == UO_WB_CR || B == UO_WB_LF || B == UO_WB_NEWLINE)
        return BRK;
    if (bi == pi) {                     /* ICU's rules for these have no Extend between */
        int fp = REC(pc)->flags, fc = REC(cc)->flags;
        if (is_hangul(pc) && is_hangul(cc))
            return NOBRK;
        if (fp & UO_F_KANAKANJI && fc & UO_F_KANAKANJI)
            return NOBRK;
    }
    if (is_ahl(B) && is_ahl(c))
        return NOBRK;                                           /* WB5 */
    if (is_ahl(B) && is_midl(c)) {                              /* WB6 */
        ai = w_base_after(b, i);
        if (ai >= 0 && is_ahl(wb_of(cp_at(b, ai))))
            return NOBRK;
    }
    if (is_midl(B) && is_ahl(c)) {                              /* WB7 */
        int32_t k = w_base_before(b, bi);
        if (k >= 0 && is_ahl(wb_of(cp_at(b, k))))
            return NOBRK;
    }
    if (B == UO_WB_HEBREW_LETTER && c == UO_WB_SINGLE_QUOTE)
        return NOBRK;                                           /* WB7a */
    if (B == UO_WB_HEBREW_LETTER && c == UO_WB_DOUBLE_QUOTE) {  /* WB7b */
        ai = w_base_after(b, i);
        if (ai >= 0 && wb_of(cp_at(b, ai)) == UO_WB_HEBREW_LETTER)
            return NOBRK;
    }
    if (B == UO_WB_DOUBLE_QUOTE && c == UO_WB_HEBREW_LETTER) {  /* WB7c */
        int32_t k = w_base_before(b, bi);
        if (k >= 0 && wb_of(cp_at(b, k)) == UO_WB_HEBREW_LETTER)
            return NOBRK;
    }
    if ((B == UO_WB_NUMERIC || is_ahl(B)) && c == UO_WB_NUMERIC)
        return NOBRK;                                           /* WB8, 9 */
    if (B == UO_WB_NUMERIC && is_ahl(c))
        return NOBRK;                                           /* WB10 */
    if (is_midn(B) && c == UO_WB_NUMERIC) {                     /* WB11 */
        int32_t k = w_base_before(b, bi);
        if (k >= 0 && wb_of(cp_at(b, k)) == UO_WB_NUMERIC)
            return NOBRK;
    }
    if (B == UO_WB_NUMERIC && is_midn(c)) {                     /* WB12 */
        ai = w_base_after(b, i);
        if (ai >= 0 && wb_of(cp_at(b, ai)) == UO_WB_NUMERIC)
            return NOBRK;
    }
    if (B == UO_WB_KATAKANA && c == UO_WB_KATAKANA)
        return NOBRK;                                           /* WB13 */
    if ((is_ahl(B) || B == UO_WB_NUMERIC || B == UO_WB_KATAKANA || B == UO_WB_EXTENDNUMLET) &&
        c == UO_WB_EXTENDNUMLET)
        return NOBRK;                                           /* WB13a */
    if (B == UO_WB_EXTENDNUMLET && (is_ahl(c) || c == UO_WB_NUMERIC || c == UO_WB_KATAKANA))
        return NOBRK;                                           /* WB13b */
    if (B == UO_WB_REGIONAL_INDICATOR && c == UO_WB_REGIONAL_INDICATOR) {   /* WB15, 16 */
        int n = 0;
        int32_t k = bi;
        while (k >= 0 && wb_of(cp_at(b, k)) == UO_WB_REGIONAL_INDICATOR) {
            n++;
            k = w_base_before(b, k);
        }
        if (n & 1)
            return NOBRK;
    }
    return BRK;                                                 /* WB999 */
}

/* As for lines: the decisions that depend on the two classes alone, once */
#define WB_SLOW 3
#define WM(x) (1UL << (x))
#define WB_SLOW_B (WM(UO_WB_EXTEND) | WM(UO_WB_FORMAT) | WM(UO_WB_ZWJ) | WM(UO_WB_REGIONAL_INDICATOR) | \
                   WM(UO_WB_MIDLETTER) | WM(UO_WB_MIDNUMLET) | WM(UO_WB_SINGLE_QUOTE) | \
                   WM(UO_WB_DOUBLE_QUOTE) | WM(UO_WB_MIDNUM))
#define WB_SLOW_A (WM(UO_WB_MIDLETTER) | WM(UO_WB_MIDNUMLET) | WM(UO_WB_SINGLE_QUOTE) | \
                   WM(UO_WB_DOUBLE_QUOTE) | WM(UO_WB_MIDNUM))
#define WB_CLASSES (UO_WB_WSEGSPACE + 1)

static uint8_t wb_pairs[WB_CLASSES][WB_CLASSES];
static volatile uint8_t wb_pairs_ready;

static void wb_make_pairs(void)
{
    struct uo_bi t;
    UChar s[4];
    int x, y;
    memset(&t, 0, sizeof t);
    t.type = UBRK_WORD;
    t.text = s;
    for (x = 0; x < WB_CLASSES; x++)
        for (y = 0; y < WB_CLASSES; y++) {
            int n = 0, k;
            int32_t mid = 0;
            for (k = 0; k < 2; k++) {
                UChar32 c = uo_wb_rep[k ? y : x];
                if (k)
                    mid = n;
                if (c >= 0x10000) {
                    s[n++] = (UChar)(0xD7C0 + (c >> 10));
                    s[n++] = (UChar)(0xDC00 | (c & 0x3FF));
                } else
                    s[n++] = (UChar)c;
            }
            t.len = n;
            wb_pairs[x][y] = (WM(x) & WB_SLOW_B) || (WM(y) & WB_SLOW_A) ? WB_SLOW : (uint8_t)word_rules(&t, mid);
        }
    wb_pairs_ready = 1;
}

static inline int word_pair(const struct uo_bi *b, const struct uo_brk *pr, const struct uo_brk *cr, int32_t i)
{
    int v;
    if ((pr->flags | cr->flags) & UO_F_KANAKANJI)
        return word_rules(b, i);
    if (!wb_pairs_ready)
        wb_make_pairs();
    v = wb_pairs[pr->wb][cr->wb];
    if (v == WB_SLOW)
        return word_rules(b, i);
    if (pr->wb == UO_WB_OTHER && cr->wb == UO_WB_OTHER)
        return word_rules(b, i);        /* Hangul syllables are Other here, and join */
    return v;
}

static int word_break(const struct uo_bi *b, int32_t i)
{
    return word_pair(b, REC(cp_at(b, prev_i(b, i))), REC(cp_at(b, i)), i);
}

/* ICU's rule status of the word that ends at e and starts at s */
static int32_t word_status(const struct uo_bi *b, int32_t s, int32_t e)
{
    int32_t li = -1, k = e, pi;
    int trailing = 0, L, st = UBRK_WORD_NONE, f;
    UChar32 lc;
    while (k > s) {
        int32_t j = prev_i(b, k);
        UChar32 c = cp_at(b, j);
        if (!is_exfm(wb_of(c))) {
            li = j;
            break;
        }
        /* an Ideographic Extend (U+16FE4) starts ICU's "$Ideographic
           $ExFm*" rule by chaining: the rest of the word is that rule's */
        if (REC(c)->flags & UO_F_IDEO)
            return UBRK_WORD_IDEO;
        trailing = 1;
        k = j;
    }
    if (li < 0)
        return UBRK_WORD_NONE;
    lc = cp_at(b, li);
    L = wb_of(lc);
    f = REC(lc)->flags;
    if (L == UO_WB_NUMERIC)
        st = UBRK_WORD_NUMBER;
    if (is_ahl(L))
        st = UBRK_WORD_LETTER;
    if (is_hangul(lc) && !trailing)
        st = UBRK_WORD_LETTER;
    if (L == UO_WB_KATAKANA || f & (UO_F_HIRAGANA | UO_F_IDEO))
        st = UBRK_WORD_IDEO;
    /* rules of two that end at the last character (none takes Extend after it) */
    pi = w_base_before(b, li);
    if (pi >= s && !trailing) {
        UChar32 pc = cp_at(b, pi);
        int P = wb_of(pc), v = UBRK_WORD_NONE;
        if (P == UO_WB_HEBREW_LETTER && L == UO_WB_SINGLE_QUOTE)
            v = UBRK_WORD_LETTER;
        if (L == UO_WB_EXTENDNUMLET) {
            if (is_ahl(P) || P == UO_WB_EXTENDNUMLET)
                v = UBRK_WORD_LETTER;
            else if (P == UO_WB_NUMERIC)
                v = UBRK_WORD_NUMBER;
            else if (P == UO_WB_KATAKANA)
                v = UBRK_WORD_IDEO;
        }
        if (pi == prev_i(b, li) && REC(pc)->flags & f & UO_F_KANAKANJI)
            v = UBRK_WORD_IDEO;
        if (v > st)
            st = v;
    }
    return st;
}

/* ---- sentences (UAX #29) ---------------------------------------------------------- */

static int sb_of(UChar32 c) { return REC(c)->sb; }
static inline int is_sep(int s) { return s == UO_SB_SEP || s == UO_SB_CR || s == UO_SB_LF; }
static inline int is_saterm(int s) { return s == UO_SB_STERM || s == UO_SB_ATERM; }

static int32_t s_base_before(const struct uo_bi *b, int32_t i)
{
    while (i > 0) {
        int32_t k = prev_i(b, i);
        int s = sb_of(cp_at(b, k));
        if (s != UO_SB_EXTEND && s != UO_SB_FORMAT)
            return k;
        i = k;
    }
    return -1;
}

/* from base index k back over Sp* (when sp) and Close*: the SATerm's class, or -1 */
static int s_term_before(const struct uo_bi *b, int32_t k, int sp, int32_t *at)
{
    if (sp)
        while (k >= 0 && sb_of(cp_at(b, k)) == UO_SB_SP)
            k = s_base_before(b, k);
    while (k >= 0 && sb_of(cp_at(b, k)) == UO_SB_CLOSE)
        k = s_base_before(b, k);
    if (k >= 0 && is_saterm(sb_of(cp_at(b, k)))) {
        if (at)
            *at = k;
        return sb_of(cp_at(b, k));
    }
    return -1;
}

/* only a terminator, its closing marks and spaces, a separator, or the
 * marks after any of them, can start a rule that breaks or joins */
#define SB_TERMINAL ((1U << UO_SB_ATERM) | (1U << UO_SB_STERM) | (1U << UO_SB_CLOSE) | (1U << UO_SB_SP) | \
                     (1U << UO_SB_SEP) | (1U << UO_SB_CR) | (1U << UO_SB_LF) | (1U << UO_SB_EXTEND) | \
                     (1U << UO_SB_FORMAT))

static int sent_rules(const struct uo_bi *b, int32_t i);
/* the classes that start a rule, and those a rule carries on through */
#define SB_STARTS ((1U << UO_SB_ATERM) | (1U << UO_SB_STERM) | (1U << UO_SB_SEP) | (1U << UO_SB_CR) | \
                   (1U << UO_SB_LF))
#define SB_CHAIN (SB_STARTS | (1U << UO_SB_CLOSE) | (1U << UO_SB_SP) | (1U << UO_SB_EXTEND) | \
                  (1U << UO_SB_FORMAT))

static int sent_break(const struct uo_bi *b, int32_t i)
{
    if (!((1U << sb_of(cp_at(b, prev_i(b, i)))) & SB_TERMINAL))
        return NOBRK;
    return sent_rules(b, i);
}

static int sent_rules(const struct uo_bi *b, int32_t i)
{
    int32_t pi = prev_i(b, i), bi, k;
    int p = sb_of(cp_at(b, pi)), c = sb_of(cp_at(b, i)), B, t;
    if (p == UO_SB_CR && c == UO_SB_LF)
        return NOBRK;                                           /* SB3 */
    if (is_sep(p))
        return BRK;                                             /* SB4 */
    if (c == UO_SB_EXTEND || c == UO_SB_FORMAT)
        return NOBRK;                                           /* SB5 */
    bi = s_base_before(b, i);
    if (bi < 0)
        return NOBRK;
    B = sb_of(cp_at(b, bi));
    if (B == UO_SB_ATERM && c == UO_SB_NUMERIC)
        return NOBRK;                                           /* SB6 */
    if (B == UO_SB_ATERM && c == UO_SB_UPPER) {                 /* SB7 */
        k = s_base_before(b, bi);
        if (k >= 0 && (sb_of(cp_at(b, k)) == UO_SB_UPPER || sb_of(cp_at(b, k)) == UO_SB_LOWER))
            return NOBRK;
    }
    t = s_term_before(b, bi, 1, NULL);
    if (t == UO_SB_ATERM) {                                     /* SB8 */
        int32_t j = i;
        while (j < b->len) {
            int s = sb_of(cp_at(b, j));
            if (s == UO_SB_LOWER)
                return NOBRK;
            if (s == UO_SB_OLETTER || s == UO_SB_UPPER || is_sep(s) || is_saterm(s))
                break;
            j = next_i(b, j);
        }
    }
    if (t >= 0 && (c == UO_SB_SCONTINUE || is_saterm(c)))
        return NOBRK;                                           /* SB8a */
    if (s_term_before(b, bi, 0, NULL) >= 0 && (c == UO_SB_CLOSE || c == UO_SB_SP || is_sep(c)))
        return NOBRK;                                           /* SB9 */
    if (t >= 0 && (c == UO_SB_SP || is_sep(c)))
        return NOBRK;                                           /* SB10 */
    if (t >= 0)
        return BRK;                                             /* SB11 */
    return NOBRK;                                               /* SB998 */
}

/* UBRK_SENTENCE_TERM when the sentence ending at e ends with a terminator
 * (and its closing marks, spaces and one separator), else UBRK_SENTENCE_SEP */
static int32_t sent_status(const struct uo_bi *b, int32_t s, int32_t e)
{
    int32_t k = e;
    if (k > s) {
        int32_t j = prev_i(b, k);
        int v = sb_of(cp_at(b, j));
        if (v == UO_SB_LF && j > s && sb_of(cp_at(b, j - 1)) == UO_SB_CR)
            k = j - 1;
        else if (is_sep(v))
            k = j;
    }
    while (k > s) {
        int32_t j = prev_i(b, k);
        int v = sb_of(cp_at(b, j));
        if (is_saterm(v))
            return UBRK_SENTENCE_TERM;
        if (v != UO_SB_EXTEND && v != UO_SB_FORMAT && v != UO_SB_SP && v != UO_SB_CLOSE)
            break;
        k = j;
    }
    return UBRK_SENTENCE_SEP;
}

/* ---- lines (UAX #14) ------------------------------------------------------------- */

static inline int lb_of(const struct uo_bi *b, UChar32 c)
{
    int x = REC(c)->lb;
    if (x == UO_LB_CJ)
        return b->lbv ? UO_LB_ID : UO_LB_NS;
    if (b->lbv == 2 && x == UO_LB_NS &&
        (c == 0x3005 || c == 0x303B || c == 0x309D || c == 0x309E || c == 0x30FD || c == 0x30FE))
        return LB_NSX;
    return x;
}

#define M(x) (1ULL << (x))
#define IN(x, set) ((M(x) & (set)) != 0)
#define QU_ANY (M(UO_LB_QU) | M(UO_LB_QU_PI) | M(UO_LB_QU_PF))
#define HARDS (M(UO_LB_BK) | M(UO_LB_CR) | M(UO_LB_LF) | M(UO_LB_NL))
#define NOABSORB (HARDS | M(UO_LB_SP) | M(UO_LB_ZW))
#define AHL (M(UO_LB_AL) | M(UO_LB_HL))
#define CMS (M(UO_LB_CM) | M(UO_LB_ZWJ))

/* A unit is a character with the combining marks it carries (LB9); a mark
 * that follows a space, a break or the start is a unit of class AL (LB10). */
struct unit { int cls; int32_t start; UChar32 base; };

/* the unit that ends at i (its last character is the one before i); 0 at the start */
static int unit_before(const struct uo_bi *b, int32_t i, struct unit *u)
{
    int32_t j = i;
    int cls = 0;
    if (i <= 0)
        return 0;
    while (j > 0) {
        int32_t k = prev_i(b, j);
        cls = lb_of(b, cp_at(b, k));
        if (!IN(cls, CMS))
            break;
        j = k;
    }
    if (j == i) {                       /* no marks: the character before i */
        u->start = prev_i(b, i);
        u->base = cp_at(b, u->start);
        u->cls = cls;
        return 1;
    }
    if (j > 0) {
        int32_t k = prev_i(b, j);
        if (!IN(cls, NOABSORB)) {       /* cls: the class of the character before the marks */
            u->start = k;
            u->base = cp_at(b, k);
            u->cls = cls;
            return 1;
        }
    }
    u->start = j;
    u->base = cp_at(b, j);
    u->cls = UO_LB_AL;
    return 1;
}

/* the class of the unit that starts at i */
static int unit_cls_at(const struct uo_bi *b, int32_t i, UChar32 *base)
{
    UChar32 c = cp_at(b, i);
    int x = lb_of(b, c);
    if (base)
        *base = c;
    return IN(x, CMS) ? UO_LB_AL : x;
}

/* the start of the unit after the one that starts at i, or len */
static int32_t unit_next(const struct uo_bi *b, int32_t i)
{
    int x = lb_of(b, cp_at(b, i));
    i = next_i(b, i);
    if (IN(x, NOABSORB))
        return i;
    while (i < b->len && IN(lb_of(b, cp_at(b, i)), CMS))
        i = next_i(b, i);
    return i;
}

static inline int ea(UChar32 c) { return REC(c)->flags & UO_F_EA; }
static inline int is_dotted(UChar32 c) { return c == 0x25CC; }
static int line_break(const struct uo_bi *b, int32_t i);

static int line_rules(const struct uo_bi *b, int32_t i);

/* Most decisions depend on the two classes alone. lb_pairs holds them per
 * variant, worked out once by the rules themselves on two characters of
 * those classes, and LB_SLOW where the rules look further back or ahead, or
 * at a character's own East Asian width: then the rules run in full. */
#define LB_SLOW 3
#define LB_CLASSES (UO_LB_COUNT + 1)
#define LB_SLOW_B (M(UO_LB_SP) | M(UO_LB_CM) | QU_ANY | M(UO_LB_HY) | M(UO_LB_HH) | M(UO_LB_AK) | \
                   M(UO_LB_AS) | M(UO_LB_AP) | M(UO_LB_VI) | M(UO_LB_VF) | M(UO_LB_RI))
#define LB_SLOW_A (QU_ANY | M(UO_LB_OP) | M(UO_LB_AK) | M(UO_LB_AS) | M(UO_LB_VF) | M(UO_LB_VI) | M(UO_LB_EM))

static uint8_t lb_pairs[3][LB_CLASSES][LB_CLASSES];
static volatile uint8_t lb_pairs_ready[3];

static int lb_slow_pair(int x, int y)
{
    if (IN(x, LB_SLOW_B) || IN(y, LB_SLOW_A))
        return 1;
    if (IN(x, M(UO_LB_IS) | M(UO_LB_SY) | M(UO_LB_CL) | M(UO_LB_CP)) && IN(y, M(UO_LB_PO) | M(UO_LB_PR)))
        return 1;                       /* LB25: NU (SY|IS)* (CL|CP)? x (PO|PR) */
    if (x == UO_LB_SY && y == UO_LB_NU)
        return 1;                       /* LB25: NU (SY|IS)* x NU */
    if (x == UO_LB_CP && IN(y, AHL | M(UO_LB_NU)))
        return 1;                       /* LB30: CP's own width */
    return 0;
}

static void lb_make_pairs(int v)
{
    struct uo_bi t;
    UChar s[4];
    int x, y;
    memset(&t, 0, sizeof t);
    t.type = UBRK_LINE;
    t.lbv = v;
    t.text = s;
    for (x = 0; x < LB_CLASSES; x++)
        for (y = 0; y < LB_CLASSES; y++) {
            int n = 0, k;
            int32_t mid = 0;
            for (k = 0; k < 2; k++) {
                UChar32 c = uo_lb_rep[k ? y : x];
                if (k)
                    mid = n;
                if (c >= 0x10000) {
                    s[n++] = (UChar)(0xD7C0 + (c >> 10));
                    s[n++] = (UChar)(0xDC00 | (c & 0x3FF));
                } else
                    s[n++] = (UChar)c;
            }
            t.len = n;
            lb_pairs[v][x][y] = lb_slow_pair(x, y) ? LB_SLOW : (uint8_t)line_rules(&t, mid);
        }
    lb_pairs_ready[v] = 1;
}

/* after a space: LB8, 14, 15a, 18 look back over the spaces */
static int line_after_space(const struct uo_bi *b, int32_t i, int c)
{
    struct unit u;
    int32_t k;
    if (IN(c, HARDS | M(UO_LB_SP) | M(UO_LB_ZW)))
        return NOBRK;                   /* LB6, 7 */
    if (IN(c, M(UO_LB_QU_PF) | M(UO_LB_IS) | M(UO_LB_NS) | M(UO_LB_B2)))
        return line_rules(b, i);        /* LB15b, 15c, 16, 17 */
    k = prev_i(b, i);
    while (k > 0) {
        int32_t j = prev_i(b, k);
        if (lb_of(b, cp_at(b, j)) != UO_LB_SP)
            break;
        k = j;
    }
    if (!unit_before(b, k, &u))
        u.cls = UO_LB_SP;               /* only spaces back to the start */
    if (u.cls == UO_LB_ZW)
        return BRK;                     /* LB8 */
    if (IN(c, M(UO_LB_CL) | M(UO_LB_CP) | M(UO_LB_EX) | M(UO_LB_SY) | M(UO_LB_WJ)))
        return NOBRK;                   /* LB11, 13 */
    if (u.cls == UO_LB_SP)
        return BRK;
    if (u.cls == UO_LB_OP)
        return NOBRK;                   /* LB14 */
    if (u.cls == UO_LB_QU_PI)
        return line_rules(b, i);        /* LB15a */
    return BRK;                         /* LB18 */
}

static inline int line_pair(const struct uo_bi *b, UChar32 pc, int p, UChar32 cc, int c, int32_t i)
{
    int v;
    if (p == UO_LB_SP)
        return line_after_space(b, i, c);
    if (!lb_pairs_ready[b->lbv])
        lb_make_pairs(b->lbv);
    v = lb_pairs[b->lbv][p][c];
    if (v == LB_SLOW || pc == 0x25CC || cc == 0x25CC)
        return line_rules(b, i);
    return v;
}

static int line_break(const struct uo_bi *b, int32_t i)
{
    UChar32 pc = cp_at(b, prev_i(b, i)), cc = cp_at(b, i);
    return line_pair(b, pc, lb_of(b, pc), cc, lb_of(b, cc), i);
}

static int line_rules(const struct uo_bi *b, int32_t i)
{
    int32_t pi = prev_i(b, i);
    UChar32 pc = cp_at(b, pi), ac;
    int p = lb_of(b, pc), c = lb_of(b, cp_at(b, i)), A, B;
    struct unit ub, uu;
    int32_t k;
    if (p == UO_LB_BK)
        return HARD;                                            /* LB4 */
    if (p == UO_LB_CR)
        return c == UO_LB_LF ? NOBRK : HARD;                    /* LB5 */
    if (p == UO_LB_LF || p == UO_LB_NL)
        return HARD;
    if (IN(c, HARDS) || c == UO_LB_SP || c == UO_LB_ZW)
        return NOBRK;                                           /* LB6, 7 */
    k = i;                                                      /* LB8 */
    while (k > 0) {
        int32_t j = prev_i(b, k);
        int x = lb_of(b, cp_at(b, j));
        if (x == UO_LB_ZW)
            return BRK;
        if (x != UO_LB_SP)
            break;
        k = j;
    }
    if (p == UO_LB_ZWJ)
        return NOBRK;                                           /* LB8a */
    if (IN(c, CMS) && !IN(p, NOABSORB))
        return NOBRK;                                           /* LB9 */
    unit_before(b, i, &ub);
    B = ub.cls;
    A = unit_cls_at(b, i, &ac);                                 /* LB10 inside */
    if (A == UO_LB_WJ || B == UO_LB_WJ)
        return NOBRK;                                           /* LB11 */
    if (B == UO_LB_GL)
        return NOBRK;                                           /* LB12 */
    if (A == UO_LB_GL && !IN(B, M(UO_LB_SP) | M(UO_LB_BA) | M(UO_LB_HY) | M(UO_LB_HH)))
        return NOBRK;                                           /* LB12a */
    if (IN(A, M(UO_LB_CL) | M(UO_LB_CP) | M(UO_LB_EX) | M(UO_LB_SY)))
        return NOBRK;                                           /* LB13 */
    /* the unit before B's spaces: LB14, 15a, 16, 17 */
    uu = ub;
    while (uu.cls == UO_LB_SP && unit_before(b, uu.start, &uu))
        ;
    if (uu.cls == UO_LB_SP)
        uu.cls = -1;                    /* only spaces back to the start */
    if (uu.cls == UO_LB_OP)
        return NOBRK;                                           /* LB14 */
    if (uu.cls == UO_LB_QU_PI) {                                /* LB15a */
        struct unit r;
        if (!unit_before(b, uu.start, &r) ||
            IN(r.cls, HARDS | M(UO_LB_OP) | QU_ANY | M(UO_LB_GL) | M(UO_LB_SP) | M(UO_LB_ZW)))
            return NOBRK;
    }
    if (A == UO_LB_QU_PF) {                                     /* LB15b */
        int32_t n = unit_next(b, i);
        if (n >= b->len)
            return NOBRK;
        if (IN(unit_cls_at(b, n, NULL), M(UO_LB_SP) | M(UO_LB_GL) | M(UO_LB_WJ) | M(UO_LB_CL) | QU_ANY |
               M(UO_LB_CP) | M(UO_LB_EX) | M(UO_LB_IS) | M(UO_LB_SY) | HARDS | M(UO_LB_ZW)))
            return NOBRK;
    }
    if (A == UO_LB_IS) {
        if (B == UO_LB_SP) {                                    /* LB15c */
            int32_t n = unit_next(b, i);
            if (n < b->len && unit_cls_at(b, n, NULL) == UO_LB_NU)
                return BRK;
        }
        return NOBRK;                                           /* LB15d */
    }
    if (A == UO_LB_NS && (uu.cls == UO_LB_CL || uu.cls == UO_LB_CP))
        return NOBRK;                                           /* LB16 */
    if (A == UO_LB_B2 && uu.cls == UO_LB_B2)
        return NOBRK;                                           /* LB17 */
    if (B == UO_LB_SP)
        return BRK;                                             /* LB18 */
    if (A == UO_LB_QU || A == UO_LB_QU_PF || B == UO_LB_QU || B == UO_LB_QU_PI)
        return NOBRK;                                           /* LB19 */
    if (IN(A, QU_ANY)) {                                        /* LB19a */
        int32_t n;
        if (!ea(ub.base))
            return NOBRK;
        n = unit_next(b, i);
        if (n >= b->len || !ea(cp_at(b, n)))
            return NOBRK;
    }
    if (IN(B, QU_ANY)) {
        struct unit r;
        if (!ea(ac))
            return NOBRK;
        if (!unit_before(b, ub.start, &r) || !ea(r.base))
            return NOBRK;
    }
    if (A == UO_LB_CB || B == UO_LB_CB)
        return BRK;                                             /* LB20 */
    if ((B == UO_LB_HY || B == UO_LB_HH) && IN(A, AHL)) {       /* LB20a */
        struct unit r;
        if (!unit_before(b, ub.start, &r) ||
            IN(r.cls, HARDS | M(UO_LB_SP) | M(UO_LB_ZW) | M(UO_LB_CB) | M(UO_LB_GL)))
            return NOBRK;
    }
    if (IN(A, M(UO_LB_BA) | M(UO_LB_HH) | M(UO_LB_HY) | M(UO_LB_NS)) || B == UO_LB_BB)
        return NOBRK;                                           /* LB21 */
    if ((B == UO_LB_HY || B == UO_LB_HH) && A != UO_LB_HL) {    /* LB21a */
        struct unit r;
        if (unit_before(b, ub.start, &r) && r.cls == UO_LB_HL)
            return NOBRK;
    }
    if (B == UO_LB_SY && A == UO_LB_HL)
        return NOBRK;                                           /* LB21b */
    if (A == UO_LB_IN && (B != UO_LB_IN || b->lbv != 2))
        return NOBRK;                                           /* LB22 (loose: IN ÷ IN) */
    if ((IN(B, AHL) && A == UO_LB_NU) || (B == UO_LB_NU && IN(A, AHL)))
        return NOBRK;                                           /* LB23 */
    if ((B == UO_LB_PR && IN(A, M(UO_LB_ID) | M(UO_LB_EB) | M(UO_LB_EM))) ||
        (IN(B, M(UO_LB_ID) | M(UO_LB_EB) | M(UO_LB_EM)) && A == UO_LB_PO))
        return NOBRK;                                           /* LB23a */
    if ((IN(B, M(UO_LB_PR) | M(UO_LB_PO)) && IN(A, AHL)) ||
        (IN(B, AHL) && IN(A, M(UO_LB_PR) | M(UO_LB_PO))))
        return NOBRK;                                           /* LB24 */
    /* LB25 */
    if (IN(B, M(UO_LB_PR) | M(UO_LB_PO) | M(UO_LB_OP) | M(UO_LB_HY))) {
        int32_t n = i;
        int x = A;
        if (IN(B, M(UO_LB_PR) | M(UO_LB_PO)) && (x == UO_LB_OP || x == UO_LB_HY)) {
            n = unit_next(b, n);
            x = n < b->len ? unit_cls_at(b, n, NULL) : -1;
        }
        if (x == UO_LB_IS) {
            n = unit_next(b, n);
            x = n < b->len ? unit_cls_at(b, n, NULL) : -1;
        }
        if (x == UO_LB_NU)
            return NOBRK;
    }
    if (B == UO_LB_IS && A == UO_LB_NU)
        return NOBRK;
    if (A == UO_LB_NU || A == UO_LB_PO || A == UO_LB_PR) {
        struct unit r = ub;
        int ok = 1;
        if (A != UO_LB_NU && (r.cls == UO_LB_CL || r.cls == UO_LB_CP))
            ok = unit_before(b, r.start, &r);
        while (ok && (r.cls == UO_LB_SY || r.cls == UO_LB_IS))
            ok = unit_before(b, r.start, &r);
        if (ok && r.cls == UO_LB_NU)
            return NOBRK;
    }
    /* LB26, 27 */
    if (B == UO_LB_JL && IN(A, M(UO_LB_JL) | M(UO_LB_JV) | M(UO_LB_H2) | M(UO_LB_H3)))
        return NOBRK;
    if (IN(B, M(UO_LB_JV) | M(UO_LB_H2)) && IN(A, M(UO_LB_JV) | M(UO_LB_JT)))
        return NOBRK;
    if (IN(B, M(UO_LB_JT) | M(UO_LB_H3)) && A == UO_LB_JT)
        return NOBRK;
    if ((IN(B, M(UO_LB_JL) | M(UO_LB_JV) | M(UO_LB_JT) | M(UO_LB_H2) | M(UO_LB_H3)) && A == UO_LB_PO) ||
        (B == UO_LB_PR && IN(A, M(UO_LB_JL) | M(UO_LB_JV) | M(UO_LB_JT) | M(UO_LB_H2) | M(UO_LB_H3))))
        return NOBRK;
    if (IN(B, AHL) && IN(A, AHL))
        return NOBRK;                                           /* LB28 */
    {                                                           /* LB28a */
        int bAk = B == UO_LB_AK || B == UO_LB_AS || is_dotted(ub.base);
        int aAk = A == UO_LB_AK || A == UO_LB_AS || is_dotted(ac);
        if (B == UO_LB_AP && aAk)
            return NOBRK;
        if (bAk && (A == UO_LB_VF || A == UO_LB_VI))
            return NOBRK;
        if (B == UO_LB_VI && (A == UO_LB_AK || is_dotted(ac))) {
            struct unit r;
            if (unit_before(b, ub.start, &r) &&
                (r.cls == UO_LB_AK || r.cls == UO_LB_AS || is_dotted(r.base)))
                return NOBRK;
        }
        if (bAk && aAk) {
            int32_t n = unit_next(b, i);
            if (n < b->len && unit_cls_at(b, n, NULL) == UO_LB_VF)
                return NOBRK;
        }
    }
    if (B == UO_LB_IS && IN(A, AHL))
        return NOBRK;                                           /* LB29 */
    if (IN(B, AHL | M(UO_LB_NU)) && A == UO_LB_OP && !ea(ac))
        return NOBRK;                                           /* LB30 */
    if (B == UO_LB_CP && !ea(ub.base) && IN(A, AHL | M(UO_LB_NU)))
        return NOBRK;
    if (B == UO_LB_RI && A == UO_LB_RI) {                       /* LB30a */
        struct unit r = ub;
        int n = 0, ok = 1;
        while (ok && r.cls == UO_LB_RI) {
            n++;
            ok = unit_before(b, r.start, &r);
        }
        if (n & 1)
            return NOBRK;
    }
    if (A == UO_LB_EM && (B == UO_LB_EB || REC(ub.base)->flags & UO_F_EXTPICT_CN))
        return NOBRK;                                           /* LB30b */
    return BRK;                                                 /* LB31 */
}

/* ---- title-case segments ----------------------------------------------------------- */

static int title_of(UChar32 c) { return REC(c)->gcb_incb_title >> 6; }

/* from a boundary s: the next one (the longest of ICU's two title rules) */
static int32_t title_next(const struct uo_bi *b, int32_t s)
{
    int32_t i = s;
    if (title_of(cp_at(b, i)) != UO_TITLE_CASED) {
        while (i < b->len && title_of(cp_at(b, i)) != UO_TITLE_CASED)
            i = next_i(b, i);
        return i;
    }
    i = next_i(b, i);
    while (i < b->len && title_of(cp_at(b, i)) != UO_TITLE_NOTCASED)
        i = next_i(b, i);
    while (i < b->len && title_of(cp_at(b, i)) != UO_TITLE_CASED)
        i = next_i(b, i);
    return i;
}

/* ---- boundaries --------------------------------------------------------------------- */

static int is_break(const struct uo_bi *b, int32_t i)
{
    if (i <= 0 || i >= b->len)
        return 1;
    switch (b->type) {
    case UBRK_CHARACTER: return char_break(b, i) != NOBRK;
    case UBRK_WORD: return word_break(b, i) != NOBRK;
    case UBRK_LINE: return line_break(b, i) != NOBRK;
    case UBRK_SENTENCE: return sent_break(b, i) != NOBRK;
    default: {
        int32_t s = 0;
        while (s < i)
            s = title_next(b, s);
        return s == i;
    }
    }
}

/* the first boundary after i (i < len) */
static int32_t after(const struct uo_bi *b, int32_t i)
{
    if (b->type == UBRK_TITLE) {
        int32_t s = 0;
        while (s <= i)
            s = title_next(b, s);
        return s;
    }
    {
        int32_t j = i, k;
        UChar32 pc = cp_step(b, &j), cc;
        switch (b->type) {
        case UBRK_LINE: {
            int p = lb_of(b, pc), c;
            for (; j < b->len; j = k, pc = cc, p = c) {
                k = j;
                cc = cp_step(b, &k);
                c = lb_of(b, cc);
                if (line_pair(b, pc, p, cc, c, j) != NOBRK)
                    break;
            }
            return j;
        }
        case UBRK_WORD: {
            const struct uo_brk *pr = REC(pc), *cr;
            for (; j < b->len; j = k, pr = cr) {
                k = j;
                cr = REC(cp_step(b, &k));
                if (word_pair(b, pr, cr, j) != NOBRK)
                    break;
            }
            return j;
        }
        case UBRK_SENTENCE: {
            /* t: a terminator or separator since the last character that
             * cannot follow one in a rule (no rule breaks without one) */
            int p = sb_of(pc), c, t = !((1U << p) & ~SB_CHAIN);
            for (; j < b->len; j = k, p = c) {
                if (!t) {
                    /* no rule open: pass over ASCII that cannot start one */
                    const UChar *x = b->text;
                    while (j < b->len && x[j] < 0x80 && !((1U << uo_brks[uo_brk_latin1[x[j]]].sb) & SB_STARTS))
                        j++;
                    if (j >= b->len)
                        break;
                } else if (((1U << p) & SB_TERMINAL) && sent_rules(b, j) != NOBRK)
                    break;
                k = j;
                c = sb_of(cp_step(b, &k));
                if ((1U << c) & SB_STARTS)
                    t = 1;
                else if (!((1U << c) & SB_CHAIN))
                    t = 0;
            }
            return j;
        }
        default:
            while (j < b->len && !is_break(b, j))
                j = next_i(b, j);
            return j;
        }
    }
}

/* the last boundary before i (i > 0) */
static int32_t before(const struct uo_bi *b, int32_t i)
{
    if (b->type == UBRK_TITLE) {
        int32_t s = 0, last = 0;
        while (s < i) {
            last = s;
            s = title_next(b, s);
        }
        return last;
    }
    do
        i = prev_i(b, i);
    while (i > 0 && !is_break(b, i));
    return i;
}

static int32_t native(const struct uo_bi *b, int32_t i)
{
    return b->map ? b->map[i] : i;
}

static int32_t from_native(const struct uo_bi *b, int32_t n)
{
    int32_t lo = 0, hi;
    if (!b->map)
        return n;
    if (n <= 0)
        return n < 0 ? n : 0;
    if (n >= b->nativeLen)
        return n > b->nativeLen ? b->len + 1 : b->len;
    hi = b->len;
    while (lo < hi) {                   /* the last unit whose native index <= n */
        int32_t m = (lo + hi + 1) / 2;
        if (b->map[m] <= n)
            lo = m;
        else
            hi = m - 1;
    }
    return lo;
}

static struct uo_bi *bi_of(UBreakIterator *bi)
{
    struct uo_bi *b = (struct uo_bi *)bi;
    return b && b->magic == UO_BI_MAGIC ? b : NULL;
}

/* ---- the calls ---------------------------------------------------------------------- */

static void drop_copy(struct uo_bi *b)
{
    free(b->own);
    free(b->map);
    b->own = NULL;
    b->map = NULL;
}

static void set_text(struct uo_bi *b, const UChar *text, int32_t len)
{
    static const UChar empty[1];
    drop_copy(b);
    if (!text) {
        text = empty;
        len = 0;
    }
    if (len < 0)
        for (len = 0; text[len]; len++)
            ;
    b->text = text;
    b->len = len;
    b->nativeLen = len;
    b->pos = 0;
}

UBreakIterator *uo_ubrk_open(UBreakIteratorType type, const char *locale, const UChar *text,
                             int32_t textLength, UErrorCode *status)
{
    struct uo_bi *b;
    if (!status || U_FAILURE(*status))
        return NULL;
    if ((int)type < UBRK_CHARACTER || (int)type > UBRK_TITLE) {
        *status = U_ILLEGAL_ARGUMENT_ERROR;
        return NULL;
    }
    b = calloc(1, sizeof *b);
    if (!b) {
        *status = U_MEMORY_ALLOCATION_ERROR;
        return NULL;
    }
    b->magic = UO_BI_MAGIC;
    b->type = type;
    if (locale && type == UBRK_LINE) {
        const char *k = strstr(locale, "lb=");
        if (k && (k == locale || k[-1] == '@' || k[-1] == ';')) {
            if (!strncmp(k + 3, "loose", 5))
                b->lbv = 2;
            else if (!strncmp(k + 3, "normal", 6))
                b->lbv = 1;
        }
    }
    set_text(b, text, textLength);
    return (UBreakIterator *)b;
}

UBreakIterator *uo_ubrk_clone(const UBreakIterator *bi, UErrorCode *status)
{
    struct uo_bi *b = bi_of((UBreakIterator *)bi), *n;
    if (!status || U_FAILURE(*status))
        return NULL;
    if (!b) {
        *status = U_ILLEGAL_ARGUMENT_ERROR;
        return NULL;
    }
    n = malloc(sizeof *n);
    if (!n) {
        *status = U_MEMORY_ALLOCATION_ERROR;
        return NULL;
    }
    *n = *b;
    if (b->own) {
        n->own = malloc(sizeof(UChar) * (b->len + 1));
        n->map = b->map ? malloc(sizeof(int32_t) * (b->len + 1)) : NULL;
        if (!n->own || (b->map && !n->map)) {
            free(n->own);
            free(n->map);
            free(n);
            *status = U_MEMORY_ALLOCATION_ERROR;
            return NULL;
        }
        memcpy(n->own, b->own, sizeof(UChar) * (b->len + 1));
        if (b->map)
            memcpy(n->map, b->map, sizeof(int32_t) * (b->len + 1));
        n->text = n->own;
    }
    return (UBreakIterator *)n;
}

void uo_ubrk_close(UBreakIterator *bi)
{
    struct uo_bi *b = bi_of(bi);
    if (!b)
        return;
    drop_copy(b);
    b->magic = 0;
    free(b);
}

void uo_ubrk_setText(UBreakIterator *bi, const UChar *text, int32_t textLength, UErrorCode *status)
{
    struct uo_bi *b = bi_of(bi);
    if (!status || U_FAILURE(*status))
        return;
    if (!b) {
        *status = U_ILLEGAL_ARGUMENT_ERROR;
        return;
    }
    set_text(b, text, textLength);
}

/* A UText is read once through its provider, chunk by chunk, into UTF-16;
 * where the provider's native indexes are not UTF-16 offsets, a map keeps
 * them, so the calls take and give native indexes as ICU's do. */
void uo_ubrk_setUText(UBreakIterator *bi, UText *ut, UErrorCode *status)
{
    struct uo_bi *b = bi_of(bi);
    int64_t n, idx = 0;
    int32_t cap, len = 0, *map = NULL;
    UChar *buf;
    if (!status || U_FAILURE(*status))
        return;
    if (!b || !ut || !ut->pFuncs || !ut->pFuncs->access || !ut->pFuncs->nativeLength) {
        *status = U_ILLEGAL_ARGUMENT_ERROR;
        return;
    }
    n = ut->pFuncs->nativeLength(ut);
    if (n < 0 || n > 0x3FFFFFFF) {
        *status = U_INDEX_OUTOFBOUNDS_ERROR;
        return;
    }
    cap = (int32_t)n + 1;
    buf = malloc(sizeof(UChar) * cap);
    if (!buf) {
        *status = U_MEMORY_ALLOCATION_ERROR;
        return;
    }
    while (idx < n && ut->pFuncs->access(ut, idx, true)) {
        int32_t k, cl = ut->chunkLength;
        int64_t start = ut->chunkNativeStart;
        int ident = ut->nativeIndexingLimit >= cl && ut->chunkNativeLimit - start == cl;
        if (!ident && !map) {
            /* native indexes are not UTF-16 offsets here: keep a map */
            int32_t m;
            if (!ut->pFuncs->mapOffsetToNative || !ut->pFuncs->mapNativeIndexToUTF16 ||
                !(map = malloc(sizeof(int32_t) * cap)))
                goto fail;
            for (m = 0; m < len; m++)
                map[m] = m;
        }
        k = ident ? (int32_t)(idx - start) : ut->pFuncs->mapNativeIndexToUTF16(ut, idx);
        if (k < 0 || k > cl || ut->chunkNativeLimit <= idx)
            break;
        if (len + (cl - k) >= cap) {
            int32_t nc = len + (cl - k) + 1;
            UChar *nb = realloc(buf, sizeof(UChar) * nc);
            if (!nb)
                goto fail;
            buf = nb;
            if (map) {
                int32_t *nm = realloc(map, sizeof(int32_t) * nc);
                if (!nm)
                    goto fail;
                map = nm;
            }
            cap = nc;
        }
        for (; k < cl; k++) {
            if (map) {
                if (ident)
                    map[len] = (int32_t)(start + k);
                else {
                    ut->chunkOffset = k;
                    map[len] = (int32_t)ut->pFuncs->mapOffsetToNative(ut);
                }
            }
            buf[len++] = ut->chunkContents[k];
        }
        idx = ut->chunkNativeLimit;
    }
    if (map)
        map[len] = (int32_t)n;
    buf[len] = 0;
    set_text(b, buf, len);
    b->own = buf;
    b->map = map;
    b->nativeLen = (int32_t)n;
    return;
fail:
    free(buf);
    free(map);
    *status = U_MEMORY_ALLOCATION_ERROR;
}

int32_t uo_ubrk_current(const UBreakIterator *bi)
{
    struct uo_bi *b = bi_of((UBreakIterator *)bi);
    return b ? native(b, b->pos) : 0;
}

int32_t uo_ubrk_first(UBreakIterator *bi)
{
    struct uo_bi *b = bi_of(bi);
    if (!b)
        return UBRK_DONE;
    b->pos = 0;
    return native(b, 0);
}

int32_t uo_ubrk_last(UBreakIterator *bi)
{
    struct uo_bi *b = bi_of(bi);
    if (!b)
        return UBRK_DONE;
    b->pos = b->len;
    return native(b, b->len);
}

int32_t uo_ubrk_next(UBreakIterator *bi)
{
    struct uo_bi *b = bi_of(bi);
    if (!b || b->pos >= b->len)
        return UBRK_DONE;
    b->pos = after(b, b->pos);
    return native(b, b->pos);
}

int32_t uo_ubrk_previous(UBreakIterator *bi)
{
    struct uo_bi *b = bi_of(bi);
    if (!b || b->pos <= 0)
        return UBRK_DONE;
    b->pos = before(b, b->pos);
    return native(b, b->pos);
}

int32_t uo_ubrk_following(UBreakIterator *bi, int32_t offset)
{
    struct uo_bi *b = bi_of(bi);
    int32_t i;
    if (!b)
        return UBRK_DONE;
    if (offset < 0)
        return uo_ubrk_first(bi);
    i = from_native(b, offset);
    if (i >= b->len) {
        b->pos = b->len;
        return UBRK_DONE;
    }
    b->pos = after(b, snap(b, i));
    return native(b, b->pos);
}

int32_t uo_ubrk_preceding(UBreakIterator *bi, int32_t offset)
{
    struct uo_bi *b = bi_of(bi);
    int32_t i;
    if (!b)
        return UBRK_DONE;
    i = from_native(b, offset);
    if (i > b->len)
        return uo_ubrk_last(bi);
    i = snap(b, i);
    if (i <= 0) {
        b->pos = 0;
        return UBRK_DONE;
    }
    b->pos = before(b, i);
    return native(b, b->pos);
}

UBool uo_ubrk_isBoundary(UBreakIterator *bi, int32_t offset)
{
    struct uo_bi *b = bi_of(bi);
    int32_t i, s;
    if (!b)
        return 0;
    if (offset < 0) {
        b->pos = 0;
        return 0;
    }
    i = from_native(b, offset);
    if (i > b->len) {
        b->pos = b->len;
        return 0;
    }
    s = snap(b, i);
    if (s == i && (!b->map || b->map[i] == offset) && is_break(b, i)) {
        b->pos = i;
        return 1;
    }
    /* not a boundary: the iterator goes to the next one */
    b->pos = s >= b->len ? b->len : after(b, s);
    return 0;
}

int32_t uo_ubrk_getRuleStatus(UBreakIterator *bi)
{
    struct uo_bi *b = bi_of(bi);
    int32_t e, s;
    if (!b || b->pos <= 0)
        return 0;
    e = b->pos;
    switch (b->type) {
    case UBRK_LINE: {
        int x = lb_of(b, cp_at(b, prev_i(b, e)));
        return IN(x, HARDS) ? UBRK_LINE_HARD : UBRK_LINE_SOFT;
    }
    case UBRK_WORD:
        s = before(b, e);
        return word_status(b, s, e);
    case UBRK_SENTENCE:
        s = before(b, e);
        return sent_status(b, s, e);
    default:
        return 0;
    }
}
