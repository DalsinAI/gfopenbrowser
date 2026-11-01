/* ob_http: HTTP/1.1 GET with redirects and chunked bodies.
 *
 * GFOpenBrowser. MIT, Copyright (c) 2026 AmigaChrome Team. */
#include "ob_http.h"
#include "oam_net.h"
#include "weblayout/wl_url.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define USER_AGENT "GFOpenBrowser/0.4.1 (AmigaOS 3.2; m68k)"
#define MAX_REDIRECTS 5
#define MAX_BODY (8L * 1024 * 1024)

void (*ob_http_progress)(const char *step);
#define STEP(s) do { if (ob_http_progress) ob_http_progress(s); } while (0)

static void set_err(char *err, size_t errlen, const char *what)
{
    if (err && errlen) snprintf(err, errlen, "%s", what);
}

static void lower(char *s)
{
    for (; *s; s++) *s = (char)tolower((unsigned char)*s);
}

int ob_url_parse(const char *url, ob_url *out, char *err, size_t errlen)
{
    const char *p = url, *host, *hend, *slash;
    size_t n;
    memset(out, 0, sizeof *out);
    while (*p == ' ' || *p == '\t') p++;
    if (!strncmp(p, "http://", 7)) { p += 7; out->tls = 0; }
    else if (!strncmp(p, "https://", 8)) { p += 8; out->tls = 1; }
    else if (strstr(p, "://")) { set_err(err, errlen, "Only http:// and https:// addresses work so far."); return 0; }
    out->port = out->tls ? 443 : 80;
    host = p;
    slash = strpbrk(host, "/?#");
    hend = slash ? slash : host + strlen(host);
    {
        const char *at = NULL, *colon, *q;
        for (q = host; q < hend; q++)           /* the last '@' ends user:password */
            if (*q == '@') at = q;
        if (at) host = at + 1;                  /* no user:password support; skip it */
        colon = memchr(host, ':', (size_t)(hend - host));   /* the port's, after it */
        if (colon && colon > host) {
            out->port = atoi(colon + 1);
            hend = colon;
        }
    }
    n = (size_t)(hend - host);
    if (!n || n >= sizeof out->host) { set_err(err, errlen, "The address has no host name."); return 0; }
    memcpy(out->host, host, n);
    out->host[n] = 0;
    lower(out->host);
    if (out->port <= 0 || out->port > 65535) { set_err(err, errlen, "The address has a bad port."); return 0; }
    if (!slash || *slash == '#') strcpy(out->path, "/");
    else {
        const char *e = strchr(slash, '#');
        n = e ? (size_t)(e - slash) : strlen(slash);
        if (n >= sizeof out->path) n = sizeof out->path - 1;
        if (*slash == '?') { out->path[0] = '/'; memcpy(out->path + 1, slash, n < sizeof out->path - 1 ? n : sizeof out->path - 2); }
        else memcpy(out->path, slash, n);
    }
    for (p = out->path; *p; p++)
        if (*p == ' ') *(char *)p = '+';         /* keep the request line valid */
    return 1;
}

void ob_url_resolve(const char *base, const char *href, char *out, size_t outlen)
{
    wl_url_resolve(base, href, out, outlen);
}

/* Read everything until the server closes, up to MAX_BODY. */
static int read_all(oam_conn *c, oam_buf *raw)
{
    char buf[4096];
    long n;
    while ((n = oam_net_read(c, buf, sizeof buf)) > 0) {
        if (!oam_buf_add(raw, buf, (size_t)n)) return 0;
        if ((long)raw->len > MAX_BODY) break;
    }
    return n >= 0;
}

static char *find_header(const char *head, const char *name)
{
    size_t nl = strlen(name);
    const char *p = head;
    while ((p = strchr(p, '\n'))) {
        p++;
        if (!strncasecmp(p, name, nl) && p[nl] == ':') {
            const char *v = p + nl + 1, *e;
            char *out;
            while (*v == ' ' || *v == '\t') v++;
            e = v;
            while (*e && *e != '\r' && *e != '\n') e++;
            out = malloc((size_t)(e - v) + 1);
            if (!out) return NULL;
            memcpy(out, v, (size_t)(e - v));
            out[e - v] = 0;
            return out;
        }
    }
    return NULL;
}

/* Decodes a chunked body in place: data[start..len) becomes the body at
 * data[0..]. The body is never longer than what it was read from, so it
 * never overtakes what is still to be read. Returns the body's length. */
static size_t dechunk(char *data, size_t start, size_t len)
{
    const char *p = data + start, *end = data + len;
    size_t o = 0;
    while (p < end) {
        unsigned long size = strtoul(p, NULL, 16);
        const char *line = memchr(p, '\n', (size_t)(end - p));
        if (!line) break;
        p = line + 1;
        if (!size) break;
        if ((size_t)(end - p) < size) size = (unsigned long)(end - p);
        memmove(data + o, p, size);
        o += size;
        p += size;
        if (p < end && *p == '\r') p++;
        if (p < end && *p == '\n') p++;
    }
    return o;
}

static int fetch_once(const ob_url *u, ob_response *resp, char **location, char *err, size_t errlen)
{
    oam_conn *c;
    oam_buf req, raw;
    char *hend, *te, *ct;
    size_t hlen;
    int ok = 0;

    *location = NULL;
    STEP("Connecting");
    c = oam_net_connect(u->host, u->port, u->tls, err, errlen);
    if (!c) return 0;
    oam_net_set_timeout(c, 30);
    oam_buf_init(&req);
    oam_buf_init(&raw);
    oam_buf_printf(&req, "GET %s HTTP/1.1\r\nHost: %s", u->path, u->host);
    if (u->port != (u->tls ? 443 : 80))         /* the Host header names a port that isn't the usual one */
        oam_buf_printf(&req, ":%d", u->port);
    oam_buf_adds(&req, "\r\nUser-Agent: " USER_AGENT
                 "\r\nAccept: text/html, text/plain;q=0.9, */*;q=0.5\r\n"
                 "Accept-Encoding: identity\r\nConnection: close\r\n\r\n");
    STEP("Sending the request");
    if (req.failed || oam_net_write(c, req.data, (long)req.len) < 0) {
        set_err(err, errlen, oam_net_error(c));
        goto out;
    }
    STEP("Receiving");
    if (!read_all(c, &raw) || raw.failed) {
        set_err(err, errlen, raw.failed ? "Out of memory." : oam_net_error(c));
        goto out;
    }
    if (!raw.len) { set_err(err, errlen, "The server sent nothing back."); goto out; }
    hend = strstr(raw.data, "\r\n\r\n");
    hlen = hend ? (size_t)(hend - raw.data) + 4 : raw.len;
    if (!hend) {
        char *lf = strstr(raw.data, "\n\n");
        if (lf) { hend = lf; hlen = (size_t)(lf - raw.data) + 2; }
    }
    if (strncmp(raw.data, "HTTP/", 5)) { set_err(err, errlen, "That is not an HTTP server's answer."); goto out; }
    resp->status = atoi(strchr(raw.data, ' ') ? strchr(raw.data, ' ') + 1 : "0");
    raw.data[hlen - 1] = 0;           /* end the headers as one string */
    if (resp->status >= 300 && resp->status < 400)
        *location = find_header(raw.data, "Location");
    if ((ct = find_header(raw.data, "Content-Type"))) {
        char *semi = strchr(ct, ';'), *cs = strstr(ct, "charset=");
        if (cs) {
            cs += 8;
            if (*cs == '"') cs++;
            snprintf(resp->charset, sizeof resp->charset, "%s", cs);
            resp->charset[strcspn(resp->charset, "\"; ")] = 0;
            lower(resp->charset);
        }
        if (semi) *semi = 0;
        snprintf(resp->content_type, sizeof resp->content_type, "%s", ct);
        resp->content_type[strcspn(resp->content_type, " ")] = 0;
        lower(resp->content_type);
        free(ct);
    }
    /* The body is moved to the front of what was read, which becomes the
     * response's body: one copy of up to MAX_BODY, not two. */
    te = find_header(raw.data, "Transfer-Encoding");
    if (te && strstr(te, "chunked"))
        raw.len = dechunk(raw.data, hlen, raw.len);
    else {
        memmove(raw.data, raw.data + hlen, raw.len - hlen);
        raw.len -= hlen;
    }
    raw.data[raw.len] = 0;
    free(te);
    oam_buf_free(&resp->body);
    resp->body = raw;
    oam_buf_init(&raw);
    ok = 1;
out:
    oam_net_close(c);
    oam_buf_free(&req);
    oam_buf_free(&raw);
    return ok;
}

int ob_http_get(const char *url, ob_response *resp, char *err, size_t errlen)
{
    char current[1280];
    int hops;
    memset(resp, 0, sizeof *resp);
    oam_buf_init(&resp->body);
    snprintf(current, sizeof current, "%s", url);
    for (hops = 0; hops <= MAX_REDIRECTS; hops++) {
        ob_url u;
        char *location = NULL;
        int n;
        if (!ob_url_parse(current, &u, err, errlen)) return 0;
        if ((u.tls && u.port != 443) || (!u.tls && u.port != 80))
            n = snprintf(resp->final_url, sizeof resp->final_url, "%s%s:%d%s",
                         u.tls ? "https://" : "http://", u.host, u.port, u.path);
        else
            n = snprintf(resp->final_url, sizeof resp->final_url, "%s%s%s",
                         u.tls ? "https://" : "http://", u.host, u.path);
        if (n < 0 || n >= (int)sizeof resp->final_url) {
            /* refused, not cut: a cut address is another address */
            set_err(err, errlen, "The address is too long.");
            return 0;
        }
        resp->status = 0;
        resp->content_type[0] = resp->charset[0] = 0;
        if (!fetch_once(&u, resp, &location, err, errlen)) return 0;
        if (!location) return 1;
        {
            char next[1280];
            ob_url_resolve(resp->final_url, location, next, sizeof next);
            free(location);
            snprintf(current, sizeof current, "%s", next);
        }
        STEP("Following a redirect");
    }
    set_err(err, errlen, "Too many redirects.");
    return 0;
}

void ob_response_free(ob_response *resp)
{
    oam_buf_free(&resp->body);
}
