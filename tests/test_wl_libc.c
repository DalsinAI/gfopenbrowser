/* weblayout's own C library calls, its UTF-8 to Latin-1 converter and its
 * allocator, against the host's C library: tests/run-host-tests.sh.
 * MIT, Copyright (c) 2026 AmiCradle Team. */
#include "wl_libc.h"
#include "wl_mem.h"
#include "wl_text.h"

#include <ctype.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

static int failed, checks;

#define CHECK(cond) do { checks++; if (!(cond)) { printf("WLLIBC FAIL line %d: %s\n", __LINE__, #cond); failed = 1; } } while (0)

static void check_printf(const char *want, const char *fmt, ...)
{
    char got[64], small[5];
    va_list ap;
    int n;
    va_start(ap, fmt);
    n = wl_vsnprintf(got, sizeof got, fmt, ap);
    va_end(ap);
    checks++;
    if (strcmp(got, want) || n != (int)strlen(want)) {
        printf("WLLIBC FAIL printf \"%s\": got \"%s\" (%d), want \"%s\"\n", fmt, got, n, want);
        failed = 1;
    }
    va_start(ap, fmt);
    n = wl_vsnprintf(small, sizeof small, fmt, ap);   /* cut, terminated, whole length */
    va_end(ap);
    checks++;
    if (strncmp(small, want, sizeof small - 1) || strlen(small) > sizeof small - 1 || n != (int)strlen(want)) {
        printf("WLLIBC FAIL cut printf \"%s\": got \"%s\" (%d)\n", fmt, small, n);
        failed = 1;
    }
}

static void check_strtol(const char *s, int base)
{
    char *e1, *e2;
    long a = wl_strtol(s, &e1, base);
    long b = strtol(s, &e2, base);
    checks++;
    if (a != b || e1 != e2) {
        printf("WLLIBC FAIL strtol(\"%s\", %d): %ld/%d, libc %ld/%d\n", s, base, a, (int)(e1 - s), b, (int)(e2 - s));
        failed = 1;
    }
}

static void check_latin1(const char *utf8, const char *want)
{
    char *got = wl_utf8_to_latin1(NULL, utf8, strlen(utf8));
    checks++;
    if (!got || strcmp(got, want)) {
        printf("WLLIBC FAIL latin1 \"%s\": got \"%s\"\n", utf8, got ? got : "(null)");
        failed = 1;
    }
    wl_free(got);
}

int main(void)
{
    int c;
    wl_pool *pool;
    char *p, *q;
    char big[400];

    for (c = 0; c < 128; c++) {
        CHECK(!!wl_isspace(c) == !!isspace(c));
        CHECK(!!wl_isdigit(c) == !!isdigit(c));
        CHECK(!!wl_isalpha(c) == !!isalpha(c));
        CHECK(!!wl_isalnum(c) == !!isalnum(c));
        CHECK(!!wl_isxdigit(c) == !!isxdigit(c));
        CHECK(wl_tolower(c) == tolower(c));
    }
    CHECK(!wl_isspace(0xa0) && !wl_isalpha(0xe9));     /* ASCII only */

    CHECK(wl_strcasecmp("Color", "cOLOR") == 0);
    CHECK(wl_strcasecmp("a", "B") < 0 && wl_strcasecmp("b", "A") > 0);
    CHECK(wl_strcasecmp("abc", "ab") > 0 && wl_strcasecmp("", "") == 0);
    CHECK(wl_strncasecmp("HTTP/1.1", "http/9", 5) == 0);
    CHECK(wl_strncasecmp("abc", "abd", 3) < 0 && wl_strncasecmp("x", "y", 0) == 0);

    check_strtol("42", 10);
    check_strtol("  -17px", 10);
    check_strtol("+8", 10);
    check_strtol("0x1F", 0);
    check_strtol("0x1F", 16);
    check_strtol("0xg", 16);
    check_strtol("017", 0);
    check_strtol("zz", 36);
    check_strtol("", 10);
    check_strtol("px", 10);
    check_strtol("-", 10);
    check_strtol("99999999999999999999999", 10);
    check_strtol("-99999999999999999999999", 10);
    CHECK(wl_atoi("600") == 600 && wl_atoi("bold") == 0);

    check_printf("3. ", "%d. ", 3);
    check_printf("-12|ab|x", "%d|%.*s|%c", -12, 2, "abcdef", 'x');
    check_printf("http://example.com", "http://%s", "example.com");
    check_printf("4294967295 ffffffff FF", "%lu %lx %X", 4294967295UL, 4294967295UL, 255U);
    check_printf("[  7][7  ][007][-07]", "[%3d][%-3d][%03d][%03d]", 7, 7, 7, -7);
    check_printf("100% (null)", "100%% %s", (char *)NULL);
    sprintf(big, "%ld", LONG_MIN);
    {
        char got[40];
        wl_snprintf(got, sizeof got, "%ld", LONG_MIN);
        CHECK(!strcmp(got, big));
    }
    CHECK(wl_snprintf(NULL, 0, "%s", "abc") == 3);

    check_latin1("plain", "plain");
    check_latin1("caf\xc3\xa9", "caf\xe9");
    check_latin1("\xe2\x80\x9cquoted\xe2\x80\x9d \xe2\x80\x94 \xe2\x82\xac" "5\xe2\x80\xa6", "\"quoted\" - EUR5...");
    check_latin1("\xc5\x81\xc3\xb3" "d\xc5\xba \xc5\x92uvre", "L\xf3" "dz OEuvre");
    check_latin1("a\xe2\x80\x8b" "b\xc2\xad" "c", "ab\xad" "c");   /* the soft hyphen is Latin-1 */
    check_latin1("\xe2\x84\xa2\xe2\x84\xa2", "(TM)(TM)");
    check_latin1("bad \xff\xc3 end", "bad ?? end");
    check_latin1("\xf0\x9f\x98\x80", "?");
    {
        char *e = wl_utf8_to_latin1(NULL, "abc", 0);
        CHECK(e && !*e);
        wl_free(e);
    }

    pool = wl_pool_new();
    CHECK(pool != NULL);
    p = (char *)wl_alloc(pool, 10);
    CHECK(p && !p[0] && !p[9]);
    memcpy(p, "123456789", 10);
    q = (char *)wl_realloc(pool, p, 5000);
    CHECK(q && !strcmp(q, "123456789"));
    p = wl_strndup(pool, "abcdef", 3);
    CHECK(p && !strcmp(p, "abc"));
    wl_free(p);
    wl_free(q);
    wl_free(NULL);
    wl_pool_delete(pool);

    printf("WLLIBC %s (%d checks)\n", failed ? "FAILED" : "PASS", checks);
    return failed;
}
