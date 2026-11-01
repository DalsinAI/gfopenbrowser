/*
 * weblayout: the few C library calls the engine needs, as its own small
 * versions, so weblayout.library needs no caller's C library. ASCII only,
 * as HTML's and CSS's syntax is; the C locale's answers.
 *
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#ifndef WL_LIBC_H
#define WL_LIBC_H

#include <stdarg.h>
#include <stddef.h>

static inline int wl_isspace(int c) { return c == ' ' || (c >= '\t' && c <= '\r'); }
static inline int wl_isdigit(int c) { return c >= '0' && c <= '9'; }
static inline int wl_isupper(int c) { return c >= 'A' && c <= 'Z'; }
static inline int wl_islower(int c) { return c >= 'a' && c <= 'z'; }
static inline int wl_isalpha(int c) { return wl_isupper(c) || wl_islower(c); }
static inline int wl_isalnum(int c) { return wl_isalpha(c) || wl_isdigit(c); }
static inline int wl_isxdigit(int c)
{
    return wl_isdigit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}
static inline int wl_tolower(int c) { return wl_isupper(c) ? c + ('a' - 'A') : c; }

int wl_strcasecmp(const char *a, const char *b);
int wl_strncasecmp(const char *a, const char *b, size_t n);

/* strtol's decimal, octal (0) and hex (0x) with base 0, or base 2 to 36;
 * clamps at LONG_MIN and LONG_MAX. */
long wl_strtol(const char *s, char **end, int base);
int wl_atoi(const char *s);

/* snprintf for what weblayout writes: %s, %.*s, %c, %d, %i, %u, %x, %X,
 * %ld, %lu, %lx, %%, with a width and the '0' and '-' flags. Always
 * terminated when size > 0; returns the length the whole would have. */
int wl_vsnprintf(char *out, size_t size, const char *fmt, va_list ap);
int wl_snprintf(char *out, size_t size, const char *fmt, ...)
#ifdef __GNUC__
    __attribute__((format(printf, 3, 4)))
#endif
    ;

#endif
