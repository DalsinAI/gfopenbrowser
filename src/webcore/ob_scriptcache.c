/*
 * GFOpenBrowser: the folder of translated scripts (see ob_scriptcache.h).
 *
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include "ob_scriptcache.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

struct entry {
    char name[24];
    long size;
    time_t when;
};

static int oldestFirst(const void *a, const void *b)
{
    const struct entry *x = a, *y = b;
    return x->when < y->when ? -1 : x->when > y->when;
}

static void pathOf(char *path, size_t size, const char *dir, const char *name)
{
    size_t n = strlen(dir);
    snprintf(path, size, "%s%s%s", dir, n && dir[n - 1] != '/' && dir[n - 1] != ':' ? "/" : "", name);
}

long ob_scriptcache_trim(const char *dir, long max)
{
    DIR *d = opendir(dir);
    struct dirent *e;
    struct entry *list = NULL;
    int n = 0, room = 0, i;
    long total = 0;
    char path[300];
    if (!d)
        return 0;
    while ((e = readdir(d))) {
        size_t len = strlen(e->d_name);
        struct stat st;
        if (len < 5 || len >= sizeof list[0].name || strcmp(e->d_name + len - 4, ".jsc"))
            continue;
        pathOf(path, sizeof path, dir, e->d_name);
        if (stat(path, &st))
            continue;
        if (n == room) {
            struct entry *more = realloc(list, sizeof *list * (room = room ? room * 2 : 64));
            if (!more)
                break;
            list = more;
        }
        strcpy(list[n].name, e->d_name);
        list[n].size = (long)st.st_size;
        list[n].when = st.st_mtime;
        total += list[n].size;
        n++;
    }
    closedir(d);
    if (total > max && list) {
        qsort(list, (size_t)n, sizeof *list, oldestFirst);
        for (i = 0; i < n && total > max / 4 * 3; i++) {
            pathOf(path, sizeof path, dir, list[i].name);
            if (!remove(path))
                total -= list[i].size;
        }
    }
    free(list);
    return total;
}
