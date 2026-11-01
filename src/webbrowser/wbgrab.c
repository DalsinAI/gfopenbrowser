/*
 * WBGrab: webbrowser.library's example. URL in; the page's picture
 * (a 24-bit PPM), its text and its title out.
 *   WBGrab URL/A,WIDTH/N,HEIGHT/N,PICTURE/K,TEXT/K,WAIT/N,NOSCRIPTS/S,SHUTDOWN/S
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include <stdio.h>
#include <stdlib.h>
#include <exec/types.h>
#include <dos/dos.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/webbrowser.h>

const char version[] = "$VER: WBGrab 1.0 (6.10.2026)";
struct Library *WebBrowserBase;

static void savePicture(const char *name, const ULONG *argb, LONG width, LONG height)
{
    FILE *f = fopen(name, "wb");
    LONG i;
    if (!f) {
        printf("WBGrab: cannot write %s\n", name);
        return;
    }
    fprintf(f, "P6\n%ld %ld\n255\n", (long)width, (long)height);
    for (i = 0; i < width * height; i++) {
        ULONG p = argb[i];
        putc((p >> 16) & 255, f);
        putc((p >> 8) & 255, f);
        putc(p & 255, f);
    }
    fclose(f);
}

int main(void)
{
    enum { A_URL, A_WIDTH, A_HEIGHT, A_PICTURE, A_TEXT, A_WAIT, A_NOSCRIPTS, A_SHUTDOWN, A_COUNT };
    LONG args[A_COUNT] = { 0 };
    struct RDArgs *rd = ReadArgs((CONST_STRPTR)"URL/A,WIDTH/N,HEIGHT/N,PICTURE/K,TEXT/K,WAIT/N,NOSCRIPTS/S,SHUTDOWN/S", args, NULL);
    struct TagItem tags[] = { { WBA_Scripts, TRUE }, { TAG_DONE, 0 } };
    LONG width, height, wait, state, n;
    APTR view;
    ULONG *pixels;
    char title[256] = "";
    int rc = RETURN_OK;

    if (!rd) {
        PrintFault(IoErr(), (CONST_STRPTR)"WBGrab");
        return RETURN_FAIL;
    }
    width = args[A_WIDTH] ? *(LONG *)args[A_WIDTH] : 800;
    height = args[A_HEIGHT] ? *(LONG *)args[A_HEIGHT] : 600;
    wait = args[A_WAIT] ? *(LONG *)args[A_WAIT] : 120;
    tags[0].ti_Data = !args[A_NOSCRIPTS];
    if (!(WebBrowserBase = OpenLibrary((CONST_STRPTR)WEBBROWSER_NAME, 1))) {
        printf("WBGrab: no %s\n", WEBBROWSER_NAME);
        FreeArgs(rd);
        return RETURN_FAIL;
    }
    if (!(view = WB_OpenView(width, height, tags))) {
        printf("WBGrab: the engine did not start\n");
        rc = RETURN_FAIL;
        goto done;
    }
    WB_Load(view, (CONST_STRPTR)args[A_URL]);
    state = WB_WaitLoaded(view, wait);
    printf("WBGRAB_LOADED %s\n", state == 0 ? "ok" : state == 1 ? "failed" : state == WBERR_TIMEOUT ? "timeout" : "error");
    WB_GetTitle(view, (STRPTR)title, sizeof title);
    printf("WBGRAB_TITLE %s\n", title);
    if (args[A_PICTURE] && (pixels = malloc(width * height * 4))) {
        if (WB_Render(view, pixels, width * 4, 0, 0, width, height) >= 0)
            savePicture((const char *)args[A_PICTURE], pixels, width, height);
        free(pixels);
    }
    n = WB_GetText(view, NULL, 0);
    printf("WBGRAB_TEXT %ld bytes\n", (long)n);
    if (n > 0) {
        char *text = malloc(n + 1);
        if (text) {
            WB_GetText(view, (STRPTR)text, n + 1);
            if (args[A_TEXT]) {
                FILE *f = fopen((const char *)args[A_TEXT], "w");
                if (f) {
                    fputs(text, f);
                    fclose(f);
                }
            } else {
                printf("%.400s\n", text);
            }
            free(text);
        }
    }
    WB_CloseView(view);
done:
    if (args[A_SHUTDOWN])
        printf("WBGRAB_SHUTDOWN %ld\n", (long)WB_Shutdown());
    CloseLibrary(WebBrowserBase);
    FreeArgs(rd);
    return rc;
}
