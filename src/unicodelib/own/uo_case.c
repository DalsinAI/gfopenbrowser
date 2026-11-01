/* unicode.library, our own code: case mapping and case folding.
 * Copyright (c) 2026 AmigaChrome Team. MIT.
 *
 * ICU's C calls for case, by their own interface, on the tables
 * gen_props.py makes from UnicodeData.txt, SpecialCasing.txt and
 * CaseFolding.txt. The conditional mappings of SpecialCasing.txt (final
 * sigma, Lithuanian and Turkic i, dot above) are written out below;
 * gen_props.py stops the build when Unicode changes their list.
 *
 * Still ICU's: Greek uppercasing (locale "el", which drops accents) and
 * u_strToTitle, which needs word breaking (step 7 piece 2).
 */
#include <stdlib.h>
#include <string.h>

#include <unicode/uchar.h>
#include <unicode/uloc.h>
#include <unicode/ustring.h>
#include <unicode/utypes.h>

#include "uo_data.h"
#include "uo_own.h"

#define IN_RANGE(c) ((uint32_t)(c) <= 0x10FFFF)

enum { ROOT, TURKISH, LITHUANIAN, GREEK, DUTCH };

static const struct uo_case *caseOf(UChar32 c)
{
    return &uo_cases[IN_RANGE(c) ? UO_CASE(c) : 0];
}

/* ---- single code points ----------------------------------------------------- */

UChar32 uo_u_tolower(UChar32 c)
{
    return c + caseOf(c)->lower;
}

UChar32 uo_u_toupper(UChar32 c)
{
    return c + caseOf(c)->upper;
}

UChar32 uo_u_totitle(UChar32 c)
{
    return c + caseOf(c)->title;
}

UChar32 uo_u_foldCase(UChar32 c, uint32_t options)
{
    if (c == 0x49)
        return (options & U_FOLD_CASE_EXCLUDE_SPECIAL_I) ? 0x131 : 0x69;
    if (c == 0x130)
        return (options & U_FOLD_CASE_EXCLUDE_SPECIAL_I) ? 0x69 : 0x130;
    return c + caseOf(c)->fold;
}

/* ---- the context of a code point in its string ----------------------------- */

struct text {
    const UChar *s;
    int32_t length;
};

/* the code point before index i (and its start in *at), or -1 */
static UChar32 before(const struct text *t, int32_t *at)
{
    int32_t i = *at;
    UChar32 c;

    if (i <= 0)
        return -1;
    c = t->s[--i];
    if (U16_IS_TRAIL(c) && i > 0 && U16_IS_LEAD(t->s[i - 1]))
        c = U16_GET_SUPPLEMENTARY(t->s[--i], c);
    *at = i;
    return c;
}

/* the code point at index *at (moving *at past it), or -1 */
static UChar32 after(const struct text *t, int32_t *at)
{
    int32_t i = *at;
    UChar32 c;

    if (i >= t->length)
        return -1;
    c = t->s[i++];
    if (U16_IS_LEAD(c) && i < t->length && U16_IS_TRAIL(t->s[i]))
        c = U16_GET_SUPPLEMENTARY(c, t->s[i++]);
    *at = i;
    return c;
}

enum { NO_DOT, SOFT_DOTTED, ABOVE, OTHER_ACCENT };

static int dotType(UChar32 c)
{
    unsigned f = caseOf(c)->flags;
    if (f & UO_SOFT_DOTTED)
        return SOFT_DOTTED;
    if (f & UO_ABOVE)
        return ABOVE;
    if (f & UO_OTHER_ACCENT)
        return OTHER_ACCENT;
    return NO_DOT;
}

/* Final_Sigma: a cased letter before or after c, case-ignorables skipped */
static int casedLetter(const struct text *t, int32_t i, int forward)
{
    UChar32 c;

    while ((c = forward ? after(t, &i) : before(t, &i)) >= 0) {
        unsigned f = caseOf(c)->flags;
        if (f & UO_IGNORABLE)
            continue;
        return (f & UO_CASED) != 0;
    }
    return 0;
}

/* More_Above (lt) */
static int moreAbove(const struct text *t, int32_t i)
{
    UChar32 c;
    while ((c = after(t, &i)) >= 0) {
        int d = dotType(c);
        if (d == ABOVE)
            return 1;
        if (d != OTHER_ACCENT)
            return 0;
    }
    return 0;
}

/* After_Soft_Dotted (lt) */
static int afterSoftDotted(const struct text *t, int32_t i)
{
    UChar32 c;
    while ((c = before(t, &i)) >= 0) {
        int d = dotType(c);
        if (d == SOFT_DOTTED)
            return 1;
        if (d != OTHER_ACCENT)
            return 0;
    }
    return 0;
}

/* After_I (tr, az) */
static int afterI(const struct text *t, int32_t i)
{
    UChar32 c;
    while ((c = before(t, &i)) >= 0) {
        if (c == 0x49)
            return 1;
        if (dotType(c) != OTHER_ACCENT)
            return 0;
    }
    return 0;
}

/* Before_Dot (tr, az: the opposite of Not_Before_Dot) */
static int beforeDot(const struct text *t, int32_t i)
{
    UChar32 c;
    while ((c = after(t, &i)) >= 0) {
        if (c == 0x307)
            return 1;
        if (dotType(c) != OTHER_ACCENT)
            return 0;
    }
    return 0;
}

static const struct uo_full *fullOf(UChar32 c)
{
    int lo = 0, hi = UO_FULL_COUNT - 1;
    while (lo <= hi) {
        int mid = (lo + hi) >> 1;
        if (uo_full[mid].c == c)
            return &uo_full[mid];
        if (uo_full[mid].c < c)
            lo = mid + 1;
        else
            hi = mid - 1;
    }
    return 0;
}

/* ---- output ------------------------------------------------------------------ */

struct sink {
    UChar *dest;
    int32_t capacity, length;
};

static void put(struct sink *o, UChar u)
{
    if (o->length < o->capacity)
        o->dest[o->length] = u;
    o->length++;
}

static void putc32(struct sink *o, UChar32 c)
{
    if (c <= 0xFFFF) {
        put(o, (UChar)c);
    } else {
        put(o, U16_LEAD(c));
        put(o, U16_TRAIL(c));
    }
}

static void putString(struct sink *o, const UChar *s, int n)
{
    while (n-- > 0)
        put(o, *s++);
}

static int putFull(struct sink *o, UChar32 c, int which)
{
    const struct uo_full *f;

    if (!(caseOf(c)->flags & UO_FULL) || !(f = fullOf(c)) || f->s[which][0] == 0xFFFF)
        return 0;
    putString(o, uo_full_str + f->s[which][0], f->s[which][1]);
    return 1;
}

#define PUT(o, ...) do { static const UChar u_[] = { __VA_ARGS__ }; putString(o, u_, sizeof u_ / sizeof u_[0]); } while (0)

/* one code point lowercased, at index i (its start) and next (after it) */
static void lowerOne(struct sink *o, const struct text *t, UChar32 c, int32_t i, int32_t next, int loc)
{
    switch (c) {
    case 0x49:
    case 0x4A:
    case 0x12E:
        if (loc == LITHUANIAN && moreAbove(t, next)) {
            putc32(o, c == 0x49 ? 0x69 : c == 0x4A ? 0x6A : 0x12F);
            put(o, 0x307);
            return;
        }
        if (c == 0x49 && loc == TURKISH && !beforeDot(t, next)) {
            put(o, 0x131);
            return;
        }
        break;
    case 0xCC:
    case 0xCD:
    case 0x128:
        if (loc == LITHUANIAN) {
            put(o, 0x69);
            put(o, 0x307);
            put(o, c == 0xCC ? 0x300 : c == 0xCD ? 0x301 : 0x303);
            return;
        }
        break;
    case 0x130:
        if (loc == TURKISH) {
            put(o, 0x69);
            return;
        }
        PUT(o, 0x69, 0x307);
        return;
    case 0x307:
        if (loc == TURKISH && afterI(t, i))
            return;
        break;
    case 0x3A3:
        if (!casedLetter(t, next, 1) && casedLetter(t, i, 0)) {
            put(o, 0x3C2);
            return;
        }
        break;
    }
    if (!putFull(o, c, UO_FULL_LOWER))
        putc32(o, c + caseOf(c)->lower);
}

static void upperOne(struct sink *o, const struct text *t, UChar32 c, int32_t i, int loc)
{
    if (c == 0x69 && loc == TURKISH) {
        put(o, 0x130);
        return;
    }
    if (c == 0x307 && loc == LITHUANIAN && afterSoftDotted(t, i))
        return;
    if (!putFull(o, c, UO_FULL_UPPER))
        putc32(o, c + caseOf(c)->upper);
}

static void foldOne(struct sink *o, UChar32 c, uint32_t options)
{
    if (c == 0x49) {
        put(o, (options & U_FOLD_CASE_EXCLUDE_SPECIAL_I) ? 0x131 : 0x69);
        return;
    }
    if (c == 0x130) {
        if (options & U_FOLD_CASE_EXCLUDE_SPECIAL_I)
            put(o, 0x69);
        else
            PUT(o, 0x69, 0x307);
        return;
    }
    if (!putFull(o, c, UO_FULL_FOLD))
        putc32(o, c + caseOf(c)->fold);
}

/* ---- strings ----------------------------------------------------------------- */

/* ICU's language of a locale for case mapping (ucase_getCaseLocale) */
static int caseLocale(const char *locale)
{
    char lang[4];
    int n = 0;

    if (!locale)
        locale = uloc_getDefault();             /* ICU, until piece 5 */
    while (n < 4 && locale[n] && locale[n] != '_' && locale[n] != '-') {
        char x = locale[n];
        lang[n++] = (x >= 'A' && x <= 'Z') ? x + 32 : x;
    }
    if (n == 4)
        return ROOT;
#define IS(s) (n == (int)sizeof(s) - 1 && !__builtin_memcmp(lang, s, n))
    if (IS("tr") || IS("tur") || IS("az") || IS("aze"))
        return TURKISH;
    if (IS("lt") || IS("lit"))
        return LITHUANIAN;
    if (IS("el") || IS("ell"))
        return GREEK;
    if (IS("nl") || IS("nld"))
        return DUTCH;
#undef IS
    return ROOT;
}

/* ICU's checks and its rules for the end of a string (ustrcase_map); the
 * overlap test only matters once COPY_IF_OVERLAPPING has run */
static int prepare(UChar *dest, int32_t capacity, const UChar *src, int32_t *length, UErrorCode *err)
{
    if (!err || U_FAILURE(*err))
        return 0;
    if (capacity < 0 || (!dest && capacity > 0) || !src || *length < -1) {
        *err = U_ILLEGAL_ARGUMENT_ERROR;
        return 0;
    }
    if (*length == -1)
        *length = u_strlen(src);
    if (dest && ((src >= dest && src < dest + capacity) || (dest >= src && dest < src + *length))) {
        *err = U_ILLEGAL_ARGUMENT_ERROR;
        return 0;
    }
    return 1;
}

static int32_t finish(struct sink *o, UErrorCode *err)
{
    if (o->length > o->capacity)
        *err = U_BUFFER_OVERFLOW_ERROR;
    else if (o->length == o->capacity)
        *err = U_STRING_NOT_TERMINATED_WARNING;
    else {
        o->dest[o->length] = 0;
        if (*err == U_STRING_NOT_TERMINATED_WARNING)
            *err = U_ZERO_ERROR;
    }
    return o->length;
}

/* ICU maps a string onto itself, or onto one it overlaps, through a copy */
static int32_t overlapping(const UChar *dest, int32_t capacity, const UChar *src, int32_t length,
                           const UErrorCode *err)
{
    int32_t n;

    if (!err || U_FAILURE(*err) || !dest || !src || length < -1 || capacity <= 0)
        return -1;
    n = length == -1 ? u_strlen(src) : length;
    if ((src >= dest && src < dest + capacity) || (dest >= src && dest < src + n))
        return n;
    return -1;
}

#define COPY_IF_OVERLAPPING(call)                                                   \
    do {                                                                            \
        int32_t n = overlapping(dest, capacity, src, length, err);                  \
        if (n >= 0) {                                                               \
            UChar small[256], *copy = n <= 256 ? small : malloc(sizeof(UChar) * n); \
            int32_t r;                                                              \
            if (!copy) {                                                            \
                *err = U_MEMORY_ALLOCATION_ERROR;                                   \
                return 0;                                                           \
            }                                                                       \
            memcpy(copy, src, sizeof(UChar) * n);                                   \
            r = call;                                                               \
            if (copy != small)                                                      \
                free(copy);                                                         \
            return r;                                                               \
        }                                                                           \
    } while (0)

int32_t uo_u_strToLower(UChar *dest, int32_t capacity, const UChar *src, int32_t length,
                        const char *locale, UErrorCode *err)
{
    struct sink o = { dest, capacity, 0 };
    struct text t;
    int32_t i = 0;
    int loc;

    COPY_IF_OVERLAPPING(uo_u_strToLower(dest, capacity, copy, n, locale, err));
    if (!prepare(dest, capacity, src, &length, err))
        return 0;
    loc = caseLocale(locale);
    t.s = src;
    t.length = length;
    while (i < length) {
        int32_t start = i;
        UChar32 c = after(&t, &i);
        lowerOne(&o, &t, c, start, i, loc);
    }
    return finish(&o, err);
}

int32_t uo_u_strToUpper(UChar *dest, int32_t capacity, const UChar *src, int32_t length,
                        const char *locale, UErrorCode *err)
{
    struct sink o = { dest, capacity, 0 };
    struct text t;
    int32_t i = 0;
    int loc;

    COPY_IF_OVERLAPPING(uo_u_strToUpper(dest, capacity, copy, n, locale, err));
    if (!prepare(dest, capacity, src, &length, err))
        return 0;
    loc = caseLocale(locale);
    if (loc == GREEK)
        return u_strToUpper(dest, capacity, src, length, locale, err);   /* ICU */
    t.s = src;
    t.length = length;
    while (i < length) {
        int32_t start = i;
        UChar32 c = after(&t, &i);
        upperOne(&o, &t, c, start, loc);
    }
    return finish(&o, err);
}

int32_t uo_u_strFoldCase(UChar *dest, int32_t capacity, const UChar *src, int32_t length,
                         uint32_t options, UErrorCode *err)
{
    struct sink o = { dest, capacity, 0 };
    struct text t;
    int32_t i = 0;

    COPY_IF_OVERLAPPING(uo_u_strFoldCase(dest, capacity, copy, n, options, err));
    if (!prepare(dest, capacity, src, &length, err))
        return 0;
    t.s = src;
    t.length = length;
    while (i < length)
        foldOne(&o, after(&t, &i), options);
    return finish(&o, err);
}
