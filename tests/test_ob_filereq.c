/*
 * The file requester's names (src/webcore/ob_filereq.c): a drawer and a
 * file become a full AmigaDOS name, one a line, for the page.
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include <stdio.h>
#include <string.h>

#include "ob_filereq.h"

static int failures;

#define CHECK(what) \
    do { \
        if (!(what)) { \
            printf("FAILED %s:%d: %s\n", __FILE__, __LINE__, #what); \
            failures++; \
        } \
    } while (0)

int main(void)
{
    char list[64];

    /* a drawer gets a "/", a volume or a drawer that ends in "/" does not */
    list[0] = 0;
    CHECK(ob_filereq_add(list, sizeof list, "Work:Pictures", "cat.png"));
    CHECK(!strcmp(list, "Work:Pictures/cat.png"));
    CHECK(ob_filereq_add(list, sizeof list, "RAM:", "a.txt"));
    CHECK(!strcmp(list, "Work:Pictures/cat.png\nRAM:a.txt"));
    list[0] = 0;
    CHECK(ob_filereq_add(list, sizeof list, "Work:Pictures/", "cat.png"));
    CHECK(!strcmp(list, "Work:Pictures/cat.png"));
    /* no drawer: the file in the current one */
    list[0] = 0;
    CHECK(ob_filereq_add(list, sizeof list, "", "notes"));
    CHECK(ob_filereq_add(list, sizeof list, NULL, "more notes"));
    CHECK(!strcmp(list, "notes\nmore notes"));
    /* AmigaDOS names are Latin-1; the page gets UTF-8 */
    list[0] = 0;
    CHECK(ob_filereq_add(list, sizeof list, "Work:Gr\xfc\xdf" "e", "caf\xe9.txt"));
    CHECK(!strcmp(list, "Work:Gr\xc3\xbc\xc3\x9f" "e/caf\xc3\xa9.txt"));
    /* no file, no line */
    list[0] = 0;
    CHECK(!ob_filereq_add(list, sizeof list, "Work:", ""));
    CHECK(!ob_filereq_add(list, sizeof list, "Work:", NULL));
    CHECK(!list[0]);
    /* a name fits with its ending 0 or not at all, and the list stays whole */
    {
        char small[12];
        small[0] = 0;
        CHECK(ob_filereq_add(small, sizeof small, "RAM:", "1234567"));   /* 11 + 0 */
        CHECK(!strcmp(small, "RAM:1234567"));
        small[0] = 0;
        CHECK(!ob_filereq_add(small, sizeof small, "RAM:", "12345678"));
        CHECK(!small[0]);
        CHECK(!ob_filereq_add(small, sizeof small, "RAM:T", "123456"));  /* the "/" counts */
        CHECK(!ob_filereq_add(small, sizeof small, "RAM:", "123456\xe9")); /* two bytes */
        CHECK(ob_filereq_add(small, sizeof small, "RAM:", "abc"));
        CHECK(!ob_filereq_add(small, sizeof small, "RAM:", "d"));        /* "\n" + 5 + 0 */
        CHECK(!strcmp(small, "RAM:abc"));
        CHECK(ob_filereq_add(small, sizeof small, "", "def"));
        CHECK(!strcmp(small, "RAM:abc\ndef"));
        CHECK(!ob_filereq_add(small, 0, "RAM:", "a"));
        CHECK(!ob_filereq_add(NULL, 12, "RAM:", "a"));
    }

    if (failures) {
        printf("test_ob_filereq: %d FAILED\n", failures);
        return 1;
    }
    printf("test_ob_filereq: PASS\n");
    return 0;
}
