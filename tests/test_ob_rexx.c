/*
 * GFOpenBrowser's ARexx commands (src/webcore/ob_rexx.c) on the host, with
 * a stand-in page: each command's return code, answer and what it asked
 * the page to do.
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include "ob_rexx.h"

#include <stdio.h>
#include <string.h>

static int failures;

#define CHECK(what) \
    do { \
        if (!(what)) { \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #what); \
            failures++; \
        } \
    } while (0)

struct page {
    char opened[2048];
    int reloads, stops, backs, forwards, quits, front, back_ok, forward_ok, loading, percent;
    long load_ms, loads;
};

static void p_open(void *user, const char *address)
{
    struct page *p = user;
    snprintf(p->opened, sizeof p->opened, "%s", address);
}
static void p_reload(void *user) { ((struct page *)user)->reloads++; }
static void p_stop(void *user) { ((struct page *)user)->stops++; }
static int p_back(void *user) { struct page *p = user; p->backs++; return p->back_ok; }
static int p_forward(void *user) { struct page *p = user; p->forwards++; return p->forward_ok; }
static const char *p_url(void *user) { (void)user; return "https://example.com/"; }
static const char *p_title(void *user) { (void)user; return "Example Domain"; }
static int p_loading(void *user, int *percent)
{
    struct page *p = user;
    *percent = p->percent;
    return p->loading;
}
static long p_load_ms(void *user) { return ((struct page *)user)->load_ms; }
static long p_load_count(void *user) { return ((struct page *)user)->loads; }
static int p_text(void *user, char *buffer, int size)
{
    (void)user;
    snprintf(buffer, (size_t)size, "%s", "This domain is for use in examples.");
    return 35;
}
static void p_activate(void *user, int front) { ((struct page *)user)->front = front ? 1 : -1; }
static int p_fault(void *user, long code, char *buffer, int size)
{
    (void)user;
    if (code != 205)
        return 0;
    snprintf(buffer, (size_t)size, "%s", "object not found");
    return 1;
}
static void p_quit(void *user) { ((struct page *)user)->quits++; }
static char saved[64];
static int savedFormat = -1;
static long saveAnswer;
static int scriptAnswer = 1;
static int p_source(void *user, char *buffer, int size)
{
    (void)user;
    snprintf(buffer, size, "<html><head></head><body><h1>Example Domain</h1></body></html>");
    return 1;
}
static long p_save(void *user, const char *file, int format)
{
    (void)user;
    snprintf(saved, sizeof saved, "%s", file);
    savedFormat = format;
    return saveAnswer;
}
static int p_script(void *user, const char *script, char *buffer, int size)
{
    (void)user;
    if (scriptAnswer == 1)
        snprintf(buffer, size, "ran:%s", script);
    else if (!scriptAnswer)
        snprintf(buffer, size, "ReferenceError: Can't find variable: nope");
    return scriptAnswer;
}
static void batch2(ob_rexx *rx)
{
    char result[256], small[8];
    /* before the page gives them: 5, with a reason */
    CHECK(ob_rexx_command(rx, "GETSOURCE", result, sizeof result) == OB_RX_NOW);
    CHECK(ob_rexx_command(rx, "SAVE RAM:a.html", result, sizeof result) == OB_RX_NOW);
    CHECK(ob_rexx_command(rx, "RUNSCRIPT 1+1", result, sizeof result) == OB_RX_NOW);
    CHECK(ob_rexx_command(rx, "PRINT", result, sizeof result) == OB_RX_NOW);
    CHECK(ob_rexx_command(rx, "LASTERROR", result, sizeof result) == OB_RX_OK);
    CHECK(strstr(result, "doesn't print yet"));
    rx->host.source = p_source;
    rx->host.save = p_save;
    rx->host.script = p_script;
    CHECK(ob_rexx_command(rx, "getsource", result, sizeof result) == OB_RX_OK);
    CHECK(!strncmp(result, "<html>", 6) && strstr(result, "</html>"));
    CHECK(ob_rexx_command(rx, "GETSOURCE", small, sizeof small) == OB_RX_OK);
    CHECK(!strcmp(small, "<html><"));
    /* SAVE: the format follows the name */
    CHECK(ob_rexx_command(rx, "SAVE RAM:page.html", result, sizeof result) == OB_RX_OK);
    CHECK(!strcmp(saved, "RAM:page.html") && savedFormat == OB_RX_SAVE_HTML && !result[0]);
    CHECK(ob_rexx_command(rx, "SAVE \"RAM:My Page.TXT\"", result, sizeof result) == OB_RX_OK);
    CHECK(!strcmp(saved, "RAM:My Page.TXT") && savedFormat == OB_RX_SAVE_TEXT);
    CHECK(ob_rexx_command(rx, "save FILE=RAM:shot.png", result, sizeof result) == OB_RX_OK);
    CHECK(!strcmp(saved, "RAM:shot.png") && savedFormat == OB_RX_SAVE_PNG);
    CHECK(ob_rexx_command(rx, "SAVE RAM:noending", result, sizeof result) == OB_RX_OK);
    CHECK(savedFormat == OB_RX_SAVE_HTML);
    CHECK(ob_rexx_command(rx, "SAVE .png", result, sizeof result) == OB_RX_OK);
    CHECK(savedFormat == OB_RX_SAVE_HTML);   /* a name that is only an ending is a name */
    CHECK(ob_rexx_command(rx, "SAVE", result, sizeof result) == OB_RX_ERROR);
    CHECK(ob_rexx_command(rx, "LASTERROR", result, sizeof result) == OB_RX_OK);
    CHECK(!strcmp(result, "SAVE needs a file"));
    saveAnswer = 205;
    CHECK(ob_rexx_command(rx, "SAVE Nowhere:x.html", result, sizeof result) == OB_RX_ERROR);
    CHECK(ob_rexx_command(rx, "LASTERROR", result, sizeof result) == OB_RX_OK);
    CHECK(strstr(result, "error 205") && strstr(result, "FAULT 205"));
    saveAnswer = -1;
    CHECK(ob_rexx_command(rx, "SAVE RAM:x.png", result, sizeof result) == OB_RX_NOW);
    CHECK(ob_rexx_command(rx, "LASTERROR", result, sizeof result) == OB_RX_OK);
    CHECK(strstr(result, "PNG datatype"));
    saveAnswer = 0;
    /* RUNSCRIPT: the script goes as written, quotes and all */
    CHECK(ob_rexx_command(rx, "RUNSCRIPT document.title", result, sizeof result) == OB_RX_OK);
    CHECK(!strcmp(result, "ran:document.title"));
    CHECK(ob_rexx_command(rx, "js  'a' + \"b\"", result, sizeof result) == OB_RX_OK);
    CHECK(!strcmp(result, "ran:'a' + \"b\""));
    CHECK(ob_rexx_command(rx, "RUNSCRIPT", result, sizeof result) == OB_RX_ERROR);
    CHECK(ob_rexx_command(rx, "JSON", result, sizeof result) == OB_RX_FAIL);
    scriptAnswer = 0;
    CHECK(ob_rexx_command(rx, "RUNSCRIPT nope", result, sizeof result) == OB_RX_ERROR && !result[0]);
    CHECK(ob_rexx_command(rx, "LASTERROR", result, sizeof result) == OB_RX_OK);
    CHECK(!strcmp(result, "RUNSCRIPT: ReferenceError: Can't find variable: nope"));
    scriptAnswer = -1;
    CHECK(ob_rexx_command(rx, "RUNSCRIPT 1", result, sizeof result) == OB_RX_NOW);
    CHECK(ob_rexx_command(rx, "LASTERROR", result, sizeof result) == OB_RX_OK);
    CHECK(!strcmp(result, "RUNSCRIPT: JavaScript is off"));
    scriptAnswer = 1;
    CHECK(ob_rexx_command(rx, "HELP", result, sizeof result) == OB_RX_OK);
    CHECK(strstr(result, "GETSOURCE") && strstr(result, "SAVE") && strstr(result, "RUNSCRIPT") && strstr(result, "PRINT"));
}

int main(void)
{
    struct page page;
    ob_rexx_host host;
    ob_rexx rx;
    char result[256], small[8];

    memset(&page, 0, sizeof page);
    page.load_ms = -1;
    memset(&host, 0, sizeof host);
    host.user = &page;
    host.version = "GFOpenBrowser 0.9.0 (17.11.2026)";
    host.open = p_open;
    host.reload = p_reload;
    host.stop = p_stop;
    host.back = p_back;
    host.forward = p_forward;
    host.url = p_url;
    host.title = p_title;
    host.loading = p_loading;
    host.load_ms = p_load_ms;
    host.load_count = p_load_count;
    host.text = p_text;
    host.activate = p_activate;
    host.fault = p_fault;
    host.quit = p_quit;
    ob_rexx_init(&rx, &host);

    /* names in any case, as whole words */
    CHECK(ob_rexx_command(&rx, "version", result, sizeof result) == OB_RX_OK);
    CHECK(!strcmp(result, "GFOpenBrowser 0.9.0 (17.11.2026)"));
    CHECK(ob_rexx_command(&rx, "  VERSION  ", result, sizeof result) == OB_RX_OK);
    CHECK(ob_rexx_command(&rx, "VERSIONS", result, sizeof result) == OB_RX_FAIL);
    CHECK(!result[0]);
    CHECK(ob_rexx_command(&rx, "", result, sizeof result) == OB_RX_FAIL);
    CHECK(ob_rexx_command(&rx, NULL, result, sizeof result) == OB_RX_FAIL);

    /* OPEN and OPENURL: plain, quoted, with URL or URL= before it */
    CHECK(ob_rexx_command(&rx, "OPEN https://aminet.net/", result, sizeof result) == OB_RX_OK);
    CHECK(!strcmp(page.opened, "https://aminet.net/"));
    CHECK(ob_rexx_command(&rx, "OpenURL \"https://example.com/a b\"  ", result, sizeof result) == OB_RX_OK);
    CHECK(!strcmp(page.opened, "https://example.com/a b"));
    CHECK(ob_rexx_command(&rx, "OPEN URL=https://www.bbc.co.uk/", result, sizeof result) == OB_RX_OK);
    CHECK(!strcmp(page.opened, "https://www.bbc.co.uk/"));
    CHECK(ob_rexx_command(&rx, "OPEN URL \"about:blank\"", result, sizeof result) == OB_RX_OK);
    CHECK(!strcmp(page.opened, "about:blank"));
    /* an address that only starts with the keyword's letters is an address */
    CHECK(ob_rexx_command(&rx, "OPEN urlencode.example/", result, sizeof result) == OB_RX_OK);
    CHECK(!strcmp(page.opened, "urlencode.example/"));
    CHECK(ob_rexx_command(&rx, "OPEN", result, sizeof result) == OB_RX_ERROR);
    CHECK(ob_rexx_command(&rx, "LASTERROR", result, sizeof result) == OB_RX_OK);
    CHECK(!strcmp(result, "OPEN needs an address"));
    /* LASTERROR keeps the reason until a command works */
    CHECK(ob_rexx_command(&rx, "LASTERROR", result, sizeof result) == OB_RX_OK && result[0]);
    CHECK(ob_rexx_command(&rx, "VERSION", result, sizeof result) == OB_RX_OK);
    CHECK(ob_rexx_command(&rx, "LASTERROR", result, sizeof result) == OB_RX_OK && !result[0]);
    {
        char longCommand[2400];
        memset(longCommand, 'a', sizeof longCommand);
        memcpy(longCommand, "OPEN ", 5);
        longCommand[sizeof longCommand - 1] = 0;
        CHECK(ob_rexx_command(&rx, longCommand, result, sizeof result) == OB_RX_ERROR);
        CHECK(!strcmp(page.opened, "urlencode.example/"));
    }

    /* the buttons */
    CHECK(ob_rexx_command(&rx, "RELOAD", result, sizeof result) == OB_RX_OK && page.reloads == 1);
    CHECK(ob_rexx_command(&rx, "STOP", result, sizeof result) == OB_RX_OK && page.stops == 1);
    CHECK(ob_rexx_command(&rx, "BACK", result, sizeof result) == OB_RX_NOW && page.backs == 1);
    CHECK(ob_rexx_command(&rx, "LASTERROR", result, sizeof result) == OB_RX_OK);
    CHECK(!strcmp(result, "BACK: no page to go back to"));
    page.back_ok = page.forward_ok = 1;
    CHECK(ob_rexx_command(&rx, "BACK", result, sizeof result) == OB_RX_OK);
    CHECK(ob_rexx_command(&rx, "forward", result, sizeof result) == OB_RX_OK && page.forwards == 1);

    /* what the page is */
    CHECK(ob_rexx_command(&rx, "GETURL", result, sizeof result) == OB_RX_OK);
    CHECK(!strcmp(result, "https://example.com/"));
    CHECK(ob_rexx_command(&rx, "GETTITLE", result, sizeof result) == OB_RX_OK);
    CHECK(!strcmp(result, "Example Domain"));
    CHECK(ob_rexx_command(&rx, "GETTEXT", result, sizeof result) == OB_RX_OK);
    CHECK(!strcmp(result, "This domain is for use in examples."));
    /* an answer is cut to the room there is, and always ends */
    CHECK(ob_rexx_command(&rx, "GETTEXT", small, sizeof small) == OB_RX_OK);
    CHECK(!strcmp(small, "This do"));
    CHECK(ob_rexx_command(&rx, "GETTITLE", small, sizeof small) == OB_RX_OK);
    CHECK(!strcmp(small, "Example"));

    /* load state and time */
    page.loading = 1;
    page.percent = 40;
    CHECK(ob_rexx_command(&rx, "STATUS", result, sizeof result) == OB_RX_OK);
    CHECK(!strcmp(result, "LOADING 40"));
    page.percent = 100;
    CHECK(ob_rexx_command(&rx, "STATUS", result, sizeof result) == OB_RX_OK);
    CHECK(!strcmp(result, "LOADING 99"));
    CHECK(ob_rexx_command(&rx, "LOADTIME", result, sizeof result) == OB_RX_NOW);
    CHECK(!result[0]);
    page.loading = 0;
    page.load_ms = 7100;
    CHECK(ob_rexx_command(&rx, "STATUS", result, sizeof result) == OB_RX_OK);
    CHECK(!strcmp(result, "DONE 100"));
    CHECK(ob_rexx_command(&rx, "LOADTIME", result, sizeof result) == OB_RX_OK);
    CHECK(!strcmp(result, "7100"));
    /* finished loads counted, for a script that waits for its own OPEN */
    CHECK(ob_rexx_command(&rx, "LOADCOUNT", result, sizeof result) == OB_RX_OK);
    CHECK(!strcmp(result, "0"));
    page.loads = 3;
    CHECK(ob_rexx_command(&rx, "loadcount", result, sizeof result) == OB_RX_OK);
    CHECK(!strcmp(result, "3"));

    /* the window, FAULT, HELP, QUIT */
    CHECK(ob_rexx_command(&rx, "ACTIVATE", result, sizeof result) == OB_RX_OK && page.front == 1);
    CHECK(ob_rexx_command(&rx, "DEACTIVATE", result, sizeof result) == OB_RX_OK && page.front == -1);
    CHECK(ob_rexx_command(&rx, "FAULT 205", result, sizeof result) == OB_RX_OK);
    CHECK(!strcmp(result, "object not found"));
    CHECK(ob_rexx_command(&rx, "FAULT", result, sizeof result) == OB_RX_ERROR);
    CHECK(ob_rexx_command(&rx, "FAULT 1", result, sizeof result) == OB_RX_NOW);
    CHECK(ob_rexx_command(&rx, "HELP", result, sizeof result) == OB_RX_OK);
    CHECK(strstr(result, "LOADTIME") && strstr(result, "QUIT"));
    CHECK(ob_rexx_command(&rx, "QUIT", result, sizeof result) == OB_RX_OK && page.quits == 1);

    /* with no page behind it every command answers, none crashes */
    ob_rexx_init(&rx, NULL);
    {
        static const char *const all[] = { "OPEN x", "RELOAD", "STOP", "BACK", "FORWARD", "GETURL", "GETTITLE", "STATUS",
            "LOADTIME", "LOADCOUNT", "GETTEXT", "ACTIVATE", "DEACTIVATE", "FAULT 205", "QUIT" };
        unsigned i;
        for (i = 0; i < sizeof all / sizeof all[0]; i++)
            CHECK(ob_rexx_command(&rx, all[i], result, sizeof result) == OB_RX_NOW);
        CHECK(ob_rexx_command(&rx, "VERSION", result, sizeof result) == OB_RX_OK && !strcmp(result, "GFOpenBrowser"));
        CHECK(ob_rexx_command(&rx, "VERSION", NULL, 0) == OB_RX_OK);
        CHECK(ob_rexx_command(&rx, "GETTEXT", NULL, 0) == OB_RX_NOW);
    }

    /* save, source, run a script, print */
    {
        ob_rexx second;
        ob_rexx_init(&second, NULL);
        batch2(&second);
    }

    if (failures) {
        printf("test_ob_rexx: %d FAILED\n", failures);
        return 1;
    }
    printf("test_ob_rexx: PASS\n");
    return 0;
}
