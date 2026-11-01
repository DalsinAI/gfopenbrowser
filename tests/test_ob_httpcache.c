/* The engine's HTTP disk cache (src/webcore/ob_httpcache.c) on the host:
 * tests/run-host-tests.sh, which names a scratch folder as the argument.
 * MIT, Copyright (c) 2026 AmiCradle Team. */
#include "ob_httpcache.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static int failed, checks;

#define CHECK(cond) check((cond), #cond, __LINE__)

static void check(int ok, const char *what, int line)
{
    checks++;
    if (!ok) {
        printf("OBHTTPCACHE FAIL line %d: %s\n", line, what);
        failed = 1;
    }
}

static char dir[1024];

/* Empties the scratch folder. */
static void clearDir(void)
{
    DIR *d = opendir(dir);
    struct dirent *e;
    char path[1300];
    if (!d)
        return;
    while ((e = readdir(d))) {
        if (e->d_name[0] == '.')
            continue;
        sprintf(path, "%s/%s", dir, e->d_name);
        remove(path);
    }
    closedir(d);
}

/* Files in the scratch folder whose names end with suffix ("" for all). */
static int countFiles(const char *suffix)
{
    DIR *d = opendir(dir);
    struct dirent *e;
    int n = 0;
    size_t s = strlen(suffix);
    if (!d)
        return -1;
    while ((e = readdir(d))) {
        size_t l = strlen(e->d_name);
        if (e->d_name[0] != '.' && l >= s && !strcmp(e->d_name + l - s, suffix))
            n++;
    }
    closedir(d);
    return n;
}

static long lifetime(int status, const char *cc, const char *expires, const char *date, const char *age,
    const char *vary, const char *etag, long now)
{
    struct ob_httpcache_response r;
    memset(&r, 0, sizeof r);
    r.status = status;
    r.cacheControl = cc;
    r.expires = expires;
    r.date = date;
    r.age = age;
    r.vary = vary;
    r.etag = etag;
    return ob_httpcache_lifetime(&r, (time_t)now);
}

static int policy(const char *method, const char *cc, const char *pragma, int auth, int conditional)
{
    struct ob_httpcache_request r;
    r.method = method;
    r.cacheControl = cc;
    r.pragma = pragma;
    r.hasAuthorization = auth;
    r.hasConditional = conditional;
    return ob_httpcache_request_policy(&r);
}

static void testPolicy(void)
{
    const long now = 1700000000L;
    /* 784111777 is Sun, 06 Nov 1994 08:49:37 GMT */
    CHECK(ob_httpcache_parse_date("Sun, 06 Nov 1994 08:49:37 GMT") == 784111777L);
    CHECK(ob_httpcache_parse_date("Sunday, 06-Nov-94 08:49:37 GMT") == 784111777L);
    CHECK(ob_httpcache_parse_date("Sun Nov  6 08:49:37 1994") == 784111777L);
    CHECK(ob_httpcache_parse_date("Thu, 01 Jan 1970 00:00:00 GMT") == 0);
    CHECK(ob_httpcache_parse_date("Tue, 29 Feb 2028 23:59:59 GMT") == 1835481599L);
    CHECK(ob_httpcache_parse_date("0") == -1);
    CHECK(ob_httpcache_parse_date("-1") == -1);
    CHECK(ob_httpcache_parse_date("Sun, 06 Foo 1994 08:49:37 GMT") == -1);
    CHECK(ob_httpcache_parse_date("Sun, 06 Nov 1994 25:49:37 GMT") == -1);
    CHECK(ob_httpcache_parse_date("Sun, 06 Nov 1994 08:49:37 PST") == -1);
    CHECK(ob_httpcache_parse_date("") == -1);

    CHECK(policy("GET", NULL, NULL, 0, 0) == OB_HTTPCACHE_USE);
    CHECK(policy("POST", NULL, NULL, 0, 0) == OB_HTTPCACHE_BYPASS);
    CHECK(policy("HEAD", NULL, NULL, 0, 0) == OB_HTTPCACHE_BYPASS);
    CHECK(policy("GET", NULL, NULL, 1, 0) == OB_HTTPCACHE_BYPASS);
    CHECK(policy("GET", NULL, NULL, 0, 1) == OB_HTTPCACHE_BYPASS);
    CHECK(policy("GET", "no-store", NULL, 0, 0) == OB_HTTPCACHE_BYPASS);
    CHECK(policy("GET", "no-cache", NULL, 0, 0) == OB_HTTPCACHE_REVALIDATE);
    CHECK(policy("GET", "max-age=0", NULL, 0, 0) == OB_HTTPCACHE_REVALIDATE);
    CHECK(policy("GET", "max-age=60", NULL, 0, 0) == OB_HTTPCACHE_USE);
    CHECK(policy("GET", NULL, "no-cache", 0, 0) == OB_HTTPCACHE_REVALIDATE);
    {
        struct ob_httpcache_request r = { "GET", "max-age=60", NULL, 0, 0 };
        CHECK(ob_httpcache_request_max_age(&r) == 60);
        r.cacheControl = "no-cache";
        CHECK(ob_httpcache_request_max_age(&r) == -1);
        r.cacheControl = NULL;
        CHECK(ob_httpcache_request_max_age(&r) == -1);
    }

    CHECK(lifetime(200, "max-age=3600", NULL, NULL, NULL, NULL, NULL, now) == 3600);
    CHECK(lifetime(200, "public, MAX-AGE=\"120\"", NULL, NULL, NULL, NULL, NULL, now) == 120);
    CHECK(lifetime(200, "private, max-age=60", NULL, NULL, NULL, NULL, NULL, now) == 60);
    CHECK(lifetime(200, "s-maxage=900", NULL, NULL, NULL, NULL, "\"x\"", now) == 0);
    CHECK(lifetime(200, "s-maxage=900, max-age=30", NULL, NULL, NULL, NULL, NULL, now) == 30);
    CHECK(lifetime(200, "no-store, max-age=3600", NULL, NULL, NULL, NULL, "\"x\"", now) == -1);
    CHECK(lifetime(200, "no-cache", NULL, NULL, NULL, NULL, "\"x\"", now) == 0);
    CHECK(lifetime(200, "no-cache=\"set-cookie, max-age\"", NULL, NULL, NULL, NULL, NULL, now) == -1);
    CHECK(lifetime(200, "max-age=100", NULL, NULL, "30", NULL, NULL, now) == 70);
    CHECK(lifetime(200, "max-age=100", NULL, NULL, "300", NULL, "\"x\"", now) == 0);
    CHECK(lifetime(200, "max-age=abc", NULL, NULL, NULL, NULL, "\"x\"", now) == 0);
    CHECK(lifetime(200, "max-age=99999999999", NULL, NULL, NULL, NULL, NULL, now) == 365L * 24 * 60 * 60);
    /* Expires counts from the server's Date, not from the local clock */
    CHECK(lifetime(200, NULL, "Sun, 06 Nov 1994 09:49:37 GMT", "Sun, 06 Nov 1994 08:49:37 GMT", NULL, NULL, NULL, now) == 3600);
    CHECK(lifetime(200, NULL, "0", "Sun, 06 Nov 1994 08:49:37 GMT", NULL, NULL, "\"x\"", now) == 0);
    CHECK(lifetime(200, NULL, "0", NULL, NULL, NULL, NULL, now) == -1);
    CHECK(lifetime(200, "max-age=10", "Sun, 06 Nov 1994 09:49:37 GMT", "Sun, 06 Nov 1994 08:49:37 GMT", NULL, NULL, NULL, now) == 10);
    /* without Date, Expires counts from now */
    CHECK(lifetime(200, NULL, "Wed, 15 Nov 2023 00:13:20 GMT", NULL, NULL, NULL, NULL, now) == 7200);
    /* no lifetime and no validator: nothing to store */
    CHECK(lifetime(200, NULL, NULL, NULL, NULL, NULL, NULL, now) == -1);
    CHECK(lifetime(200, NULL, NULL, NULL, NULL, NULL, "W/\"abc\"", now) == 0);
    CHECK(lifetime(206, "max-age=60", NULL, NULL, NULL, NULL, NULL, now) == -1);
    CHECK(lifetime(404, "max-age=60", NULL, NULL, NULL, NULL, NULL, now) == -1);
    CHECK(lifetime(200, "max-age=60", NULL, NULL, NULL, "Accept-Encoding", NULL, now) == 60);
    CHECK(lifetime(200, "max-age=60", NULL, NULL, NULL, " accept-encoding , Accept-Encoding", NULL, now) == 60);
    CHECK(lifetime(200, "max-age=60", NULL, NULL, NULL, "Accept-Encoding, Cookie", NULL, now) == -1);
    CHECK(lifetime(200, "max-age=60", NULL, NULL, NULL, "User-Agent", NULL, now) == -1);
    CHECK(lifetime(200, "max-age=60", NULL, NULL, NULL, "*", NULL, now) == -1);
    {
        struct ob_httpcache_response r;
        memset(&r, 0, sizeof r);
        r.status = 200;
        r.pragma = "no-cache";
        r.lastModified = "Sun, 06 Nov 1994 08:49:37 GMT";
        r.expires = "Tue, 14 Nov 2023 22:14:20 GMT";
        CHECK(ob_httpcache_lifetime(&r, (time_t)now) == 0);    /* HTTP/1.0 Pragma without Cache-Control */
        r.cacheControl = "max-age=5";
        CHECK(ob_httpcache_lifetime(&r, (time_t)now) == 5);
    }
}

/* Stores body (size bytes of fill) for url; 1 if it was committed. */
static int store(ob_httpcache *c, const char *url, long life, long now, int fill, unsigned long size)
{
    static const char *const headers[] = { "Content-Type: text/css; charset=utf-8", "X-Test: 1" };
    char chunk[1000];
    ob_httpcache_writer *w = ob_httpcache_store_begin(c, url, life, (time_t)now, "\"v1\"", "Sun, 06 Nov 1994 08:49:37 GMT", headers, 2);
    if (!w)
        return 0;
    memset(chunk, fill, sizeof chunk);
    while (size) {
        unsigned long n = size < sizeof chunk ? size : sizeof chunk;
        if (!ob_httpcache_store_write(w, chunk, n))
            break;
        size -= n;
    }
    return ob_httpcache_store_commit(c, w, (time_t)now);
}

/* The entry's body is size bytes of fill. */
static int bodyIs(ob_httpcache_entry *e, int fill, unsigned long size)
{
    char buffer[777];
    unsigned long total = 0;
    long n;
    if (ob_httpcache_entry_body_size(e) != size)
        return 0;
    while ((n = ob_httpcache_entry_read(e, buffer, sizeof buffer)) > 0) {
        long i;
        for (i = 0; i < n; i++)
            if (buffer[i] != (char)fill)
                return 0;
        total += (unsigned long)n;
    }
    return n == 0 && total == size;
}

static void testStore(void)
{
    const char *a = "https://example.com/a.css", *b = "https://example.com/b.js";
    ob_httpcache *c;
    ob_httpcache_entry *e;
    ob_httpcache_writer *w;
    const char *bad[] = { "X: 1\r\nSet-Cookie: x=1" };

    clearDir();
    c = ob_httpcache_open(dir, 64UL * 1024);
    CHECK(c != NULL);
    if (!c)
        return;
    CHECK(ob_httpcache_lookup(c, a, 1000) == NULL);
    CHECK(store(c, a, 60, 1000, 'a', 3000));
    CHECK(ob_httpcache_count(c) == 1);
    CHECK(countFiles(".tmp") == 0);
    e = ob_httpcache_lookup(c, a, 1010);
    CHECK(e != NULL);
    if (e) {
        CHECK(ob_httpcache_entry_fresh(e, 1010));
        CHECK(ob_httpcache_entry_fresh(e, 1059));
        CHECK(!ob_httpcache_entry_fresh(e, 1060));
        CHECK(!strcmp(ob_httpcache_entry_etag(e), "\"v1\""));
        CHECK(!strcmp(ob_httpcache_entry_last_modified(e), "Sun, 06 Nov 1994 08:49:37 GMT"));
        CHECK(ob_httpcache_entry_header_count(e) == 2);
        CHECK(!strcmp(ob_httpcache_entry_header(e, 0), "Content-Type: text/css; charset=utf-8"));
        CHECK(ob_httpcache_entry_header(e, 2) == NULL);
        CHECK(bodyIs(e, 'a', 3000));
        /* a 304 at 2000 makes it fresh for 100 s more */
        CHECK(ob_httpcache_entry_refresh(c, e, 100, 2000));
        ob_httpcache_entry_free(e);
    }
    e = ob_httpcache_lookup(c, a, 2050);
    CHECK(e && ob_httpcache_entry_fresh(e, 2050) && !ob_httpcache_entry_fresh(e, 2100));
    CHECK(e && ob_httpcache_entry_age(e, 2050) == 50);
    CHECK(e && bodyIs(e, 'a', 3000));
    ob_httpcache_entry_free(e);
    CHECK(ob_httpcache_lookup(c, "https://example.com/a.css?x", 2050) == NULL);

    /* a second store replaces the first; an empty body is a body */
    CHECK(store(c, a, 0, 3000, 'b', 0));
    e = ob_httpcache_lookup(c, a, 3000);
    CHECK(e && !ob_httpcache_entry_fresh(e, 3000) && bodyIs(e, 'b', 0));
    ob_httpcache_entry_free(e);
    CHECK(ob_httpcache_count(c) == 1);

    /* given up: nothing stored, no file left */
    w = ob_httpcache_store_begin(c, b, 60, 3000, NULL, NULL, NULL, 0);
    CHECK(w != NULL);
    ob_httpcache_store_write(w, "partial", 7);
    ob_httpcache_store_abort(c, w);
    CHECK(ob_httpcache_lookup(c, b, 3000) == NULL);
    CHECK(countFiles(".tmp") == 0);
    /* too big for one entry (an eighth of 64 KB) */
    CHECK(!store(c, b, 60, 3000, 'x', 9000));
    CHECK(ob_httpcache_lookup(c, b, 3000) == NULL);
    CHECK(countFiles(".tmp") == 0);
    /* headers alone over the entry limit: refused before any body */
    {
        static char big[7000];
        const char *lines[2] = { big, big };
        memset(big, 'h', sizeof big - 1);
        memcpy(big, "X-Big: ", 7);
        CHECK(ob_httpcache_store_begin(c, b, 60, 3000, NULL, NULL, lines, 2) == NULL);
        CHECK(countFiles(".tmp") == 0);
    }
    /* header lines cannot smuggle a line break */
    CHECK(ob_httpcache_store_begin(c, b, 60, 3000, NULL, NULL, bad, 1) == NULL);
    CHECK(ob_httpcache_store_begin(c, "https://x/\nOBHC1", 60, 3000, NULL, NULL, NULL, 0) == NULL);
    CHECK(ob_httpcache_store_begin(c, b, -1, 3000, NULL, NULL, NULL, 0) == NULL);

    ob_httpcache_remove(c, a);
    CHECK(ob_httpcache_lookup(c, a, 3000) == NULL);
    CHECK(ob_httpcache_count(c) == 0 && ob_httpcache_bytes(c) == 0);
    CHECK(countFiles("") == 0);
    ob_httpcache_close(c);
}

static void testEviction(void)
{
    char url[64];
    int i;
    ob_httpcache *c;
    ob_httpcache_entry *e;

    clearDir();
    /* 16 KB: room for eight entries of 1800 bytes and their headers; one
     * entry may take 2 KB */
    c = ob_httpcache_open(dir, 16UL * 1024);
    CHECK(c != NULL);
    if (!c)
        return;
    for (i = 0; i < 6; i++) {
        sprintf(url, "https://example.com/%d.png", i);
        CHECK(store(c, url, 600, 100 + i, '0' + i, 1800));
    }
    CHECK(ob_httpcache_count(c) == 6);
    /* entry 0 is used again, so entry 1 is now the one used longest ago */
    e = ob_httpcache_lookup(c, "https://example.com/0.png", 200);
    CHECK(e != NULL);
    ob_httpcache_entry_free(e);
    for (i = 6; i < 9; i++) {
        sprintf(url, "https://example.com/%d.png", i);
        CHECK(store(c, url, 600, 300 + i, '0' + i, 1800));
    }
    CHECK(ob_httpcache_bytes(c) <= 16UL * 1024);
    CHECK(ob_httpcache_lookup(c, "https://example.com/1.png", 400) == NULL);
    e = ob_httpcache_lookup(c, "https://example.com/0.png", 400);
    CHECK(e && bodyIs(e, '0', 1800));
    ob_httpcache_entry_free(e);
    e = ob_httpcache_lookup(c, "https://example.com/8.png", 400);
    CHECK(e && bodyIs(e, '8', 1800));
    ob_httpcache_entry_free(e);
    CHECK(countFiles("") == (int)ob_httpcache_count(c));

    /* the index keeps sizes and uses across a restart */
    {
        unsigned count = ob_httpcache_count(c);
        unsigned long bytes = ob_httpcache_bytes(c);
        ob_httpcache_close(c);
        CHECK(countFiles("index") == 1);
        c = ob_httpcache_open(dir, 16UL * 1024);
        CHECK(c && ob_httpcache_count(c) == count && ob_httpcache_bytes(c) == bytes);
        CHECK(countFiles("index") == 0);    /* until it closes again */
        e = c ? ob_httpcache_lookup(c, "https://example.com/8.png", 500) : NULL;
        CHECK(e && bodyIs(e, '8', 1800));
        ob_httpcache_entry_free(e);
    }
    /* after a crash (no index, a temporary file left): the folder is read */
    {
        unsigned count = ob_httpcache_count(c);
        unsigned long bytes = ob_httpcache_bytes(c);
        char path[1200];
        FILE *f;
        ob_httpcache_close(c);
        sprintf(path, "%s/index", dir);
        remove(path);
        sprintf(path, "%s/0123456789abcdef.7.tmp", dir);
        f = fopen(path, "wb");
        if (f) {
            fputs("half", f);
            fclose(f);
        }
        /* not the cache's: left alone */
        sprintf(path, "%s/notes.tmp", dir);
        f = fopen(path, "wb");
        if (f) {
            fputs("mine", f);
            fclose(f);
        }
        c = ob_httpcache_open(dir, 16UL * 1024);
        CHECK(c && ob_httpcache_count(c) == count && ob_httpcache_bytes(c) == bytes);
        CHECK(countFiles(".tmp") == 1 && countFiles("notes.tmp") == 1);
        remove(path);
        e = c ? ob_httpcache_lookup(c, "https://example.com/7.png", 600) : NULL;
        CHECK(e && bodyIs(e, '7', 1800));
        ob_httpcache_entry_free(e);
        /* a smaller limit on the next start evicts at once */
        ob_httpcache_close(c);
        c = ob_httpcache_open(dir, 5000);
        CHECK(c && ob_httpcache_bytes(c) <= 5000 && ob_httpcache_count(c) >= 1);
        ob_httpcache_close(c);
    }
}

static void testEnvironment(void)
{
    ob_httpcache *c;
    char sub[1100];
    struct stat st;
    clearDir();
    setenv("OB_HTTP_CACHE", "0", 1);
    CHECK(ob_httpcache_open_from_env(dir) == NULL);
    setenv("OB_HTTP_CACHE", "1", 1);
    sprintf(sub, "%s/sub", dir);
    setenv("OB_HTTP_CACHE_DIR", sub, 1);
    setenv("OB_HTTP_CACHE_MB", "1", 1);
    c = ob_httpcache_open_from_env("/nonexistent/folder");
    CHECK(c != NULL);
    CHECK(!stat(sub, &st) && S_ISDIR(st.st_mode));
    /* 1 MB: one entry may take 128 KB */
    CHECK(c && !store(c, "https://example.com/big", 60, 1, 'z', 200000));
    CHECK(c && store(c, "https://example.com/small", 60, 1, 'z', 100000));
    ob_httpcache_close(c);
    unsetenv("OB_HTTP_CACHE_MB");
    unsetenv("OB_HTTP_CACHE_DIR");
    c = ob_httpcache_open_from_env(sub);
    CHECK(c && store(c, "https://example.com/big", 60, 1, 'z', 200000));
    CHECK(c && ob_httpcache_count(c) == 2);
    ob_httpcache_close(c);
    unsetenv("OB_HTTP_CACHE");
    CHECK(ob_httpcache_open("/nonexistent/folder/deeper", 1024) == NULL);
    {
        /* tidy up the sub folder */
        char keep[1024];
        strcpy(keep, dir);
        strcpy(dir, sub);
        clearDir();
        strcpy(dir, keep);
        remove(sub);
    }
}

/* Many entries (1 Nov 2026: lookups go through a hash table): every one is
 * found, after some go every one left is found, and none that went is. */
static void testMany(void)
{
    ob_httpcache *c;
    char url[64];
    int i, found = 0, gone = 0;
    clearDir();
    c = ob_httpcache_open(dir, 64UL * 1024 * 1024);
    CHECK(c != NULL);
    if (!c)
        return;
    for (i = 0; i < 600; i++) {
        ob_httpcache_writer *w;
        sprintf(url, "http://many.example/%d", i);
        w = ob_httpcache_store_begin(c, url, 60, 1000, NULL, NULL, NULL, 0);
        if (w && ob_httpcache_store_write(w, "x", 1))
            ob_httpcache_store_commit(c, w, 1000);
        else if (w)
            ob_httpcache_store_abort(c, w);
    }
    CHECK(ob_httpcache_count(c) == 600);
    for (i = 0; i < 600; i += 3) {
        sprintf(url, "http://many.example/%d", i);
        ob_httpcache_remove(c, url);
    }
    for (i = 0; i < 600; i++) {
        ob_httpcache_entry *e;
        sprintf(url, "http://many.example/%d", i);
        e = ob_httpcache_lookup(c, url, 1001);
        if (e) {
            found += i % 3 != 0;
            gone += i % 3 == 0;
            ob_httpcache_entry_free(e);
        }
    }
    CHECK(found == 400);
    CHECK(gone == 0);
    CHECK(ob_httpcache_count(c) == 400);
    ob_httpcache_close(c);
    c = ob_httpcache_open(dir, 64UL * 1024 * 1024);      /* from the index */
    CHECK(c && ob_httpcache_count(c) == 400);
    if (c) {
        ob_httpcache_entry *e = ob_httpcache_lookup(c, "http://many.example/599", 1002);
        CHECK(e != NULL);
        if (e)
            ob_httpcache_entry_free(e);
        ob_httpcache_close(c);
    }
    clearDir();
}

int main(int argc, char **argv)
{
    if (argc < 2 || strlen(argv[1]) > 900) {
        printf("usage: test_ob_httpcache scratch-folder\n");
        return 2;
    }
    strcpy(dir, argv[1]);
    mkdir(dir, 0755);
    testPolicy();
    testStore();
    testEviction();
    testEnvironment();
    testMany();
    clearDir();
    if (failed)
        return 1;
    printf("OBHTTPCACHE PASS (%d checks)\n", checks);
    return 0;
}
