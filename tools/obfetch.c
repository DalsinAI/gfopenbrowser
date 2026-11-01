/*
 * obfetch: fetch an address with GFOpenBrowser's engine and print it as text,
 * for testing from a Shell.   obfetch <address>...
 *
 * GFOpenBrowser. MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "oam_html.h"
#include "oam_net.h"
#include "oam_stack.h"
#include "oam_text.h"
#include "ob_http.h"

static const char version[] __attribute__((used)) = "$VER: obfetch 0.1 (4.10.2026)";

static void step(const char *s)
{
    printf("-- %s\n", s);
    fflush(stdout);
}

static int fetch_main(int argc, char **argv)
{
    char err[200];
    ob_response resp;
    oam_buf text, links;
    int ok = 0, i;
    if (argc < 2) { printf("usage: obfetch <address>...\n"); return 10; }
    if (!oam_net_init(err, sizeof err)) { printf("NET_FAIL %s\n", err); return 20; }
    oam_net_progress = step;
    ob_http_progress = step;
    for (i = 1; i < argc; i++) {
    ok = ob_http_get(argv[i], &resp, err, sizeof err);
    if (!ok) {
        printf("FETCH_FAIL %s\n", err);
    } else {
        char *latin1;
        printf("FETCH_OK status=%d type=%s charset=%s bytes=%lu url=%s\n", resp.status, resp.content_type,
               resp.charset, (unsigned long)resp.body.len, resp.final_url);
        oam_buf_init(&text);
        oam_buf_init(&links);
        oam_html_to_text(oam_buf_str(&resp.body), resp.body.len, &text, &links);
        latin1 = oam_utf8_to_latin1(oam_buf_str(&text));
        printf("%s\n-- links\n%s\n", latin1 ? latin1 : "(no memory)", oam_buf_str(&links));
        free(latin1);
        oam_buf_free(&text);
        oam_buf_free(&links);
    }
    ob_response_free(&resp);
    }
    oam_net_cleanup();
    return ok ? 0 : 5;
}

int main(int argc, char **argv)
{
    return oam_run_with_stack(65536, fetch_main, argc, argv);
}
