/* GFOpenBrowser Lite's HTTP GET (src/ob_http.c, ob_http_get) on the host, on
 * a stand-in network that answers from memory: tests/run-host-tests.sh.
 * ob_http.c and oam_buf.c are built with malloc, realloc and free renamed
 * to the counting ones here, so the test sees the most memory held at once.
 * MIT, Copyright (c) 2026 AmigaChrome Team. */
#include "ob_http.h"
#include "oam_net.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#undef malloc
#undef realloc
#undef free

/* ---- counting allocator ---- */
static size_t live, peak;
void *t_malloc(size_t n)
{
    size_t *p = malloc(n + sizeof(size_t) * 2);
    if (!p) return NULL;
    p[0] = n;
    live += n;
    if (live > peak) peak = live;
    return p + 2;
}
void t_free(void *q)
{
    size_t *p = q;
    if (!p) return;
    p -= 2;
    live -= p[0];
    free(p);
}
void *t_realloc(void *q, size_t n)
{
    size_t *p = q, old;
    void *r;
    if (!p) return t_malloc(n);
    old = p[-2];
    r = t_malloc(n);
    if (!r) return NULL;
    memcpy(r, q, old < n ? old : n);
    t_free(q);
    return r;
}

/* ---- stand-in network: one canned answer, the request kept ---- */
struct oam_conn { int dummy; };
static struct oam_conn conn;
static const char *answer;
static size_t answer_len, answer_at;
static char request[2048];
static char asked_host[256];
static int asked_port;

oam_conn *oam_net_connect(const char *host, int port, int tls, char *err, size_t errlen)
{
    (void)tls; (void)err; (void)errlen;
    snprintf(asked_host, sizeof asked_host, "%s", host);
    asked_port = port;
    answer_at = 0;
    request[0] = 0;
    return &conn;
}
void oam_net_set_timeout(oam_conn *c, int seconds) { (void)c; (void)seconds; }
long oam_net_read(oam_conn *c, void *buf, long len)
{
    size_t n = answer_len - answer_at;
    (void)c;
    if (n > (size_t)len) n = (size_t)len;
    memcpy(buf, answer + answer_at, n);
    answer_at += n;
    return (long)n;
}
long oam_net_write(oam_conn *c, const void *buf, long len)
{
    size_t have = strlen(request);
    (void)c;
    if (have + (size_t)len < sizeof request) {
        memcpy(request + have, buf, (size_t)len);
        request[have + (size_t)len] = 0;
    }
    return len;
}
const char *oam_net_error(oam_conn *c) { (void)c; return "stand-in error"; }
void oam_net_close(oam_conn *c) { (void)c; }

static int failed;
#define CHECK(c) do { if (!(c)) { printf("OBHTTP FAIL line %d: %s\n", __LINE__, #c); failed = 1; } } while (0)

static int get(const char *url, const char *canned, size_t canned_len, ob_response *r)
{
    char err[200];
    answer = canned;
    answer_len = canned_len;
    return ob_http_get(url, r, err, sizeof err);
}

int main(void)
{
    ob_response r;
    static const char plain[] = "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=UTF-8\r\n\r\n<p>hello</p>";
    static const char chunked[] = "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n"
                                  "5\r\nhello\r\n7;ext=1\r\n, world\r\n0\r\n\r\n";
    static const char empty[] = "HTTP/1.1 204 No Content\r\n\r\n";
    size_t big = 4L * 1024 * 1024, i;
    char *bigans;

    CHECK(get("http://example.com:8080/a", plain, sizeof plain - 1, &r));
    CHECK(r.status == 200 && !strcmp(r.content_type, "text/html") && !strcmp(r.charset, "utf-8"));
    CHECK(r.body.len == 12 && !strcmp(oam_buf_str(&r.body), "<p>hello</p>"));
    CHECK(asked_port == 8080 && strstr(request, "\r\nHost: example.com:8080\r\n"));
    CHECK(!strncmp(request, "GET /a HTTP/1.1\r\n", 17) && strstr(request, "\r\nConnection: close\r\n\r\n"));
    ob_response_free(&r);

    CHECK(get("http://example.com/", plain, sizeof plain - 1, &r));
    CHECK(strstr(request, "\r\nHost: example.com\r\n") != NULL);
    ob_response_free(&r);
    CHECK(get("https://example.com/", plain, sizeof plain - 1, &r));
    CHECK(strstr(request, "\r\nHost: example.com\r\n") != NULL);
    ob_response_free(&r);
    CHECK(get("https://example.com:8443/", plain, sizeof plain - 1, &r));
    CHECK(strstr(request, "\r\nHost: example.com:8443\r\n") != NULL);
    ob_response_free(&r);

    CHECK(get("http://example.com/c", chunked, sizeof chunked - 1, &r));
    CHECK(r.body.len == 12 && !strcmp(oam_buf_str(&r.body), "hello, world"));
    ob_response_free(&r);

    CHECK(get("http://example.com/e", empty, sizeof empty - 1, &r));
    CHECK(r.status == 204 && r.body.len == 0 && !strcmp(oam_buf_str(&r.body), ""));
    ob_response_free(&r);

    /* a 4 MB body: held once, not once as read and again as the body (before:
     * the 8 MB read buffer and an 8 MB body at once) */
    bigans = malloc(big + 64);
    i = (size_t)sprintf(bigans, "HTTP/1.0 200 OK\r\nContent-Type: text/plain\r\n\r\n");
    memset(bigans + i, 'x', big);
    peak = live = 0;
    CHECK(get("http://example.com/big", bigans, i + big, &r));
    CHECK(r.body.len == big && r.body.data[0] == 'x' && r.body.data[big - 1] == 'x' && r.body.data[big] == 0);
    printf("OBHTTP a %lu-byte body: at most %lu bytes held at once\n", (unsigned long)big, (unsigned long)peak);
    CHECK(peak < 3 * big + big / 2);       /* reading grows by doubling: 4 + 8 MB while it moves */
    ob_response_free(&r);
    CHECK(live == 0);
    free(bigans);

    if (!failed) printf("OBHTTP PASS\n");
    return failed ? 20 : 0;
}
