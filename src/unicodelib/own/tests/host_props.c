/* unicode.library, our own code: our property and case calls against
 * ICU 78's own, on the host, for every code point.
 * Copyright (c) 2026 AmiCradle Team. MIT.
 *
 * Built by tests/run-host.sh with our uo_*.c, the generated uo_data.c and
 * the host's build of ICU 78.3. Prints one line per call: PASS with the
 * number of cases, or FAIL with the first differences; returns 1 on any.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <unicode/uchar.h>
#include <unicode/uloc.h>
#include <unicode/uscript.h>
#include <unicode/ustring.h>

#include "uo_own.h"

static int failures;
static long cases, bad;
static const char *what;

static void begin(const char *name)
{
    what = name;
    cases = bad = 0;
}

static void differ(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
#include <stdarg.h>
static void differ(const char *fmt, ...)
{
    va_list ap;
    if (bad++ < 6) {
        printf("  %s: ", what);
        va_start(ap, fmt);
        vprintf(fmt, ap);
        va_end(ap);
        printf("\n");
    }
}

static void end(void)
{
    printf("%s %s %ld cases%s", bad ? "FAIL" : "PASS", what, cases, bad ? "" : "\n");
    if (bad) {
        printf(", %ld differ\n", bad);
        failures++;
    }
}

#define FIRST (-2)
#define LAST 0x110001

#define EACH_CP(name, expr_ours, expr_icu, fmt)                                      \
    do {                                                                             \
        UChar32 c;                                                                   \
        begin(name);                                                                 \
        for (c = FIRST; c <= LAST; c++) {                                            \
            long a = (long)(expr_ours), b = (long)(expr_icu);                       \
            cases++;                                                                 \
            if (a != b)                                                              \
                differ("U+%04X ours " fmt " ICU " fmt, (unsigned)c, a, b);          \
        }                                                                            \
        end();                                                                       \
    } while (0)

/* ---- strings ------------------------------------------------------------------- */

typedef int32_t (*casefn)(UChar *, int32_t, const UChar *, int32_t, const char *, UErrorCode *);

/* the fold calls take options where the others take a locale; "" is 0 */
static uint32_t opts(const char *o)
{
    return (uintptr_t)o < 16 ? (uint32_t)(uintptr_t)o : 0;
}

static int32_t foldOurs(UChar *d, int32_t n, const UChar *s, int32_t l, const char *o, UErrorCode *e)
{
    return uo_u_strFoldCase(d, n, s, l, opts(o), e);
}

static int32_t foldICU(UChar *d, int32_t n, const UChar *s, int32_t l, const char *o, UErrorCode *e)
{
    return u_strFoldCase(d, n, s, l, opts(o), e);
}

static void hexs(char *out, const UChar *s, int32_t n)
{
    int32_t i;
    *out = 0;
    for (i = 0; i < n && i < 12; i++)
        out += sprintf(out, "%s%04X", i ? " " : "", s[i]);
}

static void compareString(casefn ours, casefn icu, const UChar *src, int32_t len, const char *arg, int locArg)
{
    UChar a[64], b[64];
    int32_t caps[] = { 64, -2, -3, -4, 0 }, k;

    for (k = 0; k < 5; k++) {
        UErrorCode ea = U_ZERO_ERROR, eb = U_ZERO_ERROR;
        int32_t cap = caps[k], ra, rb;
        if (cap < 0) {
            /* exact, one short, one more than needed */
            UErrorCode e = U_ZERO_ERROR;
            int32_t need = icu(NULL, 0, src, len, arg, &e);
            cap = need + (cap == -2 ? 0 : cap == -3 ? -1 : 1);
            if (cap < 0 || cap > 64)
                continue;
        }
        memset(a, 0x5A, sizeof a);
        memset(b, 0x5A, sizeof b);
        ra = ours(cap ? a : NULL, cap, src, len, arg, &ea);
        rb = icu(cap ? b : NULL, cap, src, len, arg, &eb);
        cases++;
        if (ra != rb || ea != eb || (U_SUCCESS(eb) && memcmp(a, b, sizeof(UChar) * (rb < cap ? rb + 1 : rb)))) {
            char hs[80], ha[80], hb[80];
            hexs(hs, src, len < 0 ? u_strlen(src) : len);
            hexs(ha, a, ra < 64 ? ra : 0);
            hexs(hb, b, rb < 64 ? rb : 0);
            if (locArg)
                differ("[%s] %s cap %d: ours %d %s [%s] ICU %d %s [%s]", hs, arg ? arg : "NULL", cap,
                       ra, u_errorName(ea), ha, rb, u_errorName(eb), hb);
            else
                differ("[%s] options %d cap %d: ours %d %s [%s] ICU %d %s [%s]", hs, (int)(uintptr_t)arg, cap,
                       ra, u_errorName(ea), ha, rb, u_errorName(eb), hb);
        }
    }
}

static int32_t put32(UChar *s, int32_t n, UChar32 c)
{
    if (c > 0xFFFF) {
        s[n++] = U16_LEAD(c);
        s[n++] = U16_TRAIL(c);
    } else {
        s[n++] = (UChar)c;
    }
    return n;
}

static const char *const locales[] = { "", "en", "tr", "TR_tr", "az", "AZE", "tur", "lt", "lit", "LT-lt",
                                       "nl", "de", "trx", "t", "@x", "tr@x", "lt_LT@foo=bar", NULL };

/* the characters the conditional mappings look at, and some others */
static const UChar32 context[] = { 0x41, 0x61, 0x49, 0x69, 0x4A, 0x12E, 0xCC, 0x130, 0x131, 0x307, 0x300,
                                   0x323, 0x345, 0x3A3, 0x3C3, 0x3C2, 0x391, 0x27, 0xAD, 0x2E, 0x20, 0xDF,
                                   0x1F80, 0x1E9E, 0x10400, 0x1D400, 0xD800, 0x6A, 0x1E2D, 0x0, 0x0 };

static void strings(const char *name, casefn ours, casefn icu, int locArg)
{
    UChar s[32];
    UChar32 c;
    int i, j, k, l, nctx = 0, nargs = locArg ? (int)(sizeof locales / sizeof locales[0]) : 2;
    unsigned seed = 1;

    while (context[nctx])
        nctx++;
    begin(name);
    for (l = 0; l < nargs; l++) {
        const char *arg = locArg ? locales[l] : (const char *)(uintptr_t)(l ? U_FOLD_CASE_EXCLUDE_SPECIAL_I : 0);
        /* every code point alone, and NUL-terminated */
        for (c = 1; c < 0x110000; c++) {
            int32_t n = put32(s, 0, c);
            compareString(ours, icu, s, n, arg, locArg);
            if ((c & 0xFFF) == 0x123) {
                s[n] = 0;
                compareString(ours, icu, s, -1, arg, locArg);
            }
        }
        /* every string of up to three context characters */
        for (i = 0; i < nctx; i++)
            for (j = -1; j < nctx; j++)
                for (k = -1; k < nctx; k++) {
                    int32_t n = put32(s, 0, context[i]);
                    if (j >= 0)
                        n = put32(s, n, context[j]);
                    if (k >= 0)
                        n = put32(s, n, context[k]);
                    compareString(ours, icu, s, n, arg, locArg);
                }
        /* random strings of cased letters and marks */
        for (i = 0; i < 20000; i++) {
            int32_t n = 0, len = 1 + (int)((seed = seed * 1103515245 + 12345) >> 16) % 7;
            for (j = 0; j < len; j++) {
                seed = seed * 1103515245 + 12345;
                if ((seed >> 16) % 3 == 0)
                    c = context[(seed >> 18) % nctx];
                else
                    c = (UChar32)((seed >> 8) % 0x20000);
                if (c >= 0xD800 && c <= 0xDFFF && (seed & 1))
                    c = 0x3A3;
                n = put32(s, n, c);
            }
            compareString(ours, icu, s, n, arg, locArg);
        }
    }
    /* errors */
    {
        UErrorCode e1 = U_ZERO_ERROR, e2 = U_ZERO_ERROR;
        UChar d[4];
        s[0] = 0x41;
        cases++;
        if (ours(NULL, 4, s, 1, "", &e1) != icu(NULL, 4, s, 1, "", &e2) || e1 != e2)
            differ("NULL dest with capacity: ours %s ICU %s", u_errorName(e1), u_errorName(e2));
        e1 = e2 = U_ZERO_ERROR;
        cases++;
        if (ours(d, 4, NULL, 1, "", &e1) != icu(d, 4, NULL, 1, "", &e2) || e1 != e2)
            differ("NULL src: ours %s ICU %s", u_errorName(e1), u_errorName(e2));
        {
            UChar x[12] = { 0x41, 0x42, 0xDF, 0x130, 0x44, 0x45, 0 }, y[12];
            int32_t r1, r2;
            memcpy(y, x, sizeof x);
            e1 = e2 = U_ZERO_ERROR;
            cases++;
            r1 = ours(x, 10, x + 1, 4, "", &e1);
            r2 = icu(y, 10, y + 1, 4, "", &e2);
            if (r1 != r2 || e1 != e2 || memcmp(x, y, sizeof x))
                differ("overlap: ours %d %s ICU %d %s", r1, u_errorName(e1), r2, u_errorName(e2));
        }
        e1 = e2 = U_INVALID_CHAR_FOUND;
        cases++;
        if (ours(d, 4, s, 1, "", &e1) != icu(d, 4, s, 1, "", &e2) || e1 != e2)
            differ("failure in: ours %s ICU %s", u_errorName(e1), u_errorName(e2));
    }
    end();
}

/* ---- u_enumCharTypes and the property starts ----------------------------------------- */

struct ranges {
    int n;
    int32_t r[8000][3];
};

static UBool U_CALLCONV typeRange(const void *ctx, UChar32 start, UChar32 limit, UCharCategory type)
{
    struct ranges *r = (struct ranges *)(uintptr_t)ctx;
    if (r->n < 8000) {
        r->r[r->n][0] = start;
        r->r[r->n][1] = limit;
        r->r[r->n][2] = type;
    }
    r->n++;
    return 1;
}

static unsigned char *startBits;

static void addStart(void *ctx, UChar32 c)
{
    (void)ctx;
    if (c >= 0 && c <= 0x110000)
        startBits[c >> 3] |= (unsigned char)(1 << (c & 7));
}

/* every code point where one of ICU's answers changes must be a start */
static void startsCover(void)
{
    static uint32_t prev[UCHAR_BINARY_LIMIT + (UCHAR_INT_LIMIT - UCHAR_INT_START) + 8];
    UChar32 c;
    int p;

    startBits = calloc(0x110000 / 8 + 1, 1);
    uo_prop_starts(addStart, 0);
    begin("property starts");
    for (c = 0; c < 0x110000; c++) {
        uint32_t v[sizeof prev / sizeof prev[0]];
        int n = 0, changed = 0;
        UVersionInfo age;
        double d;
        for (p = UCHAR_BINARY_START; p < UCHAR_BINARY_LIMIT; p++)
            if (p != UCHAR_CASE_SENSITIVE)  /* ICU's case data has its own starts */
                v[n++] = u_hasBinaryProperty(c, (UProperty)p);
        for (p = UCHAR_INT_START; p < UCHAR_INT_LIMIT; p++)
            v[n++] = (uint32_t)u_getIntPropertyValue(c, (UProperty)p);
        u_charAge(c, age);
        v[n++] = (uint32_t)(age[0] << 8 | age[1]);
        d = u_getNumericValue(c);
        v[n++] = (uint32_t)(int64_t)(d * 1000);
        v[n++] = (uint32_t)u_isspace(c) << 3 | u_isWhitespace(c) << 2 | u_isIDIgnorable(c) << 1 | u_isxdigit(c);
        v[n++] = (uint32_t)u_getIntPropertyValue(c, UCHAR_LEAD_CANONICAL_COMBINING_CLASS);
        {
            UScriptCode s[40];
            UErrorCode e = U_ZERO_ERROR;
            int32_t k, m = uscript_getScriptExtensions(c, s, 40, &e);
            uint32_t h = (uint32_t)m;
            for (k = 0; k < m; k++)
                h = h * 31 + (uint32_t)s[k];
            v[n++] = h;
        }
        for (p = 0; p < n; p++)
            if (!c || v[p] != prev[p])
                changed = 1;
        memcpy(prev, v, sizeof(uint32_t) * (size_t)n);
        cases++;
        if (changed && !(startBits[c >> 3] & (1 << (c & 7))))
            differ("U+%04X: a property changes, no start", (unsigned)c);
    }
    end();
    free(startBits);
}

int main(void)
{
    int p, sc;

    printf("host: ICU %s, Unicode %s\n", U_ICU_VERSION, U_UNICODE_VERSION);
    EACH_CP("u_charType", uo_u_charType(c), u_charType(c), "%ld");
    EACH_CP("u_charDirection", uo_u_charDirection(c), u_charDirection(c), "%ld");
    EACH_CP("u_getCombiningClass", uo_u_getCombiningClass(c), u_getCombiningClass(c), "%ld");
    EACH_CP("u_charMirror", uo_u_charMirror(c), u_charMirror(c), "%lX");
    EACH_CP("u_isalpha", uo_u_isalpha(c), u_isalpha(c), "%ld");
    EACH_CP("u_isalnum", uo_u_isalnum(c), u_isalnum(c), "%ld");
    EACH_CP("u_isdigit", uo_u_isdigit(c), u_isdigit(c), "%ld");
    EACH_CP("u_ispunct", uo_u_ispunct(c), u_ispunct(c), "%ld");
    EACH_CP("u_isprint", uo_u_isprint(c), u_isprint(c), "%ld");
    EACH_CP("u_isUWhiteSpace", uo_u_isUWhiteSpace(c), u_isUWhiteSpace(c), "%ld");
    EACH_CP("u_tolower", uo_u_tolower(c), u_tolower(c), "%lX");
    EACH_CP("u_toupper", uo_u_toupper(c), u_toupper(c), "%lX");
    EACH_CP("u_totitle", uo_u_totitle(c), u_totitle(c), "%lX");
    EACH_CP("u_foldCase 0", uo_u_foldCase(c, 0), u_foldCase(c, 0), "%lX");
    EACH_CP("u_foldCase I", uo_u_foldCase(c, U_FOLD_CASE_EXCLUDE_SPECIAL_I), u_foldCase(c, U_FOLD_CASE_EXCLUDE_SPECIAL_I), "%lX");
    EACH_CP("ublock_getCode", uo_ublock_getCode(c), ublock_getCode(c), "%ld");
    {
        UErrorCode e1, e2;
        EACH_CP("uscript_getScript", (e1 = U_ZERO_ERROR, uo_uscript_getScript(c, &e1) * 1000 + e1),
                (e2 = U_ZERO_ERROR, uscript_getScript(c, &e2) * 1000 + e2), "%ld");
    }
    for (p = UCHAR_BINARY_START; p < UCHAR_BINARY_LIMIT; p++) {
        char name[64];
        snprintf(name, sizeof name, "u_hasBinaryProperty %s", u_getPropertyName((UProperty)p, U_LONG_PROPERTY_NAME));
        EACH_CP(name, uo_u_hasBinaryProperty(c, (UProperty)p), u_hasBinaryProperty(c, (UProperty)p), "%ld");
    }
    for (p = UCHAR_INT_START; p <= UCHAR_INT_LIMIT; p++) {
        char name[80];
        UProperty q = p == UCHAR_INT_LIMIT ? UCHAR_GENERAL_CATEGORY_MASK : (UProperty)p;
        snprintf(name, sizeof name, "u_getIntPropertyValue %s", u_getPropertyName(q, U_LONG_PROPERTY_NAME));
        EACH_CP(name, uo_u_getIntPropertyValue(c, q), u_getIntPropertyValue(c, q), "%ld");
    }
    {
        static const int odd[] = { -2, -1, UCHAR_BINARY_LIMIT, 0xFFF, UCHAR_INT_LIMIT, UCHAR_GENERAL_CATEGORY_MASK,
                                   UCHAR_NUMERIC_VALUE, UCHAR_AGE, UCHAR_SCRIPT_EXTENSIONS, UCHAR_IDENTIFIER_TYPE, 0x7FFF };
        unsigned i;
        begin("u_getIntPropertyMaxValue, u_getIntPropertyValue of other properties");
        for (p = UCHAR_BINARY_START - 2; p < UCHAR_INT_LIMIT + (int)(sizeof odd / sizeof odd[0]); p++) {
            int q = p < UCHAR_BINARY_LIMIT ? p : p < UCHAR_INT_START ? UCHAR_INT_START + (p - UCHAR_BINARY_LIMIT) : 0;
            if (p >= UCHAR_BINARY_LIMIT && q >= UCHAR_INT_LIMIT)
                q = odd[q - UCHAR_INT_LIMIT];
            cases++;
            if (uo_u_getIntPropertyMaxValue((UProperty)q) != u_getIntPropertyMaxValue((UProperty)q))
                differ("max %d ours %ld ICU %ld", q, (long)uo_u_getIntPropertyMaxValue((UProperty)q),
                       (long)u_getIntPropertyMaxValue((UProperty)q));
            if (q < 0 || (q >= UCHAR_BINARY_LIMIT && q < UCHAR_INT_START) || q >= UCHAR_INT_LIMIT) {
                cases++;
                if (uo_u_getIntPropertyValue(0x41, (UProperty)q) != u_getIntPropertyValue(0x41, (UProperty)q))
                    differ("value of %d ours %ld", q, (long)uo_u_getIntPropertyValue(0x41, (UProperty)q));
            }
        }
        for (i = 0; i < sizeof odd / sizeof odd[0]; i++) {
            cases++;
            if (uo_u_getIntPropertyMaxValue((UProperty)odd[i]) != u_getIntPropertyMaxValue((UProperty)odd[i]))
                differ("max %d ours %ld ICU %ld", odd[i], (long)uo_u_getIntPropertyMaxValue((UProperty)odd[i]),
                       (long)u_getIntPropertyMaxValue((UProperty)odd[i]));
        }
        end();
    }
    {
        UVersionInfo va, vb;
        EACH_CP("u_charAge", (uo_u_charAge(c, va), va[0] << 24 | va[1] << 16 | va[2] << 8 | va[3]),
                (u_charAge(c, vb), vb[0] << 24 | vb[1] << 16 | vb[2] << 8 | vb[3]), "%lX");
        uo_u_getUnicodeVersion(va);
        u_getUnicodeVersion(vb);
        begin("u_getUnicodeVersion");
        cases++;
        if (memcmp(va, vb, sizeof va))
            differ("ours %d.%d.%d.%d", va[0], va[1], va[2], va[3]);
        end();
    }
    {
        UChar32 c;
        begin("u_getNumericValue");
        for (c = FIRST; c <= LAST; c++) {
            double a = uo_u_getNumericValue(c), b = u_getNumericValue(c);
            cases++;
            if (memcmp(&a, &b, sizeof a))
                differ("U+%04X ours %.17g ICU %.17g", (unsigned)c, a, b);
        }
        end();
    }
    EACH_CP("u_charDigitValue", uo_u_charDigitValue(c), u_charDigitValue(c), "%ld");
    {
        static const int radix[] = { -1, 0, 1, 2, 8, 10, 11, 16, 35, 36, 37, 100 };
        unsigned i;
        int r;
        for (i = 0; i < sizeof radix / sizeof radix[0]; i++) {
            char name[40];
            r = radix[i];
            snprintf(name, sizeof name, "u_digit radix %d", r);
            EACH_CP(name, uo_u_digit(c, (int8_t)r), u_digit(c, (int8_t)r), "%ld");
        }
        begin("u_forDigit");
        for (r = -1; r <= 37; r++) {
            int d;
            for (d = -2; d <= 40; d++) {
                cases++;
                if (uo_u_forDigit(d, (int8_t)r) != u_forDigit(d, (int8_t)r))
                    differ("%d radix %d ours %X", d, r, (unsigned)uo_u_forDigit(d, (int8_t)r));
            }
        }
        end();
    }
    {
        int t;
        for (t = -1; t <= U_ID_TYPE_RECOMMENDED + 1; t++) {
            char name[40];
            snprintf(name, sizeof name, "u_hasIDType %d", t);
            EACH_CP(name, uo_u_hasIDType(c, (UIdentifierType)t), u_hasIDType(c, (UIdentifierType)t), "%ld");
        }
    }
    {
        UChar32 c;
        begin("u_getIDTypes");
        for (c = FIRST; c <= LAST; c++) {
            UIdentifierType a[16], b[16];
            int cap;
            for (cap = 0; cap <= 3; cap += 3) {
                UErrorCode e1 = U_ZERO_ERROR, e2 = U_ZERO_ERROR;
                int32_t n1 = uo_u_getIDTypes(c, a, cap, &e1), n2 = u_getIDTypes(c, b, cap, &e2);
                cases++;
                if (n1 != n2 || e1 != e2 || (cap && n1 > 0 && memcmp(a, b, sizeof a[0] * (n1 < cap ? n1 : cap))))
                    differ("U+%04X cap %d ours %d (%d) ICU %d (%d)", (unsigned)c, cap, n1, e1, n2, e2);
            }
        }
        end();
        begin("uscript_getScriptExtensions");
        for (c = FIRST; c <= LAST; c++) {
            UScriptCode a[40], b[40];
            int cap;
            for (cap = 0; cap <= 40; cap += 40) {
                UErrorCode e1 = U_ZERO_ERROR, e2 = U_ZERO_ERROR;
                int32_t n1 = uo_uscript_getScriptExtensions(c, a, cap, &e1);
                int32_t n2 = uscript_getScriptExtensions(c, b, cap, &e2);
                cases++;
                if (n1 != n2 || e1 != e2 || (cap && memcmp(a, b, sizeof a[0] * n1)))
                    differ("U+%04X cap %d ours %d (%d) ICU %d (%d)", (unsigned)c, cap, n1, e1, n2, e2);
            }
        }
        end();
    }
    EACH_CP("u_islower", uo_u_islower(c), u_islower(c), "%ld");
    EACH_CP("u_isupper", uo_u_isupper(c), u_isupper(c), "%ld");
    EACH_CP("u_istitle", uo_u_istitle(c), u_istitle(c), "%ld");
    EACH_CP("u_isdefined", uo_u_isdefined(c), u_isdefined(c), "%ld");
    EACH_CP("u_isxdigit", uo_u_isxdigit(c), u_isxdigit(c), "%ld");
    EACH_CP("u_isbase", uo_u_isbase(c), u_isbase(c), "%ld");
    EACH_CP("u_iscntrl", uo_u_iscntrl(c), u_iscntrl(c), "%ld");
    EACH_CP("u_isISOControl", uo_u_isISOControl(c), u_isISOControl(c), "%ld");
    EACH_CP("u_isspace", uo_u_isspace(c), u_isspace(c), "%ld");
    EACH_CP("u_isJavaSpaceChar", uo_u_isJavaSpaceChar(c), u_isJavaSpaceChar(c), "%ld");
    EACH_CP("u_isWhitespace", uo_u_isWhitespace(c), u_isWhitespace(c), "%ld");
    EACH_CP("u_isblank", uo_u_isblank(c), u_isblank(c), "%ld");
    EACH_CP("u_isgraph", uo_u_isgraph(c), u_isgraph(c), "%ld");
    EACH_CP("u_isIDIgnorable", uo_u_isIDIgnorable(c), u_isIDIgnorable(c), "%ld");
    EACH_CP("u_isJavaIDStart", uo_u_isJavaIDStart(c), u_isJavaIDStart(c), "%ld");
    EACH_CP("u_isJavaIDPart", uo_u_isJavaIDPart(c), u_isJavaIDPart(c), "%ld");
    EACH_CP("u_isUAlphabetic", uo_u_hasBinaryProperty(c, UCHAR_ALPHABETIC), u_isUAlphabetic(c), "%ld");
    EACH_CP("u_isIDStart", uo_u_hasBinaryProperty(c, UCHAR_ID_START), u_isIDStart(c), "%ld");
    EACH_CP("u_isIDPart", uo_u_hasBinaryProperty(c, UCHAR_ID_CONTINUE), u_isIDPart(c), "%ld");
    {
        struct ranges r1, r2;
        r1.n = r2.n = 0;
        uo_u_enumCharTypes(typeRange, &r1);
        u_enumCharTypes(typeRange, &r2);
        begin("u_enumCharTypes");
        cases++;
        if (r1.n != r2.n || memcmp(r1.r, r2.r, sizeof r1.r[0] * (size_t)r1.n))
            differ("ours %d ranges ICU %d", r1.n, r2.n);
        end();
    }
    startsCover();
    EACH_CP("u_getBidiPairedBracket", uo_u_getBidiPairedBracket(c), u_getBidiPairedBracket(c), "%lX");
    {
        static const UProperty bidiMax[] = { UCHAR_BIDI_CLASS, UCHAR_JOINING_GROUP, UCHAR_JOINING_TYPE,
            UCHAR_BIDI_PAIRED_BRACKET_TYPE };
        unsigned i;
        begin("ubidi_getMaxValue");
        for (i = 0; i < sizeof bidiMax / sizeof bidiMax[0]; i++) {
            cases++;
            if (uo_ubidi_getMaxValue(bidiMax[i]) != u_getIntPropertyMaxValue(bidiMax[i]))
                differ("%s ours %ld ICU %ld", u_getPropertyName(bidiMax[i], U_LONG_PROPERTY_NAME),
                       (long)uo_ubidi_getMaxValue(bidiMax[i]), (long)u_getIntPropertyMaxValue(bidiMax[i]));
        }
        end();
    }
    begin("uscript_hasScript");
    for (sc = -1; sc <= USCRIPT_CODE_LIMIT; sc++) {
        UChar32 c;
        for (c = FIRST; c <= LAST; c++) {
            cases++;
            if (uo_uscript_hasScript(c, (UScriptCode)sc) != uscript_hasScript(c, (UScriptCode)sc))
                differ("U+%04X script %d ours %d", (unsigned)c, sc, uo_uscript_hasScript(c, (UScriptCode)sc));
        }
    }
    end();
    begin("uscript_getShortName, uscript_isRightToLeft");
    for (sc = -2; sc <= USCRIPT_CODE_LIMIT + 1; sc++) {
        const char *a = uo_uscript_getShortName((UScriptCode)sc), *b = uscript_getShortName((UScriptCode)sc);
        cases += 2;
        if ((a == NULL) != (b == NULL) || (a && strcmp(a, b)))
            differ("short name %d ours %s ICU %s", sc, a ? a : "NULL", b ? b : "NULL");
        if (uo_uscript_isRightToLeft((UScriptCode)sc) != uscript_isRightToLeft((UScriptCode)sc))
            differ("right to left %d (%s) ours %d", sc, b ? b : "?", uo_uscript_isRightToLeft((UScriptCode)sc));
    }
    end();
    begin("u_getPropertyValueEnum script");
    for (sc = 0; sc < USCRIPT_CODE_LIMIT; sc++) {
        int k;
        for (k = 0; k < 4; k++) {
            const char *n = u_getPropertyValueName(UCHAR_SCRIPT, sc, (UPropertyNameChoice)(k & 1));
            char v[64];
            unsigned i;
            if (!n)
                n = uscript_getName((UScriptCode)sc);
            if (!n)
                continue;
            snprintf(v, sizeof v, "%s", n);
            if (k >= 2)
                for (i = 0; v[i]; i++)
                    v[i] = v[i] == '_' ? '-' : (v[i] >= 'a' && v[i] <= 'z') ? v[i] - 32 : v[i];
            cases++;
            if (uo_u_getPropertyValueEnum(UCHAR_SCRIPT, v) != u_getPropertyValueEnum(UCHAR_SCRIPT, v))
                differ("%s ours %d ICU %d", v, uo_u_getPropertyValueEnum(UCHAR_SCRIPT, v), u_getPropertyValueEnum(UCHAR_SCRIPT, v));
        }
    }
    {
        static const char *const odd[] = { "", "Zzzz", "zyyy", "Latin ", " l a t n", "Lat", "Latnx", "Qaac", "Qaai",
                                           "Katakana_Or_Hiragana", "Hrkt", "Han", "Hani", "Meetei_Mayek", "xyz", 0 };
        int i;
        for (i = 0; odd[i]; i++) {
            cases++;
            if (uo_u_getPropertyValueEnum(UCHAR_SCRIPT, odd[i]) != u_getPropertyValueEnum(UCHAR_SCRIPT, odd[i]))
                differ("%s ours %d ICU %d", odd[i], uo_u_getPropertyValueEnum(UCHAR_SCRIPT, odd[i]), u_getPropertyValueEnum(UCHAR_SCRIPT, odd[i]));
        }
    }
    end();
    begin("u_errorName");
    {
        int e;
        for (e = -300; e < 0x10600; e++) {
            cases++;
            if (strcmp(uo_u_errorName((UErrorCode)e), u_errorName((UErrorCode)e)))
                differ("%d ours %s ICU %s", e, uo_u_errorName((UErrorCode)e), u_errorName((UErrorCode)e));
        }
    }
    end();
    begin("u_getVersion");
    {
        UVersionInfo a, b;
        uo_u_getVersion(a);
        u_getVersion(b);
        cases++;
        if (memcmp(a, b, sizeof a))
            differ("ours %d.%d.%d.%d", a[0], a[1], a[2], a[3]);
    }
    end();
    begin("u_countChar32");
    {
        static const UChar s[] = { 0x41, 0xD800, 0xDC00, 0xD800, 0x42, 0xDC00, 0xDBFF, 0 };
        int32_t n;
        for (n = -1; n <= 7; n++) {
            cases++;
            if (uo_u_countChar32(s, n) != u_countChar32(s, n))
                differ("length %d ours %d ICU %d", n, uo_u_countChar32(s, n), u_countChar32(s, n));
        }
    }
    end();
    strings("u_strToLower", uo_u_strToLower, u_strToLower, 1);
    strings("u_strToUpper", uo_u_strToUpper, u_strToUpper, 1);
    strings("u_strFoldCase", foldOurs, foldICU, 0);
    printf("%s\n", failures ? "HOST-PROPS FAILED" : "HOST-PROPS ALL PASSED");
    return failures ? 1 : 0;
}
