/* unicode.library, our own code: the host checks of text breaking.
 * Copyright (c) 2026 AmiCradle Team. MIT.
 *
 *   host_brk TESTDATA_DIR [ROUNDS]
 *
 * 1. Unicode's own break tests (GraphemeBreakTest.txt, WordBreakTest.txt,
 *    SentenceBreakTest.txt, LineBreakTest.txt in TESTDATA_DIR; ICU 78's
 *    source has them in test/testdata): every line, through next(),
 *    previous(), following(), preceding() and isBoundary(). Lines where
 *    ICU's rules differ from UAX #29 are not counted as failures here but
 *    by part 2, which checks them against ICU itself.
 * 2. With ICU (built with -DWITH_ICU): ROUNDS random strings per iterator
 *    type and line variant, made of characters of every break class, and
 *    every line of the test files, against ICU's ubrk_* calls: the same
 *    boundaries and the same rule status at each. Exits 1 on any difference.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <unicode/ubrk.h>
#include <unicode/ustring.h>
#include <unicode/utext.h>

#include "uo_brk_data.h"
#include "uo_own.h"

#ifdef WITH_ICU_DL
/* a local check against the system's ICU: its calls by their versioned names */
#include <dlfcn.h>
#define S2(a, b) #a "_" #b
#define S1(a, b) S2(a, b)
static void *icu_lib;
#define ICUF(name) ((__typeof__(&name))icu_sym(S1(name, WITH_ICU_DL)))
static void *icu_sym(const char *n)
{
    void *p;
    if (!icu_lib)
        icu_lib = dlopen(ICU_SO, RTLD_NOW);
    p = icu_lib ? dlsym(icu_lib, n) : NULL;
    if (!p) {
        fprintf(stderr, "no %s\n", n);
        exit(2);
    }
    return p;
}
#define WITH_ICU 1
#define I_open ICUF(ubrk_open)
#define I_close ICUF(ubrk_close)
#define I_next ICUF(ubrk_next)
#define I_first ICUF(ubrk_first)
#define I_status ICUF(ubrk_getRuleStatus)
#define I_title ICUF(u_strToTitle)
#elif defined(WITH_ICU)
#define I_open ubrk_open
#define I_close ubrk_close
#define I_next ubrk_next
#define I_first ubrk_first
#define I_status ubrk_getRuleStatus
#define I_title u_strToTitle
#endif

static long fails;
#ifdef WITH_ICU
static long checked;
#endif

static int to_utf16(const UChar32 *cp, int n, UChar *out, int *idx)
{
    int i, len = 0;
    for (i = 0; i < n; i++) {
        idx[i] = len;
        if (cp[i] >= 0x10000) {
            out[len++] = (UChar)(0xD7C0 + (cp[i] >> 10));
            out[len++] = (UChar)(0xDC00 | (cp[i] & 0x3FF));
        } else
            out[len++] = (UChar)cp[i];
    }
    idx[n] = len;
    return len;
}

static void show(const char *what, const UChar *s, int len)
{
    int i;
    fprintf(stderr, "%s:", what);
    for (i = 0; i < len; i++)
        fprintf(stderr, " %04X", s[i]);
    fprintf(stderr, "\n");
}

/* our boundaries by next(); then every other call agrees with them */
static int ours(UBreakIteratorType type, const char *loc, const UChar *s, int len, int *bnd, int *st)
{
    UErrorCode e = U_ZERO_ERROR;
    UBreakIterator *bi = uo_ubrk_open(type, loc, s, len, &e);
    int n = 0, p, i, j;
    char *isb = calloc(len + 2, 1);
    if (U_FAILURE(e)) {
        fprintf(stderr, "uo_ubrk_open: %d\n", e);
        exit(2);
    }
    bnd[n] = uo_ubrk_first(bi);
    st[n++] = uo_ubrk_getRuleStatus(bi);
    while ((p = uo_ubrk_next(bi)) != UBRK_DONE) {
        bnd[n] = p;
        st[n++] = uo_ubrk_getRuleStatus(bi);
    }
    for (i = 0; i < n; i++)
        isb[bnd[i]] = 1;
    /* backwards */
    j = n - 1;
    if (uo_ubrk_last(bi) != len) {
        show("last() != length", s, len);
        fails++;
    }
    while ((p = uo_ubrk_previous(bi)) != UBRK_DONE) {
        if (--j < 0 || p != bnd[j] || uo_ubrk_getRuleStatus(bi) != st[j]) {
            show("previous() differs from next()", s, len);
            fails++;
            break;
        }
    }
    /* following, preceding, isBoundary at every offset */
    for (i = -1; i <= len + 1; i++) {
        int f = uo_ubrk_following(bi, i), want = -2, k;
        for (k = 0; k < n; k++)
            if (bnd[k] > i) {
                want = bnd[k];
                break;
            }
        if (i < 0)
            want = 0;
        else if (want == -2)
            want = UBRK_DONE;
        if (i >= 0 && i < len && (s[i] & 0xFC00) == 0xDC00 && i > 0 && (s[i - 1] & 0xFC00) == 0xD800) {
            for (k = 0; k < n; k++)
                if (bnd[k] > i - 1) {
                    want = bnd[k];
                    break;
                }
        }
        if (f != want) {
            fprintf(stderr, "following(%d) = %d, want %d\n", i, f, want);
            show("text", s, len);
            fails++;
        }
        {
            int pr = uo_ubrk_preceding(bi, i), wp = UBRK_DONE, x = i;
            if (x > len)
                wp = len;
            else {
                if (x > 0 && x < len && (s[x] & 0xFC00) == 0xDC00 && (s[x - 1] & 0xFC00) == 0xD800)
                    x--;
                for (k = n - 1; k >= 0; k--)
                    if (bnd[k] < x) {
                        wp = bnd[k];
                        break;
                    }
            }
            if (pr != wp) {
                fprintf(stderr, "preceding(%d) = %d, want %d\n", i, pr, wp);
                show("text", s, len);
                fails++;
            }
        }
        if (i >= 0 && i <= len && (uo_ubrk_isBoundary(bi, i) != 0) != (isb[i] != 0)) {
            fprintf(stderr, "isBoundary(%d) wrong\n", i);
            show("text", s, len);
            fails++;
        }
    }
    {   /* a clone gives the same */
        UBreakIterator *c = uo_ubrk_clone(bi, &e);
        if (uo_ubrk_first(c) != 0 || (n > 1 && uo_ubrk_next(c) != bnd[1])) {
            show("clone differs", s, len);
            fails++;
        }
        uo_ubrk_close(c);
    }
    uo_ubrk_close(bi);
    free(isb);
    return n;
}

#ifdef WITH_ICU
static const char *tname[] = {"char", "word", "line", "sent", "title"};

static int against_icu(UBreakIteratorType type, const char *loc, const UChar *s, int len)
{
    static int b1[16384], s1[16384], b2[16384], s2[16384];
    UErrorCode e = U_ZERO_ERROR;
    UBreakIterator *bi = I_open(type, loc, s, len, &e);
    int n1, n2 = 0, p, i;
    if (U_FAILURE(e)) {
        fprintf(stderr, "ICU's ubrk_open: %d\n", e);
        exit(2);
    }
    n1 = ours(type, loc, s, len, b1, s1);
    b2[n2] = I_first(bi);
    s2[n2++] = I_status(bi);
    while ((p = I_next(bi)) != UBRK_DONE && n2 < 16384) {
        b2[n2] = p;
        s2[n2++] = I_status(bi);
    }
    I_close(bi);
    checked++;
    if (n1 == n2) {
        for (i = 0; i < n1; i++)
            if (b1[i] != b2[i] || (type != UBRK_CHARACTER && s1[i] != s2[i]))
                break;
        if (i == n1)
            return 0;
    }
    if (fails < (getenv("MAXSHOW") ? atol(getenv("MAXSHOW")) : 40)) {
        fprintf(stderr, "%s %s:", tname[type], loc);
        for (i = 0; i < len; i++)
            fprintf(stderr, " %04X", s[i]);
        fprintf(stderr, "\n  ours:");
        for (i = 0; i < n1; i++)
            fprintf(stderr, " %d<%d>", b1[i], s1[i]);
        fprintf(stderr, "\n  ICU: ");
        for (i = 0; i < n2; i++)
            fprintf(stderr, " %d<%d>", b2[i], s2[i]);
        fprintf(stderr, "\n");
    }
    fails++;
    return 1;
}
#endif

/* A UText over UTF-16, read in chunks of 7: native indexes are the UTF-16
 * offsets, or (wide) twice them, as a provider of 2-byte units would give */
static int wideText;

static int64_t tx_length(UText *ut) { return (int64_t)ut->a * (wideText ? 2 : 1); }

static UBool tx_access(UText *ut, int64_t native, UBool forward)
{
    int64_t i = wideText ? native / 2 : native, start;
    if (!forward && i > 0)
        i--;
    if (i < 0 || i >= ut->a)
        return 0;
    start = i - i % 7;
    ut->chunkContents = (const UChar *)ut->context + start;
    ut->chunkLength = (int32_t)(ut->a - start < 7 ? ut->a - start : 7);
    ut->chunkNativeStart = start * (wideText ? 2 : 1);
    ut->chunkNativeLimit = (start + ut->chunkLength) * (wideText ? 2 : 1);
    ut->nativeIndexingLimit = wideText ? 0 : ut->chunkLength;
    ut->chunkOffset = (int32_t)(i - start);
    return 1;
}

static int64_t tx_toNative(const UText *ut) { return ut->chunkNativeStart + ut->chunkOffset * 2; }
static int32_t tx_toUTF16(const UText *ut, int64_t n) { return (int32_t)((n - ut->chunkNativeStart) / 2); }

static const struct UTextFuncs tx_funcs = {
    sizeof(UTextFuncs), 0, 0, 0, NULL, tx_length, tx_access, NULL, NULL, NULL,
    tx_toNative, tx_toUTF16, NULL, NULL, NULL, NULL
};

/* boundaries through ubrk_setUText, against setText's */
static void utext_check(UBreakIteratorType type, const UChar *s, int len)
{
    int w;
    for (w = 0; w < 2; w++) {
        UErrorCode e = U_ZERO_ERROR;
        UText *ut = uo_utext_setup(NULL, 16, &e);
        UBreakIterator *a = uo_ubrk_open(type, "en", s, len, &e), *b = uo_ubrk_open(type, "en", NULL, 0, &e);
        int p, q, k = w ? 2 : 1;
        wideText = w;
        ut->pFuncs = &tx_funcs;
        ut->context = s;
        ut->a = len;
        uo_ubrk_setUText(b, ut, &e);
        if (U_FAILURE(e)) {
            fprintf(stderr, "setUText: %d\n", e);
            fails++;
        }
        do {
            p = uo_ubrk_next(a);
            q = uo_ubrk_next(b);
            if ((p == UBRK_DONE ? p : p * k) != q) {
                show(w ? "setUText (wide) differs" : "setUText differs", s, len);
                fails++;
                break;
            }
        } while (p != UBRK_DONE);
        if (len > 2 && uo_ubrk_following(b, k) != uo_ubrk_following(a, 1) * k) {
            show("setUText following differs", s, len);
            fails++;
        }
        uo_ubrk_close(a);
        uo_ubrk_close(b);
        if (uo_utext_close(ut) != NULL) {
            fprintf(stderr, "utext_close kept a heap UText\n");
            fails++;
        }
    }
}

/* Unicode's test file: ÷ and × between code points */
static void test_file(const char *dir, const char *name, UBreakIteratorType type)
{
    char path[1024];
    static char line[65536];
    FILE *f;
    long lines = 0, differ = 0;
    snprintf(path, sizeof path, "%s/%s", dir, name);
    if (!(f = fopen(path, "r"))) {
        fprintf(stderr, "no %s\n", path);
        exit(2);
    }
    while (fgets(line, sizeof line, f)) {
        static UChar32 cp[8192];
        static int brk[8193], idx[8193], want[8193], got[16384], gs[16384];
        static UChar s[16384];
        int n = 0, len, nb, i, k, nw = 0;
        char *p = line, *hash = strchr(line, '#');
        if (hash)
            *hash = 0;
        while (*p) {
            if (!strncmp(p, "\xc3\xb7", 2)) {           /* ÷ */
                brk[n] = 1;
                p += 2;
            } else if (!strncmp(p, "\xc3\x97", 2)) {    /* × */
                brk[n] = 0;
                p += 2;
            } else if ((*p >= '0' && *p <= '9') || (*p >= 'A' && *p <= 'F')) {
                cp[n++] = (UChar32)strtol(p, &p, 16);
            } else
                p++;
        }
        if (!n || (!strstr(line, "\xc3\xb7") && !strstr(line, "\xc3\x97")))
            continue;
        lines++;
        len = to_utf16(cp, n, s, idx);
        for (i = 0; i <= n; i++)
            if (brk[i] || i == 0)       /* the start is a boundary, as ICU gives it */
                want[nw++] = idx[i];
        nb = ours(type, "en", s, len, got, gs);
        utext_check(type, s, len);
        for (k = 0; k < nb && k < nw; k++)
            if (got[k] != want[k])
                break;
        if (nb != nw || k < nb) {
            differ++;
            if (getenv("SHOW"))
                show(name, s, len);
        }
#ifdef WITH_ICU
        if (!getenv("STABLE"))
            against_icu(type, "en", s, len);
        if (!getenv("STABLE") && type == UBRK_LINE) {
            against_icu(type, "en@lb=normal", s, len);
            against_icu(type, "en@lb=loose", s, len);
        }
#endif
#if 0
        if (type == UBRK_LINE) {
            against_icu(type, "en@lb=normal", s, len);
            against_icu(type, "en@lb=loose", s, len);
        }
#endif
    }
    fclose(f);
    printf("%s: %ld lines, %ld differ from UAX's expectation\n", name, lines, differ);
}

#ifdef WITH_ICU
static uint32_t rnd = 12345;
static uint32_t next_rand(void)
{
    rnd ^= rnd << 13;
    rnd ^= rnd >> 17;
    rnd ^= rnd << 5;
    return rnd;
}

/* code points of every distinct break record, a few of each */
static UChar32 *pal;
static int npal;

static void make_palette(void)
{
    static const UChar32 extra[] = {0x20, 0x61, 0x41, 0x2E, 0x2C, 0x27, 0x22, 0x3A, 0x31, 0x5F, 0x28, 0x29,
                                    0x2D, 0x2010, 0x25CC, 0x3005, 0x303B, 0x309D, 0x309E, 0x30FD, 0x30FE,
                                    0x0D, 0x0A, 0x2028, 0x200D, 0x200B, 0x2060, 0xA0, 0xAC00, 0x4E00,
                                    0x3042, 0x30A2, 0x1F1E6, 0x1F1E7, 0x1F600, 0x1F3FB, 0x0E01, 0x0E31,
                                    0xAB, 0xBB, 0x2018, 0x2019, 0x201C, 0x201D, 0x3002, 0x300C, 0xFF08,
                                    0x0915, 0x094D, 0x0924, 0x1A00, 0x05D0, 0x05F3, 0x05F4, 0x0308,
                                    0x1100, 0x1161, 0x11A8, 0xAC01, 0x24, 0x25, 0xFF0E, 0x1F46A};
    static int seen[0x10000];
    static char ok[0x110000];
    UChar32 c;
    int i;
    FILE *st = getenv("STABLE") ? fopen(getenv("STABLE"), "r") : NULL;
    /* STABLE: a list of the code points to use (hex, one a line), for a
     * check against an ICU of another Unicode version */
    if (st) {
        unsigned x;
        while (fscanf(st, "%x", &x) == 1)
            if (x < 0x110000)
                ok[x] = 1;
        fclose(st);
    } else
        memset(ok, 1, sizeof ok);
    pal = malloc(sizeof(UChar32) * 40000);
    for (i = 0; i < (int)(sizeof extra / sizeof extra[0]); i++)
        if (!st || ok[extra[i]])
            pal[npal++] = extra[i];
    for (c = 0; c < 0x110000; c++) {
        const struct uo_brk *r = UO_BRK(c);
        int key = (int)(r - uo_brks);
        if ((c >= 0xD800 && c < 0xE000) || !ok[c])
            continue;
        if (seen[key] < 3 && (seen[key] == 0 || (next_rand() & 63) == 0)) {
            seen[key]++;
            pal[npal++] = c;
        }
    }
}

static void random_strings(long rounds)
{
    static const UBreakIteratorType types[] = {UBRK_CHARACTER, UBRK_WORD, UBRK_LINE, UBRK_SENTENCE};
    static const char *const lineLoc[] = {"en", "en@lb=normal", "en@lb=loose"};
    long r;
    int t;
    for (t = 0; t < 6; t++) {
        UBreakIteratorType type = t < 4 ? types[t] : UBRK_LINE;
        const char *loc = t < 2 || t == 3 ? "en" : lineLoc[t == 2 ? 0 : t - 3];
        long before = fails;
        for (r = 0; r < rounds; r++) {
            UChar32 cp[24];
            UChar s[64];
            int idx[25], n = 1 + next_rand() % 12, i;
            for (i = 0; i < n; i++)
                cp[i] = pal[next_rand() % npal];
            against_icu(type, loc, s, to_utf16(cp, n, s, idx));
        }
        printf("random %s %s: %ld strings, %ld differ from ICU\n", tname[type], loc, rounds, fails - before);
    }
}

/* u_strToTitle: the same text, length and error as ICU's, in four locales,
 * into a big buffer and into one too small */
static void random_titles(long rounds)
{
    static const char *const locs[] = {"en", "nl", "tr", "lt", "el"};
    long r, before = fails;
    for (r = 0; r < rounds; r++) {
        UChar32 cp[24];
        UChar s[64], a[200], b[200];
        int idx[25], n = 1 + next_rand() % 12, i, len, cap;
        const char *loc = locs[next_rand() % 5];
        UErrorCode e1 = U_ZERO_ERROR, e2 = U_ZERO_ERROR;
        int32_t l1, l2;
        for (i = 0; i < n; i++) {
            uint32_t k = next_rand() % 16;
            /* letters that case-map specially, often */
            static const UChar32 sp[] = {0x69, 0x49, 0x6A, 0x4A, 0xED, 0xCD, 0x301, 0x307, 0x3A3, 0x3C3,
                                         0xDF, 0x130, 0x131, 0x20, 0x2E, 0x27, 0x1F0, 0xFB00, 0x1C5};
            cp[i] = k < 6 ? sp[next_rand() % (sizeof sp / sizeof sp[0])] : pal[next_rand() % npal];
        }
        len = to_utf16(cp, n, s, idx);
        cap = (next_rand() & 3) ? 200 : (int)(next_rand() % (len + 2));
        l1 = uo_u_strToTitle(a, cap, s, len, NULL, loc, &e1);
        l2 = I_title(b, cap, s, len, NULL, loc, &e2);
        checked++;
        /* ICU's warnings (U_USING_DEFAULT_WARNING for a locale without
         * break data of its own) are not compared */
        if (e1 < U_ZERO_ERROR)
            e1 = U_ZERO_ERROR;
        if (e2 < U_ZERO_ERROR)
            e2 = U_ZERO_ERROR;
        if (l1 != l2 || e1 != e2 || (U_SUCCESS(e1) && memcmp(a, b, sizeof(UChar) * (l1 < cap ? l1 : cap)))) {
            if (fails - before < 40) {
                show(loc, s, len);
                fprintf(stderr, "  ours %d e%d:", l1, e1);
                for (i = 0; i < l1 && i < cap; i++)
                    fprintf(stderr, " %04X", a[i]);
                fprintf(stderr, "\n  ICU  %d e%d:", l2, e2);
                for (i = 0; i < l2 && i < cap; i++)
                    fprintf(stderr, " %04X", b[i]);
                fprintf(stderr, "\n");
            }
            fails++;
        }
    }
    printf("random u_strToTitle: %ld strings, %ld differ from ICU\n", rounds, fails - before);
}
#endif

int main(int argc, char **argv)
{
    long rounds = argc > 2 ? atol(argv[2]) : 200000;
    if (argc < 2) {
        fprintf(stderr, "usage: host_brk TESTDATA_DIR [ROUNDS]\n");
        return 2;
    }
    test_file(argv[1], "GraphemeBreakTest.txt", UBRK_CHARACTER);
    test_file(argv[1], "WordBreakTest.txt", UBRK_WORD);
    test_file(argv[1], "SentenceBreakTest.txt", UBRK_SENTENCE);
    test_file(argv[1], "LineBreakTest.txt", UBRK_LINE);
#ifdef WITH_ICU
    make_palette();
    random_strings(rounds);
    random_titles(rounds);
    printf("host_brk: %ld strings against ICU, %ld differ\n", checked, fails);
#else
    (void)rounds;
    printf("host_brk: %ld failures in our own calls' agreement\n", fails);
#endif
    return fails ? 1 : 0;
}
