/* ob_http: fetch a web address over HTTP/1.1, plain or TLS, on oam_net
 * (bsdsocket.library and GFOpenTLS, gfopentls.library, on the Amiga).
 *
 * GFOpenBrowser. MIT, Copyright (c) 2026 AmiCradle Team. */
#ifndef OB_HTTP_H
#define OB_HTTP_H

#include <stddef.h>
#include "oam_buf.h"

typedef struct ob_url {
    int tls;              /* https */
    char host[256];
    int port;
    char path[1024];      /* from the first '/', with the query; "/" when empty */
} ob_url;

typedef struct ob_response {
    int status;               /* 200, 404...; 0 when nothing came back */
    char final_url[1280];     /* after redirects */
    char content_type[128];   /* "text/html" etc., lower case, no parameters */
    char charset[48];         /* lower case, "" when the server gave none */
    oam_buf body;
} ob_response;

/* 1 when url parses; a bare "host/path" means http://. */
int ob_url_parse(const char *url, ob_url *out, char *err, size_t errlen);
/* Make an absolute address from href as seen on the page at base. */
void ob_url_resolve(const char *base, const char *href, char *out, size_t outlen);

/* Reports each step (connecting, the handshake, receiving) when set. */
extern void (*ob_http_progress)(const char *step);

/* GET url, following up to 5 redirects. 1 with the response filled in (any
 * status), 0 with the reason in err. Free the response with
 * ob_response_free. */
int ob_http_get(const char *url, ob_response *resp, char *err, size_t errlen);
void ob_response_free(ob_response *resp);

#endif
