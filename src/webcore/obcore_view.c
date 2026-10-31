/*
 * obcore-view: load a page into an OpenBrowser view without a window, run
 * WebCore until the page has loaded, then paint the view into a 32-bit
 * buffer and save it as PNG. A test of the browser's page layer (frame
 * loader, chrome and editor clients, run loop, painting) from C.
 *
 *   obcore-view <file.html> [width height] [page.png]
 *   obcore-view -url <address> [width height] [page.png]
 *   obcore-view -dl ...   also paint through WebKit's display list (twice:
 *                         the second time with caches warm), report the
 *                         commands and timings, and save the replayed
 *                         picture as page-dl.png beside page.png
 *   obcore-view -text ...  also print the page's text (OBVIEW_TEXT, cut
 *                         to 2000 bytes), for pages that show a score
 *   obcore-view -wait N ...    run the page for at most N seconds (120)
 *   obcore-view -settle N ...  paint once the load is over and the network
 *                         has been quiet for N seconds (2; 0 = only when no
 *                         timer is due), see run()
 *
 * If the run loop stops turning for 20 s (a thread waits on another that
 * never answers), a watchdog prints OBVIEW_STALL, then one OBVIEW_THREAD
 * line per task (the main one first): its state, signals (waited for,
 * received, allocated), priority, stack, and the return addresses on its
 * stack that lie in our code, as offsets for the link map.
 *
 * At the end it prints one OBVIEW_BENCH line of timings and memory for
 * tools/bench (see docs/BENCHMARKS.md and the block marked "Benchmark"),
 * with how long the page took to paint and what painted it (OpenGPU's draws
 * and batches and the calls cairo drew, ob_webview_paint_report()).
 *
 * MIT, Copyright (c) 2026 AmigaChrome Team.
 */
#include <stdio.h>
#include <time.h>
#include <sys/time.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <cairo.h>
#include <proto/dos.h>
#include <proto/exec.h>
#include <dos/dosextens.h>
#include <dos/dostags.h>
#include <exec/execbase.h>
#include <exec/memory.h>
#include <unistd.h>

#include "ob_webview.h"
#include "oam_stack.h"

static const char version[] __attribute__((used)) = "$VER: obcore-view 0.2 (23.10.2026)";

/* abort(), replaced so a crash says where it came from: the return
 * addresses on the stack that lie in our code, as offsets for the link map. */
static ULONG codeStart, codeEnd;
static void stopWatchdog(void);

void abort(void)
{
    struct Task *task = FindTask(NULL);
    ULONG here = 0, *p = &here, *top = (ULONG *)task->tc_SPUpper;
    int n = 0;
    printf("OBVIEW_ABORT in %s:", task->tc_Node.ln_Name ? task->tc_Node.ln_Name : "?");
    for (; p < top && n < 160; p++)
        if (*p >= codeStart && *p < codeEnd) {
            printf(" %lx", (unsigned long)(*p - codeStart));
            n++;
        }
    printf("\n");
    fflush(stdout);
    Wait(SIGBREAKF_CTRL_C);
    stopWatchdog();
    _exit(20);
}

static void findCode(void)
{
    struct Process *process = (struct Process *)FindTask(NULL);
    BPTR segList = 0;
    if (process->pr_CLI)
        segList = ((struct CommandLineInterface *)BADDR(process->pr_CLI))->cli_Module;
    if (!segList && process->pr_SegList)
        segList = ((BPTR *)BADDR(process->pr_SegList))[3];
    if (segList) {
        ULONG *segment = BADDR(segList);
        codeStart = (ULONG)(segment + 1);
        codeEnd = (ULONG)segment - 4 + segment[-1];
    }
}

/* ---- Stall watchdog ---- */
static volatile ULONG heartbeat;        /* run() counts its turns here */
static volatile int watchdogQuit;
static struct Task *mainTask;
static struct Task *volatile watchdogTask;
static struct Task *volatile watchdogStopper;   /* told when the watchdog has gone */
static BPTR watchdogOutput;
static char dumpText[32768];
static int dumpLength;

static void dumpPut(const char *text)
{
    while (*text && dumpLength < (int)sizeof dumpText - 1)
        dumpText[dumpLength++] = *text++;
}

static void dumpHex(ULONG value)
{
    char digits[12];
    int i = 0, n;
    do {
        n = value & 15;
        digits[i++] = n < 10 ? '0' + n : 'a' + n - 10;
        value >>= 4;
    } while (value);
    while (i && dumpLength < (int)sizeof dumpText - 1)
        dumpText[dumpLength++] = digits[--i];
}

/* One task, called inside Forbid(): it is not running, so its stack from
 * tc_SPReg up is what it left when it last switched out. When tc_SPReg is
 * outside the stack exec knows of, 64 KB above it are searched instead. */
static void dumpTask(struct Task *task)
{
    ULONG *p = (ULONG *)task->tc_SPReg, *top = (ULONG *)task->tc_SPUpper;
    int n = 0, most = task == mainTask ? 96 : 40;
    if (p < (ULONG *)task->tc_SPLower || p >= top)
        top = p + 65536 / sizeof(ULONG);
    dumpPut("OBVIEW_THREAD ");
    dumpPut(task->tc_Node.ln_Name ? task->tc_Node.ln_Name : "?");
    dumpPut(task == mainTask ? " (main)" : "");
    dumpPut(" state=");
    dumpHex(task->tc_State);
    dumpPut(" sigwait=");
    dumpHex(task->tc_SigWait);
    dumpPut(" sigrecvd=");
    dumpHex(task->tc_SigRecvd);
    dumpPut(" sigalloc=");
    dumpHex(task->tc_SigAlloc);
    dumpPut(" pri=");
    dumpHex((UBYTE)task->tc_Node.ln_Pri);
    dumpPut(" sp=");
    dumpHex((ULONG)task->tc_SPReg);
    dumpPut(" stack=");
    dumpHex((ULONG)task->tc_SPLower);
    dumpPut("-");
    dumpHex((ULONG)task->tc_SPUpper);
    dumpPut(":");
    for (; p && p < top && n < most; p++)
        if (*p >= codeStart && *p < codeEnd) {
            dumpPut(" ");
            dumpHex(*p - codeStart);
            n++;
        }
    dumpPut("\n");
}

static void dumpThreads(void)
{
    struct Node *node;
    dumpLength = 0;
    Forbid();
    /* Every task: the program's threads have many names (WebCore, JSC,
     * WTF work queues, libpthread's own). The main one first. */
    dumpTask(mainTask);
    for (node = SysBase->TaskWait.lh_Head; node->ln_Succ; node = node->ln_Succ)
        if ((struct Task *)node != mainTask)
            dumpTask((struct Task *)node);
    for (node = SysBase->TaskReady.lh_Head; node->ln_Succ; node = node->ln_Succ)
        if ((struct Task *)node != mainTask)
            dumpTask((struct Task *)node);
    Permit();
}

/* A process of its own (it writes with dos.library), polling once a second. */
static void watchdog(void)
{
    ULONG seen = heartbeat;
    int still = 0, dumped = 0;
    while (!watchdogQuit) {
        Delay(TICKS_PER_SECOND);
        if (heartbeat != seen) {
            seen = heartbeat;
            still = 0;
            continue;
        }
        if (++still == 20 && !dumped) {
            dumped = 1;
            dumpThreads();
            Write(watchdogOutput, (APTR)"OBVIEW_STALL run loop still for 20 s\n", sizeof "OBVIEW_STALL run loop still for 20 s\n" - 1);
            Write(watchdogOutput, dumpText, dumpLength);
        }
    }
    Forbid();
    watchdogTask = NULL;
    Signal(watchdogStopper, SIGBREAKF_CTRL_F);
}

static void startWatchdog(void)
{
    mainTask = FindTask(NULL);
    watchdogOutput = Output();
    watchdogTask = (struct Task *)CreateNewProcTags(NP_Entry, (ULONG)watchdog, NP_Name, (ULONG)"obcore-view watchdog",
        NP_StackSize, 16384, NP_Output, (ULONG)watchdogOutput, NP_CloseOutput, FALSE, NP_Input, 0, NP_CloseInput, FALSE,
        TAG_DONE);
}

static void stopWatchdog(void)
{
    Forbid();
    if (!watchdogTask || watchdogStopper) {
        Permit();
        return;
    }
    watchdogStopper = FindTask(NULL);
    SetSignal(0, SIGBREAKF_CTRL_F);
    watchdogQuit = 1;
    Permit();
    while (watchdogTask)
        Wait(SIGBREAKF_CTRL_F);
}

static const char *tlsFile;  /* -tls: TLS sessions kept across runs */
static int loading = -1;   /* -1 not started yet, 1 loading, 0 done */
static int invalidations;
static long busyCycles, waits;  /* run loop turns with work waiting, and waits */
static time_t startTime;        /* for the seconds in OBVIEW_CREATED and OBVIEW_RAN */
static double settleSeconds = 2.0;  /* -settle: network quiet this long after the load counts as done */
static long lastNetworkMs;          /* benchNow() of the last request started or ended, or of the load's end */
static int settledSaid;

/* Benchmark (tools/bench): milliseconds since the program started for each
 * step, the requests, and free memory as AvailMem(MEMF_ANY) sees it. All of
 * it is printed once, as OBVIEW_BENCH, at the end; -1 means it did not
 * happen. Free memory is sampled at most every 100 ms while the page runs,
 * so avail_min (and the peak use worked out from it) can miss short peaks. */
static struct timeval benchStart;
static long benchInit = -1, benchLoad = -1, benchCommit = -1, benchFirstLoaded = -1, benchFirstInvalidate = -1,
    benchLoadDone = -1, benchLastResource = -1, benchPainted = -1, benchLastSample = -1000;
static long benchRequests, benchLoaded, benchFailed;
static unsigned long availStart, availReady, availMin, availPainted, largestStart;
static long benchPaintMs = -1;
static char benchPainters[256];

static long benchNow(void)
{
    struct timeval now;
    gettimeofday(&now, NULL);
    return (now.tv_sec - benchStart.tv_sec) * 1000L + (now.tv_usec - benchStart.tv_usec) / 1000L;
}

static void benchSample(int always)
{
    long now = benchNow();
    unsigned long avail;
    if (!always && now - benchLastSample < 100)
        return;
    benchLastSample = now;
    avail = AvailMem(MEMF_ANY);
    if (!availMin || avail < availMin)
        availMin = avail;
}

static void benchReport(unsigned long bytes)
{
    printf("OBVIEW_BENCH init_ms=%ld load_ms=%ld commit_ms=%ld first_loaded_ms=%ld first_invalidate_ms=%ld"
        " load_done_ms=%ld last_resource_ms=%ld painted_ms=%ld end_ms=%ld requests=%ld loaded=%ld failed=%ld bytes=%lu"
        " avail_start=%lu avail_ready=%lu avail_min=%lu avail_painted=%lu avail_end=%lu largest_start=%lu"
        " paint_ms=%ld%s\n",
        benchInit, benchLoad, benchCommit, benchFirstLoaded, benchFirstInvalidate, benchLoadDone, benchLastResource,
        benchPainted, benchNow(), benchRequests, benchLoaded, benchFailed, bytes, availStart, availReady, availMin,
        availPainted, (unsigned long)AvailMem(MEMF_ANY), largestStart, benchPaintMs, benchPainters);
    fflush(stdout);
}

static void onInvalidate(void *context, int x, int y, int w, int h)
{
    (void)context; (void)x; (void)y; (void)w; (void)h;
    invalidations++;
    if (benchFirstInvalidate < 0)
        benchFirstInvalidate = benchNow();
}

static void onTitle(void *context, const char *title)
{
    (void)context;
    printf("OBVIEW_TITLE %s\n", title);
}

static void onURL(void *context, const char *url)
{
    (void)context;
    if (benchCommit < 0)
        benchCommit = benchNow();
    printf("OBVIEW_URL %s\n", url);
}

static void onLoading(void *context, int isLoading, int percent)
{
    (void)context;
    if (isLoading != loading || !isLoading)
        printf("OBVIEW_LOADING %d %d%%\n", isLoading, percent);
    if (!isLoading && loading == 1)
        benchLoadDone = lastNetworkMs = benchNow();
    loading = isLoading;
}

static void onFailed(void *context, const char *url, const char *description)
{
    (void)context;
    printf("OBVIEW_FAILED %s: %s\n", url, description);
}

static void onAlert(void *context, const char *message)
{
    (void)context;
    printf("OBVIEW_ALERT %s\n", message);
}

static void onConsole(void *context, const char *message, int line, const char *source)
{
    (void)context;
    printf("OBVIEW_CONSOLE %s (%s:%d)\n", message, source, line);
}

/* Milliseconds since the program started, for the resource lines. */
static long sinceStart(void)
{
    static struct timeval first;
    struct timeval now;
    gettimeofday(&now, NULL);
    if (!first.tv_sec)
        first = now;
    return (now.tv_sec - first.tv_sec) * 1000L + (now.tv_usec - first.tv_usec) / 1000L;
}

static void onResource(void *context, const char *url, int started, const char *error)
{
    (void)context;
    lastNetworkMs = benchNow();
    if (started)
        benchRequests++;
    else {
        benchLastResource = lastNetworkMs;
        if (benchFirstLoaded < 0)
            benchFirstLoaded = benchLastResource;
        if (error)
            benchFailed++;
        else
            benchLoaded++;
    }
    benchSample(0);
    printf("OBVIEW_%s [%ld ms] %.200s%s%s\n", started ? "REQUEST" : "LOADED", sinceStart(), url, error ? " FAILED: " : "", error ? error : "");
    fflush(stdout);
}

static char *readFile(const char *name)
{
    FILE *f = fopen(name, "rb");
    long size;
    char *text;
    if (!f)
        return NULL;
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, 0, SEEK_SET);
    text = malloc(size + 1);
    if (text && fread(text, 1, size, f) == (size_t)size)
        text[size] = 0;
    else {
        free(text);
        text = NULL;
    }
    fclose(f);
    return text;
}

/* Requests asked for and not yet ended. A failure can be reported for a
 * request that never started (a blocked host), so this can go below 0. */
static long requestsOpen(void)
{
    return benchRequests - benchLoaded - benchFailed;
}

/* Whether the page is done: the load is over and either nothing is due
 * within 50 ms, or the network has settled: at most two requests open
 * (a long poll, a beacon) and none started or ended for -settle seconds.
 * A page with an endless animation or a short repeating timer (a loading
 * placeholder, a carousel, a script polling a flag) always has a timer
 * due, so without the second rule it ran until -wait ran out and was
 * painted only then (Reddit: loaded in 9 s, painted at 160 s), while a
 * window paints such a page at each rendering update. The rule is the
 * one browser test tools call "network idle". */
static int pageDone(double next)
{
    if (loading != 0)
        return 0;
    if (next < 0 || next > 0.05)
        return 1;
    if (settleSeconds <= 0 || requestsOpen() > 2 || benchNow() - lastNetworkMs < (long)(settleSeconds * 1000))
        return 0;
    if (!settledSaid) {
        settledSaid = 1;
        printf("OBVIEW_SETTLED network quiet %.1f s, %ld requests open, timers still running, at %ld ms\n", settleSeconds,
            requestsOpen() > 0 ? requestsOpen() : 0, benchNow());
        fflush(stdout);
    }
    return 1;
}

/* Runs WebCore's run loop for up to `seconds`, or until the page is done
 * (pageDone()), saying every 15 seconds how it goes. */
static void run(double seconds)
{
    time_t start = time(NULL), lastTick = start;
    while (difftime(time(NULL), start) < seconds) {
        double next;
        ob_webcore_cycle();
        heartbeat++;
        benchSample(0);
        next = ob_webcore_next_timer();
        if (pageDone(next))
            break;
        if (next != 0) {
            Delay(next < 0 || next > 0.2 ? 10 : (int)(next * 50) + 1);
            waits++;
        } else
            busyCycles++;
        if (difftime(time(NULL), lastTick) >= 15) {
            lastTick = time(NULL);
            printf("OBVIEW_TICK %lds loading=%d cycles=%ld waits=%ld\n", (long)(lastTick - start), loading, busyCycles, waits);
            fflush(stdout);
        }
    }
}

/* Intuition raw key codes of a US keyboard, for the input test. */
static int rawKeyFor(char c, int *shift)
{
    static const char *rows[] = { "1234567890-=", "qwertyuiop[]", "asdfghjkl;'", "zxcvbnm,./" };
    static const char *shifted[] = { "!@#$%^&*()_+", "QWERTYUIOP{}", "ASDFGHJKL:\"", "ZXCVBNM<>?" };
    static const int first[] = { 0x01, 0x10, 0x20, 0x31 };
    int row;
    *shift = 0;
    if (c == ' ')
        return 0x40;
    for (row = 0; row < 4; row++) {
        const char *at = strchr(rows[row], c);
        if (!at && (at = strchr(shifted[row], c)))
            *shift = OB_QUAL_SHIFT;
        if (at)
            return first[row] + (int)(at - (*shift ? shifted[row] : rows[row]));
    }
    return -1;
}

/* Clicks at (x, y) on the page, as the window does with the left button. */
static void click(OBWebView *view, int x, int y)
{
    ob_webview_mouse(view, OB_MOUSE_MOVE, x, y, OB_BUTTON_NONE, 0, 0);
    ob_webview_mouse(view, OB_MOUSE_DOWN, x, y, OB_BUTTON_LEFT, 0, 1);
    ob_webview_mouse(view, OB_MOUSE_UP, x, y, OB_BUTTON_LEFT, 0, 1);
}

/* Types text a key at a time, each key down and up, as the window does. */
static void typeText(OBWebView *view, const char *text)
{
    for (; *text; text++) {
        char one[2] = { *text, 0 };
        int shift, raw = rawKeyFor(*text, &shift);
        if (raw < 0)
            continue;
        ob_webview_key(view, 1, raw, one, shift);
        ob_webview_key(view, 0, raw, "", shift);
        ob_webcore_cycle();
    }
}

/* -dl: paint directly and through the display list, twice, then compare
 * the two pictures pixel by pixel and save the replayed one. */
static void reportDisplayList(OBWebView *view, int width, int height, const char *png)
{
    size_t size = (size_t)width * height * 4, i, differing = 0;
    unsigned char *direct = calloc(size, 1), *replayed = calloc(size, 1);
    int pass;
    if (!direct || !replayed) {
        printf("OBVIEW_DL no memory\n");
        free(direct);
        free(replayed);
        return;
    }
    for (pass = 1; pass <= 2; pass++) {
        printf("OBVIEW_DL_PASS %d\n", pass);
        ob_webview_report_display_list(view, direct, replayed, width * 4, 0, 0, width, height);
    }
    for (i = 0; i < size; i += 4) {
        if (memcmp(direct + i, replayed + i, 4))
            differing++;
    }
    printf("OBVIEW_DL_DIFF %lu of %lu pixels differ\n", (unsigned long)differing, (unsigned long)(size / 4));
    if (png) {
        char name[256];
        const char *dot = strrchr(png, '.');
        size_t stem = dot ? (size_t)(dot - png) : strlen(png);
        cairo_surface_t *surface;
        if (stem > sizeof(name) - 8)
            stem = sizeof(name) - 8;
        memcpy(name, png, stem);
        strcpy(name + stem, "-dl.png");
        surface = cairo_image_surface_create_for_data(replayed, CAIRO_FORMAT_ARGB32, width, height, width * 4);
        printf("OBVIEW_DL_PNG %s %s\n", name, cairo_status_to_string(cairo_surface_write_to_png(surface, name)));
        cairo_surface_destroy(surface);
    }
    fflush(stdout);
    free(direct);
    free(replayed);
}

/* A local file as the type its name says, so .xhtml, .svg and .xml go
 * through the XML parser as they would from a server. */
static void loadFile(OBWebView *view, const char *path, const char *text)
{
    static const struct { const char *ext, *mime, *base; } types[] = {
        { ".xhtml", "application/xhtml+xml", "file:///page.xhtml" },
        { ".svg", "image/svg+xml", "file:///page.svg" },
        { ".xml", "application/xml", "file:///page.xml" },
    };
    const char *dot = strrchr(path, '.');
    for (unsigned i = 0; dot && i < sizeof types / sizeof types[0]; i++) {
        if (!strcasecmp(dot, types[i].ext)) {
            ob_webview_load_data(view, text, types[i].mime, types[i].base);
            return;
        }
    }
    ob_webview_load_html(view, text, "file:///page.html");
}

static int viewMain(int argc, char **argv)
{
    OBWebViewCallbacks callbacks = { 0 };
    OBWebView *view;
    int width = 800, height = 600, argi = 1, isURL = 0, inputTest = 0, displayList = 0, showText = 0;
    unsigned long bytes = 0;
    double seconds = 120.0;
    const char *source, *png = NULL;
    unsigned char *pixels;
    char *html = NULL;

    if (argc > argi + 1 && !strcmp(argv[argi], "-wait")) {
        seconds = atof(argv[argi + 1]);
        argi += 2;
    }
    if (argc > argi + 1 && !strcmp(argv[argi], "-settle")) {
        settleSeconds = atof(argv[argi + 1]);
        argi += 2;
    }
    if (argc > argi + 1 && !strcmp(argv[argi], "-tls")) {
        tlsFile = argv[argi + 1];          /* TLS sessions: read before, saved after */
        argi += 2;
    }
    if (argc > argi && !strcmp(argv[argi], "-input")) {
        /* After the load: click at (30, 35), type an address, click at (30, 110). */
        inputTest = 1;
        argi++;
    }
    if (argc > argi && !strcmp(argv[argi], "-dl")) {
        displayList = 1;
        argi++;
    }
    if (argc > argi && !strcmp(argv[argi], "-text")) {
        showText = 1;
        argi++;
    }
    if (argc > argi && !strcmp(argv[argi], "-url")) {
        isURL = 1;
        argi++;
    }
    if (argc <= argi) {
        printf("usage: obcore-view [-wait seconds] [-settle seconds] [-tls file] [-input] [-dl] [-text] [-url] <file.html|address> [width height] [page.png]\n");
        return 10;
    }
    source = argv[argi++];
    if (argc > argi + 1) {
        width = atoi(argv[argi]);
        height = atoi(argv[argi + 1]);
        argi += 2;
    }
    if (argc > argi)
        png = argv[argi];
    if (width < 100 || height < 100) {
        width = 800;
        height = 600;
    }
    if (!isURL && !(html = readFile(source))) {
        printf("OBVIEW_FAIL cannot read %s\n", source);
        return 20;
    }

    if (isURL ? !ob_webcore_init_with_network(":memory:") : !ob_webcore_init()) {
        printf("OBVIEW_FAIL init\n");
        return 20;
    }
    sinceStart();
    benchInit = benchNow();
    availReady = AvailMem(MEMF_ANY);
    availMin = availReady;
    if (isURL && tlsFile)
        printf("OBVIEW_TLS %d sessions read\n", ob_webview_load_tls_sessions(tlsFile));
    callbacks.invalidate = onInvalidate;
    callbacks.title = onTitle;
    callbacks.url = onURL;
    callbacks.loading = onLoading;
    callbacks.failed = onFailed;
    callbacks.alert = onAlert;
    callbacks.console = onConsole;
    callbacks.resource = onResource;
    view = ob_webview_create(width, height, &callbacks);
    printf("OBVIEW_CREATED %dx%d after %lds\n", width, height, (long)(time(NULL) - startTime));
    fflush(stdout);

    benchLoad = benchNow();
    if (isURL)
        ob_webview_load(view, source);
    else
        loadFile(view, source, html);
    run(seconds);
    run(1.0); /* rendering updates and timers the load left behind */
    if (inputTest) {
        ob_webview_focus(view, 1);
        click(view, 30, 35);
        run(1.0);
        typeText(view, "someone@example.com");
        run(1.0);
        printf("OBVIEW_TYPED\n");
        click(view, 30, 110);
        run(3.0);
    }
    printf("OBVIEW_RAN loading=%d invalidations=%d cycles=%ld waits=%ld at %lds\n", loading, invalidations, busyCycles, waits,
        (long)(time(NULL) - startTime));
    fflush(stdout);

    if (showText) {
        char text[2001];
        int length = ob_webview_text(view, text, sizeof text), i;
        for (i = 0; text[i]; i++)
            if (text[i] == '\n' || text[i] == '\r' || text[i] == '\t')
                text[i] = ' ';
        printf("OBVIEW_TEXT %d %s\n", length, text);
        fflush(stdout);
    }
    if (displayList)
        reportDisplayList(view, width, height, png);
    pixels = calloc((size_t)width * height, 4);
    if (pixels) {
        long paintStart = benchNow();
        ob_webview_paint_report(view, NULL, 0);
        ob_webview_paint(view, pixels, width * 4, 0, 0, width, height);
        benchPainted = benchNow();
        benchPaintMs = benchPainted - paintStart;
        ob_webview_paint_report(view, benchPainters, sizeof benchPainters);
        benchSample(1);
        availPainted = AvailMem(MEMF_ANY);
        printf("OBVIEW_PAINTED %dx%d\n", width, height);
        if (png) {
            cairo_surface_t *surface = cairo_image_surface_create_for_data(pixels, CAIRO_FORMAT_ARGB32, width, height, width * 4);
            cairo_status_t status = cairo_surface_write_to_png(surface, png);
            printf("OBVIEW_PNG %s %s\n", png, cairo_status_to_string(status));
            cairo_surface_destroy(surface);
        }
        free(pixels);
    }
    bytes = ob_webview_bytes_received(view);
    ob_webview_destroy(view);
    if (isURL && tlsFile)
        ob_webview_save_tls_sessions(tlsFile);
    if (isURL)
        ob_webcore_shutdown();
    else
        ob_webcore_stop_threads();
    free(html);
    benchReport(bytes);
    printf("OBVIEW_DONE\n");
    fflush(stdout);
    return 0;
}

int main(int argc, char **argv)
{
    int result;
    findCode();
    startTime = time(NULL);
    gettimeofday(&benchStart, NULL);
    availStart = AvailMem(MEMF_ANY);
    largestStart = AvailMem(MEMF_ANY | MEMF_LARGEST);
    startWatchdog();
    result = oam_run_with_stack(2 * 1024 * 1024, viewMain, argc, argv);
    stopWatchdog();
    return result;
}
