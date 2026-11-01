/*
 * weblayout: one allocator for the whole engine.
 *
 * Every block weblayout holds comes from wl_alloc and goes back through
 * wl_free. Linked into a program (and on the host) that is the C library's
 * malloc and free. Built as weblayout.library (WL_EXEC_POOLS) it is an Exec
 * memory pool per document, so the library never needs a caller's C
 * library, and disposing of a document gives back anything still in its
 * pool. A NULL pool is allowed: such blocks come from AllocVec (library) or
 * malloc (linked).
 *
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#ifndef WL_MEM_H
#define WL_MEM_H

#include <stddef.h>

typedef struct wl_pool wl_pool;

/* A pool for one document; NULL when there is no memory. */
wl_pool *wl_pool_new(void);
/* The pool and everything still allocated from it. */
void wl_pool_delete(wl_pool *pool);

/* n bytes, cleared; NULL when there is no memory. */
void *wl_alloc(wl_pool *pool, size_t n);
/* p (from wl_alloc on the same pool, or NULL) grown or shrunk to n bytes;
 * NULL and p untouched when there is no memory. */
void *wl_realloc(wl_pool *pool, void *p, size_t n);
/* A block from wl_alloc or wl_realloc, or NULL. */
void wl_free(void *p);

/* n bytes of s and a terminating 0. */
char *wl_strndup(wl_pool *pool, const char *s, size_t n);

#endif
