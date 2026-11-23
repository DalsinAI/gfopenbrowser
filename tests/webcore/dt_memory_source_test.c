/*
 * DatatypesSource.h (WebKit patch 0127): which datatypes.library is asked
 * for a picture from memory. Built by tests/webcore/test_dt_memory_source.sh
 * with the header taken out of the patch.
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include <stdio.h>
#include "DatatypesSource.h"

static int fails;

static void check(unsigned version, unsigned revision, int want)
{
    int got = obdt_source_from_memory(version, revision);
    if (got != want) {
        printf("FAIL datatypes.library %u.%u: %d, want %d\n", version, revision, got, want);
        fails++;
    }
}

int main(void)
{
    check(47, 100, 1);      /* the open datatypes.library */
    check(47, 101, 1);
    check(47, 65535, 1);
    check(48, 0, 1);        /* a later version, whatever its revision */
    check(53, 2, 1);
    check(47, 99, 0);
    check(47, 3, 0);        /* OS 3.2.3 */
    check(47, 0, 0);
    check(45, 200, 0);      /* an older version with a large revision */
    check(44, 100, 0);
    check(43, 0, 0);        /* the least the decoder opens */
    check(0, 0, 0);
    return fails ? 1 : 0;
}
