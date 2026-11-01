/*
 * weblayout: addresses on a page made absolute. Plain C, no Amiga headers:
 * GFOpenBrowser Lite's HTTP layer uses it for redirects too.
 * MIT, Copyright (c) 2026 AmigaChrome Team.
 */
#ifndef WL_URL_H
#define WL_URL_H

#include <stddef.h>

/* href as written on the page at base, made absolute into out (always
 * terminated when outlen > 0). A base with no scheme is http://. 1 when it
 * fitted, 0 when it was cut. */
int wl_url_resolve(const char *base, const char *href, char *out, size_t outlen);

#endif
