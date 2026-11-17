/*
 * The clipboard's IFF FTXT clips (src/webcore/ob_clipboard.c): what the
 * browser writes for a copy and what it makes of another program's clip.
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include <stdio.h>
#include <string.h>

#include "ob_clipboard.h"

static int failures;

#define CHECK(what) \
    do { \
        if (!(what)) { \
            printf("FAILED %s:%d: %s\n", __FILE__, __LINE__, #what); \
            failures++; \
        } \
    } while (0)

static long text(const void *clip, size_t n, char *out, size_t size)
{
    long length = obclip_ftxt_text(clip, n, out, size - 1);
    if (length >= 0)
        out[length] = 0;
    return length;
}

int main(void)
{
    unsigned char clip[256];
    char out[256];
    size_t n;

    /* plain ASCII: one CHRS chunk, as every Amiga program writes it */
    n = obclip_ftxt_build("Amiga", 5, clip, sizeof clip);
    CHECK(n == 26);
    CHECK(!memcmp(clip, "FORM\0\0\0\x12" "FTXTCHRS\0\0\0\5Amiga\0", 26));
    CHECK(text(clip, n, out, sizeof out) == 5 && !strcmp(out, "Amiga"));
    /* the size is told without a buffer, and nothing is written past one too small */
    CHECK(obclip_ftxt_build("Amiga", 5, NULL, 0) == 26);
    memset(clip, 'x', sizeof clip);
    CHECK(obclip_ftxt_build("Amiga", 5, clip, 25) == 26 && clip[0] == 'x');
    /* an even length has no pad byte; empty text is an empty chunk */
    CHECK(obclip_ftxt_build("OS32", 4, clip, sizeof clip) == 24 && clip[7] == 16);
    n = obclip_ftxt_build("", 0, clip, sizeof clip);
    CHECK(n == 20 && text(clip, n, out, sizeof out) == 0);

    /* Latin-1 characters: CHRS in Latin-1 and a UTF8 chunk */
    n = obclip_ftxt_build("caf\xc3\xa9", 5, clip, sizeof clip);
    CHECK(n == 12 + 8 + 4 + 8 + 5 + 1);
    CHECK(!memcmp(clip + 12, "CHRS\0\0\0\4caf\xe9", 12));
    CHECK(!memcmp(clip + 24, "UTF8\0\0\0\5caf\xc3\xa9\0", 14));
    CHECK(clip[4] == 0 && clip[5] == 0 && clip[6] == 0 && clip[7] == n - 8);
    CHECK(text(clip, n, out, sizeof out) == 5 && !strcmp(out, "caf\xc3\xa9"));
    /* beyond Latin-1: "?" for old programs, all of it for new ones */
    n = obclip_ftxt_build("\xe2\x82\xac" "5 \xf0\x9f\x98\x80", 9, clip, sizeof clip);
    CHECK(!memcmp(clip + 12, "CHRS\0\0\0\4?5 ?", 12));
    CHECK(text(clip, n, out, sizeof out) == 9 && !strcmp(out, "\xe2\x82\xac" "5 \xf0\x9f\x98\x80"));
    /* bytes that are not UTF-8 are Latin-1: a lone e9, a cut sequence, an overlong form */
    n = obclip_ftxt_build("\xe9t\xe2\x82", 4, clip, sizeof clip);
    CHECK(!memcmp(clip + 12, "CHRS\0\0\0\4\xe9t\xe2\x82", 12));
    CHECK(text(clip, n, out, sizeof out) == 7 && !strcmp(out, "\xc3\xa9t\xc3\xa2\xc2\x82"));
    n = obclip_ftxt_build("\xc0\x80", 2, clip, sizeof clip);
    CHECK(!memcmp(clip + 12, "CHRS\0\0\0\2\xc0\x80", 10));

    /* another program's clip: Latin-1 CHRS, several of them, other chunks between */
    {
        static const unsigned char theirs[] =
            "FORM\0\0\0\x2c" "FTXT"
            "FONS\0\0\0\4\0\0\0\0"
            "CHRS\0\0\0\5Gr\xfc\xdf" "e\0"
            "CHRS\0\0\0\6 Amiga";
        CHECK(text(theirs, sizeof theirs - 1, out, sizeof out) == 13);
        CHECK(!strcmp(out, "Gr\xc3\xbc\xc3\x9f" "e Amiga"));
        /* cut to the room there is, at a whole character */
        CHECK(obclip_ftxt_text(theirs, sizeof theirs - 1, out, 3) == 2 && !memcmp(out, "Gr", 2));
        CHECK(obclip_ftxt_text(theirs, sizeof theirs - 1, out, 4) == 4);
        /* a clip cut short gives what is there */
        CHECK(text(theirs, 12 + 12 + 8 + 3, out, sizeof out) == 4 && !strcmp(out, "Gr\xc3\xbc"));
    }
    /* a UTF8 chunk wins over CHRS wherever it stands */
    {
        static const unsigned char both[] =
            "FORM\0\0\0\x1a" "FTXT"
            "UTF8\0\0\0\3\xe2\x82\xac\0"
            "CHRS\0\0\0\1?\0";
        CHECK(text(both, sizeof both - 1, out, sizeof out) == 3 && !strcmp(out, "\xe2\x82\xac"));
        CHECK(obclip_ftxt_text(both, sizeof both - 1, out, 2) == 0);
    }
    /* a chunk that says it is longer than the clip, and things that are not text */
    {
        static const unsigned char lying[] = "FORM\0\0\0\x10" "FTXTCHRS\xff\xff\xff\xf0hi";
        CHECK(text(lying, sizeof lying - 1, out, sizeof out) == 2 && !strcmp(out, "hi"));
        CHECK(obclip_ftxt_text((const unsigned char *)"FORM\0\0\0\4ILBM", 12, out, sizeof out) == -1);
        CHECK(obclip_ftxt_text((const unsigned char *)"FORM", 4, out, sizeof out) == -1);
        CHECK(obclip_ftxt_text((const unsigned char *)"LIST\0\0\0\4FTXT", 12, out, sizeof out) == -1);
        CHECK(text((const unsigned char *)"FORM\0\0\0\4FTXT", 12, out, sizeof out) == 0);
    }

    if (failures) {
        printf("test_ob_clipboard: %d FAILED\n", failures);
        return 1;
    }
    printf("test_ob_clipboard: PASS\n");
    return 0;
}
