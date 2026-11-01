/*
 * weblayout: addresses on a page made absolute (RFC 3986 section 5.2, the
 * parts pages use: scheme, network path, absolute path, query, fragment and
 * relative paths with ./ and ../ folded).
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include "wl_url.h"

#include <string.h>

#include "wl_libc.h"

/* "scheme:" at the start: a letter, then letters, digits, + - . */
static size_t scheme_len(const char *s)
{
    size_t n = 0;
    if (!wl_isalpha((unsigned char)s[0])) return 0;
    while (wl_isalnum((unsigned char)s[n]) || s[n] == '+' || s[n] == '-' || s[n] == '.') n++;
    return s[n] == ':' ? n : 0;
}

static int put(char *out, size_t outlen, const char *a, size_t an, const char *b)
{
    int n = wl_snprintf(out, outlen, "%.*s%s", (int)an, a, b);
    return n >= 0 && (size_t)n < outlen;
}

/* the path with ./ and ../ segments folded, in place */
static void fold(char *path)
{
    char *w = path, *seg = path;
    while (*seg) {
        if (!strncmp(seg, "./", 2)) { seg += 2; continue; }
        if (!strcmp(seg, ".")) { seg += 1; continue; }
        if (!strncmp(seg, "../", 3) || !strcmp(seg, "..")) {
            seg += seg[2] ? 3 : 2;
            if (w > path + 1) {
                w--;                                    /* back over the '/' */
                while (w > path && w[-1] != '/') w--;
            }
            continue;
        }
        while (*seg && *seg != '/' && *seg != '?' && *seg != '#') *w++ = *seg++;
        if (*seg == '?' || *seg == '#') { while (*seg) *w++ = *seg++; break; }
        if (*seg == '/') *w++ = *seg++;
    }
    *w = 0;
}

int wl_url_resolve(const char *base, const char *href, char *out, size_t outlen)
{
    char basebuf[1400], path[1400];
    const char *auth, *bpath;
    size_t slen, originlen;
    char *q;

    if (!out || !outlen) return 0;
    out[0] = 0;
    if (!href) href = "";
    while (*href == ' ' || *href == '\t' || *href == '\n' || *href == '\r') href++;
    if (scheme_len(href) || !base || !*base) return put(out, outlen, "", 0, href);

    if (strstr(base, "://"))
        wl_snprintf(basebuf, sizeof basebuf, "%s", base);
    else
        wl_snprintf(basebuf, sizeof basebuf, "http://%s", base);
    slen = scheme_len(basebuf);
    if (!slen || strncmp(basebuf + slen, "://", 3)) return put(out, outlen, "", 0, href);
    auth = basebuf + slen + 3;
    bpath = auth + strcspn(auth, "/?#");
    originlen = (size_t)(bpath - basebuf);

    if (href[0] == '/' && href[1] == '/')              /* network path */
        return put(out, outlen, basebuf, slen + 1, href);
    if (href[0] == '/') {                              /* absolute path */
        wl_snprintf(path, sizeof path, "%s", href);
        fold(path);
        return put(out, outlen, basebuf, originlen, path);
    }
    /* the base's path without its fragment, "/" when it has none */
    wl_snprintf(path, sizeof path, "%s%s", *bpath == '/' ? "" : "/", bpath);
    if ((q = strchr(path, '#'))) *q = 0;
    if (!href[0])
        return put(out, outlen, basebuf, originlen, path);
    if (href[0] == '#') {
        size_t n = strlen(path);
        wl_snprintf(path + n, sizeof path - n, "%s", href);
        return put(out, outlen, basebuf, originlen, path);
    }
    if ((q = strchr(path, '?'))) *q = 0;
    if (href[0] == '?') {
        size_t n = strlen(path);
        wl_snprintf(path + n, sizeof path - n, "%s", href);
        return put(out, outlen, basebuf, originlen, path);
    }
    /* relative to the base's folder */
    if ((q = strrchr(path, '/'))) q[1] = 0;
    {
        size_t n = strlen(path);
        if (strlen(href) >= sizeof path - n) return 0;
        memcpy(path + n, href, strlen(href) + 1);
    }
    fold(path);
    return put(out, outlen, basebuf, originlen, path);
}
