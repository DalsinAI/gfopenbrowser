/* UnicodeTest: unicode.library's self-test on the Amiga.
 * Copyright (c) 2026 AmigaChrome Team. MIT.
 *
 * Linked as GFOpenBrowser's engine is in library mode: ICU's headers, and
 * ul_stubs.s and ul_client.c in place of ICU's libraries, so every ICU
 * call below goes through the library's table.
 *
 *   UnicodeTest            the checks, then the timings
 *   UnicodeTest TASKS      also two more tasks breaking text at once
 *
 * Prints PASS or FAIL per check, times in seconds, and returns 0 when
 * everything passed, 10 otherwise.
 */
#include <stdio.h>
#include <string.h>
#include <time.h>

#include <exec/types.h>
#include <exec/tasks.h>
#include <dos/dos.h>
#include <dos/dostags.h>
#include <proto/exec.h>
#include <proto/dos.h>

#include <unicode/ubrk.h>
#include <unicode/ucal.h>
#include <unicode/ucnv.h>
#include <unicode/ucnv_err.h>
#include <unicode/ucol.h>
#include <unicode/udat.h>
#include <unicode/uidna.h>
#include <unicode/uloc.h>
#include <unicode/unorm2.h>
#include <unicode/unumberformatter.h>
#include <unicode/ustring.h>
#include <unicode/utypes.h>

#include "ul_client.h"

static int failures;

static void check(const char *what, int ok, const char *got)
{
    printf("%s %s%s%s\n", ok ? "PASS" : "FAIL", what, got ? ": " : "", got ? got : "");
    if (!ok)
        failures++;
}

/* Seconds since an earlier DateStamp, to 1/50 s. */
static double since(const struct DateStamp *t0)
{
    struct DateStamp t;
    DateStamp(&t);
    return (t.ds_Days - t0->ds_Days) * 86400.0 + (t.ds_Minute - t0->ds_Minute) * 60.0
         + (t.ds_Tick - t0->ds_Tick) / (double)TICKS_PER_SECOND;
}

static const char *utf8(const UChar *s, int32_t len)
{
    static char buf[256];
    UErrorCode e = U_ZERO_ERROR;
    u_strToUTF8(buf, sizeof buf, NULL, s, len, &e);
    return U_SUCCESS(e) ? buf : u_errorName(e);
}

static int32_t uni(UChar *dst, int32_t cap, const char *s)
{
    UErrorCode e = U_ZERO_ERROR;
    int32_t n = 0;
    u_strFromUTF8(dst, cap, &n, s, -1, &e);
    return U_SUCCESS(e) ? n : -1;
}

static const char paragraph[] =
    "Unicode text breaking finds where lines may wrap, where words start and end, "
    "and how characters cluster: na\xc3\xafve caf\xc3\xa9s, \xe2\x80\x9cquoted\xe2\x80\x9d words, "
    "1,234.56 numbers, e-mail@example.com, and hyphen-ated compounds all count.";

/* Line breaks in the paragraph, `rounds` times; returns the count of one round. */
static int breakLines(int rounds)
{
    UChar text[512];
    int32_t len = uni(text, 512, paragraph);
    int count = 0, r;

    for (r = 0; r < rounds; r++) {
        UErrorCode e = U_ZERO_ERROR;
        UBreakIterator *bi = ubrk_open(UBRK_LINE, "en", text, len, &e);
        if (U_FAILURE(e))
            return -1;
        count = 0;
        while (ubrk_next(bi) != UBRK_DONE)
            count++;
        ubrk_close(bi);
    }
    return count;
}

/* ---- Other tasks at once ------------------------------------------------- */

static volatile int taskResult[2];

static void breakerTask(void)
{
    struct Process *me = (struct Process *)FindTask(NULL);
    int slot = (int)me->pr_Task.tc_UserData;
    int c = breakLines(200);
    Forbid();
    taskResult[slot] = c;
    Permit();
}

static int runTasks(int expect)
{
    int i, ok = 1;
    struct DateStamp t0;

    taskResult[0] = taskResult[1] = 0;
    DateStamp(&t0);
    for (i = 0; i < 2; i++) {
        struct Process *p = CreateNewProcTags(NP_Entry, (ULONG)breakerTask, NP_Name, (ULONG)"UnicodeTest breaker",
                                              NP_StackSize, 32768, NP_Output, 0, NP_CloseOutput, FALSE, TAG_DONE);
        if (!p)
            return 0;
        p->pr_Task.tc_UserData = (APTR)i;
    }
    /* the main task breaks too while they run */
    if (breakLines(200) != expect)
        ok = 0;
    for (i = 0; i < 6000 && (!taskResult[0] || !taskResult[1]); i++)
        Delay(1);
    printf("TIME three tasks x 200 line-break passes: %.2f s\n", since(&t0));
    return ok && taskResult[0] == expect && taskResult[1] == expect;
}

int main(int argc, char **argv)
{
    struct DateStamp t0;
    UErrorCode e;
    UChar a[256], b[256];
    char buf[256];
    int32_t n;
    int code, lines;

    DateStamp(&t0);
    code = ul_client_open();
    check("open unicode.library and its table", code == UL_OPEN_OK, code ? ul_client_error(code) : NULL);
    if (code != UL_OPEN_OK)
        return 20;
    printf("TIME open (first open reads the data): %.2f s\n", since(&t0));

    {
        UVersionInfo v;
        char s[24];
        u_getVersion(v);
        snprintf(s, sizeof s, "%d.%d.%d", v[0], v[1], v[2]);
        check("ICU version 78", v[0] == 78, s);
    }

    /* NFC: e + combining acute -> e-acute */
    e = U_ZERO_ERROR;
    n = uni(a, 256, "e\xcc\x81");
    n = unorm2_normalize(unorm2_getNFCInstance(&e), a, n, b, 256, &e);
    check("NFC composes e + U+0301", U_SUCCESS(e) && n == 1 && b[0] == 0xe9, utf8(b, n));

    /* case mapping with expansion */
    e = U_ZERO_ERROR;
    n = uni(a, 256, "stra\xc3\x9f" "e");
    n = u_strToUpper(b, 256, a, n, "en", &e);
    check("upper case of strasse with sharp s", U_SUCCESS(e) && !strcmp(utf8(b, n), "STRASSE"), utf8(b, n));

    /* a converter calling back through the program's stub, as WebCore's
       TextCodecICU does: U+00E9 to US-ASCII as an XML escape */
    {
        UConverter *cnv;
        char out[64], *o = out;
        const UChar *in = a;
        e = U_ZERO_ERROR;
        cnv = ucnv_open("US-ASCII", &e);
        if (U_SUCCESS(e))
            ucnv_setFromUCallBack(cnv, UCNV_FROM_U_CALLBACK_ESCAPE, UCNV_ESCAPE_XML_DEC, NULL, NULL, &e);
        n = uni(a, 256, "caf\xc3\xa9");
        if (U_SUCCESS(e))
            ucnv_fromUnicode(cnv, &o, out + sizeof out - 1, &in, a + n, NULL, TRUE, &e);
        *o = 0;
        check("converter escape callback", U_SUCCESS(e) && !strcmp(out, "caf&#233;"), U_SUCCESS(e) ? out : u_errorName(e));
        if (cnv)
            ucnv_close(cnv);
    }

    /* IDNA (UTS 46): the URL parser's call */
    {
        UIDNAInfo info = UIDNA_INFO_INITIALIZER;
        UIDNA *idna;
        e = U_ZERO_ERROR;
        idna = uidna_openUTS46(UIDNA_CHECK_BIDI | UIDNA_NONTRANSITIONAL_TO_ASCII, &e);
        n = uni(a, 256, "b\xc3\xbc" "cher.example");
        n = U_SUCCESS(e) ? uidna_nameToASCII(idna, a, n, b, 256, &info, &e) : 0;
        check("IDNA to ASCII", U_SUCCESS(e) && !info.errors && !strcmp(utf8(b, n), "xn--bcher-kva.example"), utf8(b, n));
        if (idna)
            uidna_close(idna);
    }

    /* breaking */
    lines = breakLines(1);
    snprintf(buf, sizeof buf, "%d break opportunities", lines);
    check("line breaking", lines > 30 && lines < 60, buf);
    {
        UBreakIterator *bi;
        int words = 0;
        e = U_ZERO_ERROR;
        n = uni(a, 256, "Hello, world! It's 3.14.");
        bi = ubrk_open(UBRK_WORD, "en", a, n, &e);
        if (U_SUCCESS(e)) {
            ubrk_first(bi);
            while (ubrk_next(bi) != UBRK_DONE)
                if (ubrk_getRuleStatus(bi) != UBRK_WORD_NONE)
                    words++;
            ubrk_close(bi);
        }
        snprintf(buf, sizeof buf, "%d words", words);
        check("word breaking", U_SUCCESS(e) && words == 4, buf);
    }

    /* collation */
    {
        UCollator *c;
        e = U_ZERO_ERROR;
        c = ucol_open("en", &e);
        if (U_SUCCESS(e)) {
            UChar x[8], y[8];
            int32_t nx = uni(x, 8, "apple"), ny = uni(y, 8, "Banana");
            UCollationResult r = ucol_strcoll(c, x, nx, y, ny);
            check("collation: apple before Banana", r == UCOL_LESS, NULL);
            ucol_close(c);
        } else {
            check("collation", 0, u_errorName(e));
        }
    }

    /* number and date formatting (JavaScript's Intl) */
    {
        UNumberFormatter *f;
        UFormattedNumber *r;
        e = U_ZERO_ERROR;
        n = uni(a, 64, "precision-unlimited");
        f = unumf_openForSkeletonAndLocale(a, n, "en", &e);
        r = unumf_openResult(&e);
        unumf_formatDouble(f, 1234567.891, r, &e);
        n = unumf_resultToString(r, b, 256, &e);
        check("number format 1234567.891", U_SUCCESS(e) && !strcmp(utf8(b, n), "1,234,567.891"), U_SUCCESS(e) ? utf8(b, n) : u_errorName(e));
        if (r)
            unumf_closeResult(r);
        if (f)
            unumf_close(f);
    }
    {
        UDateFormat *df;
        UChar zone[] = { 'U', 'T', 'C', 0 };
        UChar pattern[32];
        int32_t np = uni(pattern, 32, "yyyy-MM-dd HH:mm");
        e = U_ZERO_ERROR;
        df = udat_open(UDAT_PATTERN, UDAT_PATTERN, "en", zone, 3, pattern, np, &e);
        /* 23 October 2026 03:00 UTC */
        n = U_SUCCESS(e) ? udat_format(df, 1792724400000.0, b, 256, NULL, &e) : 0;
        check("date format", U_SUCCESS(e) && !strcmp(utf8(b, n), "2026-10-23 03:00"), U_SUCCESS(e) ? utf8(b, n) : u_errorName(e));
        if (df)
            udat_close(df);
    }
    e = U_ZERO_ERROR;
    n = ucal_getHostTimeZone(b, 256, &e);
    check("host time zone", U_SUCCESS(e) && n > 0, U_SUCCESS(e) ? utf8(b, n) : u_errorName(e));
    printf("INFO default locale %s\n", uloc_getDefault());

    /* timings */
    DateStamp(&t0);
    breakLines(500);
    printf("TIME 500 line-break passes over %d characters: %.2f s\n", (int)strlen(paragraph), since(&t0));
    {
        const UNormalizer2 *nfc;
        int i;
        e = U_ZERO_ERROR;
        nfc = unorm2_getNFCInstance(&e);
        n = uni(a, 256, paragraph);
        DateStamp(&t0);
        for (i = 0; i < 2000; i++)
            unorm2_normalize(nfc, a, n, b, 256, &e);
        printf("TIME 2000 NFC passes: %.2f s\n", since(&t0));
    }

    if (argc > 1 && !strcmp(argv[1], "TASKS"))
        check("three tasks breaking at once", runTasks(lines), NULL);

    printf("%s: %d failed\n", failures ? "FAILED" : "ALL PASSED", failures);
    return failures ? 10 : 0;
}
