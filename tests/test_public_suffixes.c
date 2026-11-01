/*
 * The engine's public suffix lookup (WebKit patch 0070's
 * PublicSuffixAmiga.c) against the Public Suffix List's own checks
 * (tests/public-suffix-cases.txt), on the list the package ships
 * (data/public-suffixes), and a few is-a-suffix answers as libpsl gave them.
 *   test_public_suffixes data/public-suffixes tests/public-suffix-cases.txt
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "PublicSuffixAmiga.h"

static int failures;

static char *readAll(const char *path)
{
    FILE *f = fopen(path, "rb");
    char *text;
    long size;

    if (!f || fseek(f, 0, SEEK_END) || (size = ftell(f)) < 0 || fseek(f, 0, SEEK_SET)
        || !(text = malloc((size_t)size + 1)) || fread(text, 1, (size_t)size, f) != (size_t)size) {
        fprintf(stderr, "cannot read %s\n", path);
        exit(2);
    }
    text[size] = 0;
    fclose(f);
    return text;
}

static void suffix(const char *name, int want)
{
    int got = ob_psl_is_public_suffix(name);
    if (got != want) {
        printf("FAIL is_public_suffix(%s): %d, want %d\n", name, got, want);
        failures++;
    }
}

int main(int argc, char **argv)
{
    char line[600];
    FILE *cases;
    int n = 0;

    if (argc != 3) {
        fprintf(stderr, "usage: test_public_suffixes LIST CASES\n");
        return 2;
    }
    if (!ob_psl_load_text(readAll(argv[1]))) {
        printf("FAIL %s is not an OBPSL1 list\n", argv[1]);
        return 1;
    }
    if (!(cases = fopen(argv[2], "r")))
        return 2;
    while (fgets(line, sizeof line, cases)) {
        char name[300], want[300];
        const char *got;
        if (line[0] == '#' || sscanf(line, "%299s %299s", name, want) != 2)
            continue;
        got = ob_psl_registrable_domain(name);
        if (strcmp(got ? got : "-", want)) {
            printf("FAIL registrable(%s): %s, want %s\n", name, got ? got : "-", want);
            failures++;
        }
        n++;
    }
    fclose(cases);

    suffix("com", 1);
    suffix("co.uk", 1);
    suffix("example.com", 0);
    suffix("example", 0);           /* unlisted: "*" alone doesn't count */
    suffix("ck", 1);                /* from "*.ck" */
    suffix("anything.ck", 1);
    suffix("www.ck", 0);            /* "!www.ck" */
    suffix("github.io", 1);         /* the private section counts too */
    suffix("", 0);
    suffix(".com", 0);

    printf("public suffixes: %d checks of the list, 10 suffix checks, %d failed\n", n, failures);
    return failures != 0;
}
