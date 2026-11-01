/* UnicodeProps: checksums of unicode.library's property and case calls
 * over every code point, to compare with ICU 78's on the host.
 * Copyright (c) 2026 AmiCradle Team. MIT.
 *
 * The same source is built twice: as an Amiga program on the library
 * (ICU's names, through the stubs, so our own code answers), and on the
 * host against ICU 78.3 itself (src/unicodelib/own/tests/run-host.sh),
 * which gives the expected lines. Every group prints
 *
 *   UPROPS <group> <checksum> <calls>
 *
 * and the end "UPROPS_END <seconds>". Same lines, same answers.
 */
#include <stdio.h>
#include <string.h>
#include <time.h>

#include <unicode/uchar.h>
#include <unicode/uscript.h>
#include <unicode/ustring.h>

static unsigned long hash, calls;

static void mix(long v)
{
    int i;
    for (i = 0; i < 4; i++) {
        hash ^= (unsigned long)(v >> (8 * i)) & 0xFF;
        hash = (hash * 16777619UL) & 0xFFFFFFFFUL;
    }
    calls++;
}

static void group(const char *name)
{
    if (name) {
        hash = 2166136261UL;
        calls = 0;
        return;
    }
}

static void done(const char *name)
{
    printf("UPROPS %s %08lx %lu\n", name, hash, calls);
    fflush(stdout);
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

static void mixString(int32_t r, UErrorCode e, const UChar *d)
{
    int32_t i;
    mix(r);
    mix(e);
    if (U_SUCCESS(e))
        for (i = 0; i < r; i++)
            mix(d[i]);
}

static const char *const locales[] = { "", "tr", "az", "lt", "nl", "en_US", 0 };
static const UChar32 context[] = { 0x41, 0x61, 0x49, 0x69, 0x4A, 0x12E, 0xCC, 0x130, 0x131, 0x307, 0x300,
                                   0x323, 0x345, 0x3A3, 0x3C3, 0x391, 0x27, 0xAD, 0x20, 0xDF, 0x1F80, 0x10400, 0 };

int main(void)
{
    static const UProperty ints[] = { UCHAR_BIDI_CLASS, UCHAR_BLOCK, UCHAR_CANONICAL_COMBINING_CLASS,
        UCHAR_EAST_ASIAN_WIDTH, UCHAR_GENERAL_CATEGORY, UCHAR_LINE_BREAK, UCHAR_NUMERIC_TYPE, UCHAR_SCRIPT,
        UCHAR_VERTICAL_ORIENTATION, UCHAR_GENERAL_CATEGORY_MASK };
    static const UScriptCode scripts[] = { USCRIPT_COMMON, USCRIPT_INHERITED, USCRIPT_LATIN, USCRIPT_GREEK,
        USCRIPT_CYRILLIC, USCRIPT_ARABIC, USCRIPT_HEBREW, USCRIPT_HAN, USCRIPT_HIRAGANA, USCRIPT_KATAKANA,
        USCRIPT_HANGUL, USCRIPT_DEVANAGARI, USCRIPT_BENGALI, USCRIPT_THAI, USCRIPT_SYRIAC, USCRIPT_THAANA,
        USCRIPT_COPTIC, USCRIPT_GEORGIAN, USCRIPT_UNKNOWN, USCRIPT_BOPOMOFO };
    clock_t t0 = clock();
    UChar32 c;
    unsigned i;
    int p, l;

    printf("UnicodeProps: ICU %s interface, Unicode %s\n", U_ICU_VERSION, U_UNICODE_VERSION);
    group("chars");
    for (c = -2; c <= 0x110001; c++) {
        UErrorCode e = U_ZERO_ERROR;
        mix(u_charType(c));
        mix(u_charDirection(c));
        mix(u_getCombiningClass(c));
        mix(u_charMirror(c));
        mix(u_isalpha(c) | u_isalnum(c) << 1 | u_isdigit(c) << 2 | u_ispunct(c) << 3 | u_isprint(c) << 4 |
            u_isUWhiteSpace(c) << 5);
        mix(ublock_getCode(c));
        mix(uscript_getScript(c, &e));
        mix(e);
    }
    done("chars");
    group("case");
    for (c = -2; c <= 0x110001; c++) {
        mix(u_tolower(c));
        mix(u_toupper(c));
        mix(u_totitle(c));
        mix(u_foldCase(c, U_FOLD_CASE_DEFAULT));
        mix(u_foldCase(c, U_FOLD_CASE_EXCLUDE_SPECIAL_I));
    }
    done("case");
    group("binary");
    for (p = UCHAR_BINARY_START; p < UCHAR_BINARY_LIMIT; p++)
        for (c = -2; c <= 0x110001; c++)
            mix(u_hasBinaryProperty(c, (UProperty)p));
    done("binary");
    group("int");
    for (i = 0; i < sizeof ints / sizeof ints[0]; i++)
        for (c = -2; c <= 0x110001; c++)
            mix(u_getIntPropertyValue(c, ints[i]));
    done("int");
    group("scripts");
    for (i = 0; i < sizeof scripts / sizeof scripts[0]; i++)
        for (c = 0; c < 0x110000; c++)
            mix(uscript_hasScript(c, scripts[i]));
    for (p = -1; p <= USCRIPT_CODE_LIMIT; p++) {
        const char *n = uscript_getShortName((UScriptCode)p), *q;
        char lower[8];
        mix(uscript_isRightToLeft((UScriptCode)p));
        if (n) {
            mix(u_getPropertyValueEnum(UCHAR_SCRIPT, n));
            for (q = n, i = 0; *q && i < 7; q++, i++)
                lower[i] = (*q >= 'A' && *q <= 'Z') ? *q + 32 : *q;
            lower[i] = 0;
            mix(u_getPropertyValueEnum(UCHAR_SCRIPT, lower));
        }
        while (n && *n)
            mix(*n++);
    }
    done("scripts");
    group("strings");
    for (l = 0; locales[l]; l++) {
        UChar s[8], d[16];
        int j, k, nctx = 0;
        while (context[nctx])
            nctx++;
        for (c = 1; c < 0x110000; c++) {
            UErrorCode e = U_ZERO_ERROR;
            int32_t n = put32(s, 0, c), r;
            if (u_tolower(c) == c && u_toupper(c) == c && u_foldCase(c, 0) == c && c != 0x130 && c != 0x149)
                continue;
            r = u_strToLower(d, 16, s, n, locales[l], &e);
            mixString(r, e, d);
            e = U_ZERO_ERROR;
            r = u_strToUpper(d, 16, s, n, locales[l], &e);
            mixString(r, e, d);
            if (l < 2) {
                e = U_ZERO_ERROR;
                r = u_strFoldCase(d, 16, s, n, l ? U_FOLD_CASE_EXCLUDE_SPECIAL_I : 0, &e);
                mixString(r, e, d);
            }
        }
        for (i = 0; i < (unsigned)nctx; i++)
            for (j = 0; j < nctx; j++)
                for (k = -1; k < nctx; k++) {
                    UErrorCode e = U_ZERO_ERROR;
                    int32_t n = put32(s, 0, context[i]), r;
                    n = put32(s, n, context[j]);
                    if (k >= 0)
                        n = put32(s, n, context[k]);
                    r = u_strToLower(d, 16, s, n, locales[l], &e);
                    mixString(r, e, d);
                    e = U_ZERO_ERROR;
                    r = u_strToUpper(d, 16, s, n, locales[l], &e);
                    mixString(r, e, d);
                    e = U_ZERO_ERROR;
                    r = u_strToLower(d, 1, s, n, locales[l], &e);   /* overflow */
                    mix(r);
                    mix(e);
                }
    }
    done("strings");
    group("misc");
    for (p = -200; p < 0x10600; p += 1) {
        const char *n = u_errorName((UErrorCode)p);
        while (*n)
            mix(*n++);
    }
    {
        static const UChar s[] = { 0x41, 0xD800, 0xDC00, 0xD800, 0x42, 0xDC00, 0xDBFF, 0 };
        for (p = -1; p <= 7; p++)
            mix(u_countChar32(s, p));
    }
    done("misc");
    printf("UPROPS_END %.2f\n", (double)(clock() - t0) / CLOCKS_PER_SEC);
    return 0;
}
