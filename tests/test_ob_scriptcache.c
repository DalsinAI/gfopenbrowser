/* Host test of src/webcore/ob_scriptcache.c: the scripts folder kept under
 * its size, oldest .jsg files first, and the .jsc files of before 0.9.0 RC3
 * (which held functions' code as well) removed.
 * MIT, Copyright (c) 2026 AmiCradle Team. */
#include "ob_scriptcache.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>

static int fails;
#define CHECK(c, what) do { if (!(c)) { fails++; printf("FAIL: %s\n", what); } } while (0)

static char dir[256];

static void put(const char *name, long size, long age)
{
    char path[300];
    FILE *f;
    struct timeval tv[2];
    snprintf(path, sizeof path, "%s/%s", dir, name);
    f = fopen(path, "wb");
    while (size-- > 0)
        fputc('x', f);
    fclose(f);
    gettimeofday(&tv[0], NULL);
    tv[0].tv_sec -= age;
    tv[1] = tv[0];
    utimes(path, tv);
}

static int there(const char *name)
{
    char path[300];
    struct stat st;
    snprintf(path, sizeof path, "%s/%s", dir, name);
    return !stat(path, &st);
}

int main(void)
{
    const char *tmp = getenv("TMPDIR");
    long left;
    snprintf(dir, sizeof dir, "%s/obsc.XXXXXX", tmp && *tmp ? tmp : "/tmp");
    if (!mkdtemp(dir))
        return 2;
    put("00000001-00001000.jsc", 4096, 10);
    put("00000002-00001000.jsg", 4000, 300);   /* oldest */
    put("00000003-00001000.jsg", 4000, 200);
    put("00000004-00001000.jsg", 4000, 100);   /* newest */
    put("seen", 50, 400);
    left = ob_scriptcache_trim(dir, 100000);
    CHECK(left == 12000, "under the limit: the three .jsg files counted, nothing else");
    CHECK(!there("00000001-00001000.jsc"), "an old .jsc file is removed");
    CHECK(there("00000002-00001000.jsg") && there("00000004-00001000.jsg"), "under the limit, .jsg files stay");
    CHECK(there("seen"), "the seen file stays");
    left = ob_scriptcache_trim(dir, 10000);     /* over: down to 7500 */
    CHECK(left == 4000, "over the limit: down to three quarters");
    CHECK(!there("00000002-00001000.jsg") && !there("00000003-00001000.jsg"), "the oldest go first");
    CHECK(there("00000004-00001000.jsg"), "the newest stays");
    CHECK(there("seen"), "the seen file is not trimmed");
    CHECK(ob_scriptcache_trim("/nonexistent/folder", 10) == 0, "no folder: 0");
    remove(strcat(strcpy((char[300]){0}, dir), "/00000004-00001000.jsg"));
    remove(strcat(strcpy((char[300]){0}, dir), "/seen"));
    rmdir(dir);
    printf("ob_scriptcache: %s\n", fails ? "FAILED" : "all passed");
    return fails != 0;
}
