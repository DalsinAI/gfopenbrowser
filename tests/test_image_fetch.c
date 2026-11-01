#include "ob_http.h"
#include "oam_buf.h"
#include "oam_net.h"
#include "oam_stack.h"
#include "wl_image_dt.h"

#include <intuition/screens.h>
#include <proto/intuition.h>
#include <stdio.h>
#include <string.h>

static int image_fetch_main(int argc, char **argv)
{
    const char *url = argc > 1 ? argv[1] : "https://www.gstatic.com/webp/gallery/1.webp";
    const char *path = "T:GFOpenBrowser-image-fetch.webp";
    char err[200];
    ob_response resp;
    struct Screen *screen = NULL;
    wl_dt_image image = {0};
    FILE *f = NULL;
    int rc = 20;

    if (!oam_net_init(err, sizeof err)) {
        printf("IMGNET FAIL net=%s\n", err);
        return 20;
    }
    if (!ob_http_get(url, &resp, err, sizeof err)) {
        printf("IMGNET FAIL fetch=%s\n", err);
        goto done;
    }
    printf("IMGNET FETCH status=%d type=%s bytes=%lu url=%s\n",
           resp.status, resp.content_type, (unsigned long)resp.body.len,
           resp.final_url);
    if (resp.status != 200 || !resp.body.len) goto done;

    f = fopen(path, "wb");
    if (!f) {
        puts("IMGNET FAIL temp-open");
        goto done;
    }
    if (fwrite(oam_buf_str(&resp.body), 1, resp.body.len, f) != resp.body.len) {
        puts("IMGNET FAIL temp-write");
        fclose(f);
        f = NULL;
        goto done;
    }
    fclose(f);
    f = NULL;

    screen = LockPubScreen(NULL);
    if (!screen) {
        puts("IMGNET FAIL screen");
        goto done;
    }
    if (!wl_dt_image_load(&image, path, screen, 0, 0)) {
        puts("IMGNET FAIL datatype");
        goto done;
    }
    printf("IMGNET PASS source=%ux%u bitmap=%ux%u mask=%s\n",
           image.source_width, image.source_height,
           image.width, image.height, image.mask ? "yes" : "no");
    rc = 0;

done:
    if (f) fclose(f);
    wl_dt_image_free(&image);
    if (screen) UnlockPubScreen(NULL, screen);
    remove(path);
    ob_response_free(&resp);
    oam_net_cleanup();
    wl_dt_close();
    return rc;
}

int main(int argc, char **argv)
{
    return oam_run_with_stack(65536, image_fetch_main, argc, argv);
}
