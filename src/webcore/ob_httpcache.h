/*
 * GFOpenBrowser: the engine's HTTP disk cache (ob_httpcache.c), so a page
 * seen before loads its style sheets, scripts and pictures from the disk
 * instead of the network. WebCore's own disk cache (NetworkCache) belongs
 * to WebKit's network process, which the Amiga port does not have.
 *
 * A private cache for one user: GET responses with status 200, kept by
 * address (a 64-bit hash names the file, the address inside it is checked),
 * fresh as long as Cache-Control max-age or Expires allows and checked with
 * the server (If-None-Match, If-Modified-Since; a 304 answer reuses the
 * stored copy) after that. Nothing is stored for no-store, for a Vary other
 * than Accept-Encoding, or without either a lifetime or a validator. The
 * folder holds at most a set number of bytes; the entries used longest ago
 * go first. Every entry is written to a temporary file and renamed when
 * complete, so a crash leaves no half entry behind.
 *
 * Not thread-safe: the engine uses it on the main task only.
 * MIT, Copyright (c) 2026 AmigaChrome Team.
 */
#ifndef OB_HTTPCACHE_H
#define OB_HTTPCACHE_H

#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ob_httpcache ob_httpcache;
typedef struct ob_httpcache_entry ob_httpcache_entry;
typedef struct ob_httpcache_writer ob_httpcache_writer;

/* The folder's size unless OB_HTTP_CACHE_MB says otherwise; a default to
 * be measured against real browsing, not a tuned value. */
#define OB_HTTPCACHE_DEFAULT_MB 64

/* Opens the cache in dir (made if missing), at most maxBytes on the disk;
 * one entry may take an eighth of that. NULL if the folder cannot be used. */
ob_httpcache *ob_httpcache_open(const char *dir, unsigned long maxBytes);
/* The same, set by the environment (ENV: on the Amiga): OB_HTTP_CACHE=0
 * turns the cache off (NULL), OB_HTTP_CACHE_DIR names the folder
 * (defaultDir otherwise) and OB_HTTP_CACHE_MB its size in megabytes. */
ob_httpcache *ob_httpcache_open_from_env(const char *defaultDir);
/* Writes the index, so the next start need not read the folder. */
void ob_httpcache_close(ob_httpcache *cache);
unsigned long ob_httpcache_bytes(const ob_httpcache *cache);
unsigned ob_httpcache_count(const ob_httpcache *cache);

/* What a request allows. Each header is its value, or NULL without it. */
enum {
    OB_HTTPCACHE_USE,           /* a fresh copy may answer it */
    OB_HTTPCACHE_REVALIDATE,    /* a stored copy only after the server says 304 */
    OB_HTTPCACHE_BYPASS         /* neither looked up nor stored */
};
struct ob_httpcache_request {
    const char *method;
    const char *cacheControl;
    const char *pragma;
    int hasAuthorization;
    int hasConditional;         /* If-None-Match, If-Modified-Since, If-Range or Range */
};
int ob_httpcache_request_policy(const struct ob_httpcache_request *request);
/* The request's Cache-Control max-age in seconds, or -1 without one. */
long ob_httpcache_request_max_age(const struct ob_httpcache_request *request);

/* What a response allows: seconds it stays fresh from now (0: ask the
 * server each time), or -1 if it must not be stored. */
struct ob_httpcache_response {
    int status;
    const char *cacheControl;
    const char *pragma;
    const char *expires;
    const char *date;
    const char *age;
    const char *vary;
    const char *etag;
    const char *lastModified;
};
long ob_httpcache_lifetime(const struct ob_httpcache_response *response, time_t now);

/* An HTTP date (IMF-fixdate, RFC 850 or asctime) as seconds since 1970, -1
 * if it is not one. */
long ob_httpcache_parse_date(const char *text);

/* The stored response for url, or NULL. */
ob_httpcache_entry *ob_httpcache_lookup(ob_httpcache *cache, const char *url, time_t now);
int ob_httpcache_entry_fresh(const ob_httpcache_entry *entry, time_t now);
long ob_httpcache_entry_age(const ob_httpcache_entry *entry, time_t now);   /* seconds since stored or refreshed */
const char *ob_httpcache_entry_etag(const ob_httpcache_entry *entry);           /* "" without one */
const char *ob_httpcache_entry_last_modified(const ob_httpcache_entry *entry);  /* "" without one */
unsigned ob_httpcache_entry_header_count(const ob_httpcache_entry *entry);
const char *ob_httpcache_entry_header(const ob_httpcache_entry *entry, unsigned i); /* "Name: value" */
unsigned long ob_httpcache_entry_body_size(const ob_httpcache_entry *entry);
/* The body, in order: bytes read (0 at its end), -1 on an error. */
long ob_httpcache_entry_read(ob_httpcache_entry *entry, void *buffer, unsigned long size);
/* After a 304: fresh for lifetime more seconds from now. */
int ob_httpcache_entry_refresh(ob_httpcache *cache, ob_httpcache_entry *entry, long lifetime, time_t now);
void ob_httpcache_entry_free(ob_httpcache_entry *entry);

/* Storing a response: headers are "Name: value" lines, without the ones
 * that must not come back (Set-Cookie, Content-Length, Content-Encoding).
 * write returns 0 once the entry is given up (too big, disk full); commit
 * then stores nothing. commit and abort free the writer. */
ob_httpcache_writer *ob_httpcache_store_begin(ob_httpcache *cache, const char *url, long lifetime, time_t now,
    const char *etag, const char *lastModified, const char *const *headers, unsigned headerCount);
int ob_httpcache_store_write(ob_httpcache_writer *writer, const void *data, unsigned long size);
int ob_httpcache_store_commit(ob_httpcache *cache, ob_httpcache_writer *writer, time_t now);
void ob_httpcache_store_abort(ob_httpcache *cache, ob_httpcache_writer *writer);

/* Forgets url's entry (after a POST, PUT or DELETE to it). */
void ob_httpcache_remove(ob_httpcache *cache, const char *url);

#ifdef __cplusplus
}
#endif

#endif
