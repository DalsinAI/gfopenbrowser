/*
 * weblayout: page text (UTF-8) down to ISO-8859-1 for the Amiga's fonts.
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#ifndef WL_TEXT_H
#define WL_TEXT_H

#include <stddef.h>
#include "wl_mem.h"

/* length bytes of UTF-8 as ISO-8859-1, terminated, from pool (wl_free it).
 * Typographic quotes, dashes, the euro sign and the like become their
 * nearest Latin-1; Latin Extended-A letters their base letter; invisible
 * characters are dropped; anything else is '?'. A bad byte is '?'. NULL
 * when there is no memory. */
char *wl_utf8_to_latin1(wl_pool *pool, const char *utf8, size_t length);

#endif
