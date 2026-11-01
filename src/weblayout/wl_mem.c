/*
 * weblayout: one allocator (see wl_mem.h).
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include "wl_mem.h"

#include <string.h>

#ifdef WL_EXEC_POOLS

#include <exec/types.h>
#include <exec/memory.h>
#include <proto/exec.h>

struct wl_pool {
    APTR exec;
};

/* in front of every block: where it came from and how big it is */
struct wl_block {
    wl_pool *pool;                  /* NULL: AllocVec */
    ULONG size;                     /* what the caller asked for */
};

#define WL_MAX_BLOCK 0x7fff0000UL

wl_pool *wl_pool_new(void)
{
    wl_pool *pool = (wl_pool *)AllocVec(sizeof *pool, MEMF_ANY | MEMF_CLEAR);
    if (!pool) return NULL;
    pool->exec = CreatePool(MEMF_ANY, 8192, 2048);
    if (!pool->exec) {
        FreeVec(pool);
        return NULL;
    }
    return pool;
}

void wl_pool_delete(wl_pool *pool)
{
    if (!pool) return;
    DeletePool(pool->exec);
    FreeVec(pool);
}

void *wl_alloc(wl_pool *pool, size_t n)
{
    struct wl_block *b;
    ULONG bytes;
    if (n > WL_MAX_BLOCK) return NULL;
    bytes = (ULONG)n + sizeof *b;
    if (pool) {
        b = (struct wl_block *)AllocPooled(pool->exec, bytes);
        if (b) memset(b, 0, bytes);
    } else {
        b = (struct wl_block *)AllocVec(bytes, MEMF_ANY | MEMF_CLEAR);
    }
    if (!b) return NULL;
    b->pool = pool;
    b->size = (ULONG)n;
    return b + 1;
}

void wl_free(void *p)
{
    struct wl_block *b;
    if (!p) return;
    b = (struct wl_block *)p - 1;
    if (b->pool) FreePooled(b->pool->exec, b, b->size + sizeof *b);
    else FreeVec(b);
}

void *wl_realloc(wl_pool *pool, void *p, size_t n)
{
    struct wl_block *b;
    void *q;
    if (!p) return wl_alloc(pool, n);
    b = (struct wl_block *)p - 1;
    if (n <= b->size) return p;     /* shrinking keeps the block */
    q = wl_alloc(b->pool, n);
    if (!q) return NULL;
    memcpy(q, p, b->size);
    wl_free(p);
    return q;
}

#else /* the C library's allocator, linked into a program or on the host */

#include <stdlib.h>

struct wl_pool {
    int unused;
};

wl_pool *wl_pool_new(void)
{
    return (wl_pool *)calloc(1, sizeof(wl_pool));
}

void wl_pool_delete(wl_pool *pool)
{
    free(pool);
}

void *wl_alloc(wl_pool *pool, size_t n)
{
    (void)pool;
    return calloc(1, n ? n : 1);
}

void *wl_realloc(wl_pool *pool, void *p, size_t n)
{
    (void)pool;
    return realloc(p, n ? n : 1);
}

void wl_free(void *p)
{
    free(p);
}

#endif

char *wl_strndup(wl_pool *pool, const char *s, size_t n)
{
    char *p = (char *)wl_alloc(pool, n + 1);
    if (!p) return NULL;
    if (n) memcpy(p, s, n);
    p[n] = 0;
    return p;
}
