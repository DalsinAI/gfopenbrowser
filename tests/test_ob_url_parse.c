/* GFOpenBrowser Lite's address parser (src/ob_http.c, ob_url_parse) on the
 * host: tests/run-host-tests.sh. The network calls are never reached here.
 * MIT, Copyright (c) 2026 AmiCradle Team. */
#include "ob_http.h"
#include "oam_net.h"

#include <stdio.h>
#include <string.h>

/* ob_http.c's network, unused by ob_url_parse */
oam_conn *oam_net_connect(const char *host, int port, int tls, char *err, size_t errlen)
{ (void)host; (void)port; (void)tls; (void)err; (void)errlen; return NULL; }
void oam_net_set_timeout(oam_conn *c, int seconds) { (void)c; (void)seconds; }
long oam_net_read(oam_conn *c, void *buf, long len) { (void)c; (void)buf; (void)len; return -1; }
long oam_net_write(oam_conn *c, const void *buf, long len) { (void)c; (void)buf; (void)len; return -1; }
const char *oam_net_error(oam_conn *c) { (void)c; return "no network"; }
void oam_net_close(oam_conn *c) { (void)c; }

static int failed;

static void check(const char *url, const char *host, int port, const char *path)
{
    ob_url u;
    char err[100];
    if (!ob_url_parse(url, &u, err, sizeof err)) {
        printf("OBURL FAIL %s: %s\n", url, err);
        failed = 1;
    } else if (strcmp(u.host, host) || u.port != port || strcmp(u.path, path)) {
        printf("OBURL FAIL %s: host=%s port=%d path=%s, want %s %d %s\n", url, u.host, u.port, u.path,
               host, port, path);
        failed = 1;
    }
}

int main(void)
{
    check("http://example.com/", "example.com", 80, "/");
    check("https://Example.COM", "example.com", 443, "/");
    check("http://example.com:8080/a?b=1#f", "example.com", 8080, "/a?b=1");
    check("example.com/x", "example.com", 80, "/x");
    check("http://user@host/", "host", 80, "/");
    check("http://u:p@host/", "host", 80, "/");
    check("http://u:p@host:8080/", "host", 8080, "/");
    check("https://u:p@host:8443", "host", 8443, "/");
    check("http://u@x:p@host:81/", "host", 81, "/");
    check("http://host:8080?q", "host", 8080, "/?q");
    if (!failed) printf("OBURL PASS\n");
    return failed ? 20 : 0;
}
