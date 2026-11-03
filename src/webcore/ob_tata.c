/*
 * GFOpenBrowser: Tata's ad and tracker rules (see ob_tata.h).
 *
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include "ob_tata.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#include "tata/tata_ads.h"

#define OWN_LIST "PROGDIR:Tata.ads"          /* the browser's copy, when Tata isn't installed */

static tata_ads *rules;
static time_t lookedAt;
static time_t dates[3];                      /* the list, ENV:'s rules, ENVARC:'s: 0 for none */
static int loaded;

static void *zeroed(unsigned long size)
{
    return calloc(1, size);
}

static char *readText(const char *path, long *length)
{
    FILE *file = fopen(path, "rb");
    char *text = NULL;
    long n = 0;
    if (!file)
        return NULL;
    if (!fseek(file, 0, SEEK_END) && (n = ftell(file)) >= 0 && n < 4L * 1024 * 1024 && !fseek(file, 0, SEEK_SET)
        && (text = malloc((size_t)n + 1))) {
        if (fread(text, 1, (size_t)n, file) != (size_t)n) {
            free(text);
            text = NULL;
        } else
            text[n] = 0;
    }
    fclose(file);
    *length = text ? n : 0;
    return text;
}

static time_t dateOf(const char *path)
{
    struct stat st;
    return stat(path, &st) ? 0 : st.st_mtime + 1;
}

/* The list (S:, else the browser's copy) and the user's rules (ENV:, else ENVARC:). */
static void load(void)
{
    static const char *const lists[2] = { TATA_ADS_LIST, OWN_LIST };
    static const char *const mine[2] = { TATA_ADS_ENV, TATA_ADS };
    long length;
    char *text;
    int i;
    tata_ads_free(rules);
    rules = tata_ads_new(zeroed, free);
    for (i = 0; rules && i < 2; i++) {
        if ((text = readText(lists[i], &length))) {
            tata_ads_add_text(rules, text, length);
            free(text);
            break;
        }
    }
    for (i = 0; rules && i < 2; i++) {
        if ((text = readText(mine[i], &length))) {
            tata_ads_add_text(rules, text, length);
            free(text);
            break;
        }
    }
    loaded = 1;
}

/* The rules as the files are now: read again when a date changed. */
static const tata_ads *current(void)
{
    time_t now = time(NULL);
    if (!loaded || now != lookedAt) {
        time_t d[3];
        lookedAt = now;
        d[0] = dateOf(TATA_ADS_LIST) ^ dateOf(OWN_LIST);
        d[1] = dateOf(TATA_ADS_ENV);
        d[2] = dateOf(TATA_ADS);
        if (!loaded || memcmp(d, dates, sizeof d)) {
            memcpy(dates, d, sizeof d);
            load();
        }
    }
    return rules;
}

static int sameSite(const char *host, const char *site)
{
    return site && *site && !strcmp(host, site);
}

int ob_tata_blocked(const char *host, const char *path, const char *site)
{
    if (!host || !*host || sameSite(host, site))
        return 0;
    return tata_ads_blocked(current(), host, path, site);
}

int ob_tata_site_allowed(const char *site)
{
    return tata_ads_allowed(current(), site);
}

static int writeText(const char *path, const char *text, long length)
{
    FILE *file = fopen(path, "wb");
    int ok;
    if (!file)
        return 0;
    ok = fwrite(text, 1, (size_t)length, file) == (size_t)length;
    if (fclose(file))
        ok = 0;
    return ok;
}

int ob_tata_allow_site(const char *site, int allow)
{
    long length = 0, size, n;
    char *text, *edited;
    int ok;
    if (!site || !*site)
        return 0;
    if (!(text = readText(TATA_ADS_ENV, &length)) && !(text = readText(TATA_ADS, &length)) && !(text = calloc(1, 1)))
        return 0;
    size = length + (long)strlen(site) + 16;
    if (!(edited = malloc((size_t)size))) {
        free(text);
        return 0;
    }
    n = tata_ads_edit(text, length, allow ? "allow" : NULL, site, edited, size);
    mkdir("ENVARC:Tata", 0777);               /* there already, or made: either way on */
    mkdir("ENV:Tata", 0777);
    ok = n >= 0 && writeText(TATA_ADS, edited, n);
    if (ok)
        writeText(TATA_ADS_ENV, edited, n);   /* straight over ENV:'s copy, as Tata's window writes it */
    free(edited);
    free(text);
    loaded = 0;
    return ok;
}
