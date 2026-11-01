/*
 * WebBrowserEngine: the one engine behind webbrowser.library. It opens the
 * public port WEBBROWSER.ENGINE and serves WBMessages (wb_protocol.h) from
 * any number of programs, each with its own views, until WBC_SHUTDOWN,
 * Ctrl-C, or IDLE_QUIT seconds with no view, browser window or waiting
 * request (its memory goes back; the next program starts it again).
 * ENV:WebBrowserIdleQuit sets those seconds, 0 for never. Between
 * messages it runs WebCore (its timers, the network's answers), so pages
 * go on loading while programs do other things.
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <exec/memory.h>
#include <devices/timer.h>
#include <dos/dos.h>
#include <dos/dosextens.h>
#include <dos/var.h>
#include <proto/exec.h>
#include <proto/dos.h>

#include "ob_webview.h"
#include "ob_browser.h"
#include "oam_stack.h"
#include "wb_protocol.h"
#include "libraries/webbrowser.h"

static const char version[] __attribute__((used)) = "$VER: WebBrowserEngine 1.0 (6.10.2026)";

#define MAX_VIEWS 16
#define MAX_WAITS 16
#define IDLE_QUIT 60

struct view {
    OBWebView *web;
    int width, height;
    int loading, failed, changed;
    struct Task *owner;
    LONG signal;
    char title[256];
    char url[1024];
};

static struct view views[MAX_VIEWS];
static struct WBMessage *waits[MAX_WAITS];
static ULONG waitUntil[MAX_WAITS];
static struct Task *engineTask;
static BYTE wakeSignal = -1;
static struct MsgPort *timerPort;
static struct timerequest *timerRequest;
static int timerPending;
static struct MsgPort *enginePort;
static int networkUp;                   /* bsdsocket.library opened */
static struct WBMessage *current;       /* the message being handled */

/* abort(), replaced: the programs waiting on the engine get WBERR_NOENGINE
 * instead of waiting for ever, and the crash says where it came from (the
 * return addresses on the stack that lie in our code, as offsets for the
 * link map), on the engine's output. */
static ULONG codeStart, codeEnd;

void abort(void)
{
    struct Task *task = FindTask(NULL);
    ULONG here = 0, *p = &here, *top = (ULONG *)task->tc_SPUpper;
    struct WBMessage *m;
    int n = 0, i;
    printf("WBENGINE_ABORT in %s:", task->tc_Node.ln_Name ? task->tc_Node.ln_Name : "?");
    for (; p < top && n < 160; p++)
        if (*p >= codeStart && *p < codeEnd) {
            printf(" %lx", (unsigned long)(*p - codeStart));
            n++;
        }
    printf("\n");
    fflush(stdout);
    if (task == engineTask && enginePort) {
        Forbid();
        RemPort(enginePort);
        Permit();
        while ((m = (struct WBMessage *)GetMsg(enginePort))) {
            m->result = WBERR_NOENGINE;
            ReplyMsg(&m->message);
        }
        for (i = 0; i < MAX_WAITS; i++)
            if (waits[i] && waits[i] != current) {
                waits[i]->result = WBERR_NOENGINE;
                ReplyMsg(&waits[i]->message);
                waits[i] = NULL;
            }
        if (current) {
            current->result = WBERR_NOENGINE;
            ReplyMsg(&current->message);
        }
    }
    Wait(SIGBREAKF_CTRL_C);
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

static ULONG seconds(void)
{
    struct DateStamp ds;
    DateStamp(&ds);
    return (ULONG)ds.ds_Days * 86400 + ds.ds_Minute * 60 + ds.ds_Tick / TICKS_PER_SECOND;
}

/* Seconds without work before the engine quits; 0 for never. */
static ULONG idleQuitSeconds(void)
{
    char text[16];
    long value;
    if (GetVar((CONST_STRPTR)"WebBrowserIdleQuit", (STRPTR)text, sizeof text, GVF_GLOBAL_ONLY) <= 0)
        return IDLE_QUIT;
    if (sscanf(text, "%ld", &value) != 1 || value < 0)
        return IDLE_QUIT;
    return (ULONG)value;
}

/* Nothing open and nobody waiting on us. */
static int idle(void)
{
    int i;
    for (i = 0; i < MAX_VIEWS; i++)
        if (views[i].web)
            return 0;
    for (i = 0; i < MAX_WAITS; i++)
        if (waits[i])
            return 0;
    return obb_signals() == 0;
}

static void notify(struct view *v)
{
    if (v->owner && v->signal >= 0)
        Signal(v->owner, 1UL << v->signal);
}

static void onInvalidate(void *context, int x, int y, int width, int height)
{
    struct view *v = context;
    (void)x; (void)y; (void)width; (void)height;
    v->changed = 1;
}

static void onTitle(void *context, const char *title)
{
    struct view *v = context;
    strncpy(v->title, title ? title : "", sizeof v->title - 1);
    notify(v);
}

static void onURL(void *context, const char *url)
{
    struct view *v = context;
    strncpy(v->url, url ? url : "", sizeof v->url - 1);
}

static void onLoading(void *context, int loading, int percent)
{
    struct view *v = context;
    (void)percent;
    v->loading = loading;
    if (!loading)
        notify(v);
}

static void onFailed(void *context, const char *url, const char *description)
{
    struct view *v = context;
    (void)url; (void)description;
    v->failed = 1;
}

static void wakeUp(void *context)
{
    (void)context;
    Signal(engineTask, 1UL << wakeSignal);
}

static struct view *viewFor(ULONG handle)
{
    if (!handle || handle > MAX_VIEWS || !views[handle - 1].web)
        return NULL;
    return &views[handle - 1];
}

static void copyOut(struct WBMessage *m, const char *text)
{
    LONG n = (LONG)strlen(text);
    if (m->data && m->dataSize > 0) {
        LONG k = n < m->dataSize - 1 ? n : m->dataSize - 1;
        memcpy(m->data, text, k);
        ((char *)m->data)[k] = 0;
    }
    m->result = n;
}

/* Answers the waits whose page is done or whose time is up. */
static void checkWaits(void)
{
    ULONG now = seconds();
    int i;
    for (i = 0; i < MAX_WAITS; i++) {
        struct WBMessage *m = waits[i];
        struct view *v;
        if (!m)
            continue;
        v = viewFor(m->view);
        if (!v || !v->loading || now >= waitUntil[i]) {
            m->result = !v ? WBERR_BADVIEW : v->loading ? WBERR_TIMEOUT : v->failed ? 1 : 0;
            waits[i] = NULL;
            ReplyMsg(&m->message);
        }
    }
}

/* Handles one message; 0 when its reply waits (WBC_WAIT), -1 to stop. */
static int handle(struct WBMessage *m)
{
    struct view *v = viewFor(m->view);
    int i;
    m->result = 0;
    switch (m->command) {
    case WBC_PING:
        break;
    case WBC_OPEN: {
        OBWebViewCallbacks callbacks;
        for (i = 0; i < MAX_VIEWS && views[i].web; i++)
            ;
        if (i == MAX_VIEWS) {
            m->result = WBERR_NOMEMORY;
            break;
        }
        v = &views[i];
        memset(v, 0, sizeof *v);
        memset(&callbacks, 0, sizeof callbacks);
        callbacks.context = v;
        callbacks.invalidate = onInvalidate;
        callbacks.title = onTitle;
        callbacks.url = onURL;
        callbacks.loading = onLoading;
        callbacks.failed = onFailed;
        v->width = m->arg[0] > 0 ? m->arg[0] : 640;
        v->height = m->arg[1] > 0 ? m->arg[1] : 480;
        v->owner = m->task;
        v->signal = m->arg[4];
        v->web = ob_webview_create(v->width, v->height, &callbacks);
        if (!v->web) {
            m->result = WBERR_NOMEMORY;
            break;
        }
        ob_webview_set_scripts(v->web, m->arg[2]);
        ob_webview_set_pictures(v->web, m->arg[3]);
        m->result = i + 1;
        break;
    }
    case WBC_CLOSE:
        if (v) {
            for (i = 0; i < MAX_WAITS; i++)
                if (waits[i] && waits[i]->view == m->view) {
                    waits[i]->result = WBERR_BADVIEW;
                    ReplyMsg(&waits[i]->message);
                    waits[i] = NULL;
                }
            ob_webview_destroy(v->web);
            v->web = NULL;
        }
        break;
    case WBC_LOAD:
        if (!v || !m->text) {
            m->result = WBERR_BADVIEW;
            break;
        }
        v->loading = 1;
        v->failed = 0;
        ob_webview_load(v->web, m->text);
        break;
    case WBC_LOADHTML:
        if (!v || !m->text) {
            m->result = WBERR_BADVIEW;
            break;
        }
        v->loading = 1;
        v->failed = 0;
        ob_webview_load_html(v->web, m->text, m->text2 ? m->text2 : "about:blank");
        break;
    case WBC_STATE:
        if (!v) {
            m->result = WBERR_BADVIEW;
            break;
        }
        m->result = (v->loading ? WBS_LOADING : 0) | (v->failed ? WBS_FAILED : 0) | (v->changed ? WBS_CHANGED : 0);
        break;
    case WBC_WAIT:
        if (!v) {
            m->result = WBERR_BADVIEW;
            break;
        }
        if (!v->loading) {
            m->result = v->failed ? 1 : 0;
            break;
        }
        for (i = 0; i < MAX_WAITS && waits[i]; i++)
            ;
        if (i == MAX_WAITS) {
            m->result = WBERR_NOMEMORY;
            break;
        }
        waits[i] = m;
        waitUntil[i] = seconds() + (m->arg[0] > 0 ? (ULONG)m->arg[0] : 60);
        return 0;
    case WBC_RENDER:
        if (!v || !m->data) {
            m->result = WBERR_BADVIEW;
            break;
        }
        ob_webview_paint(v->web, m->data, m->arg[0], m->arg[1], m->arg[2], m->arg[3], m->arg[4]);
        v->changed = 0;
        break;
    case WBC_TEXT:
        if (!v) {
            m->result = WBERR_BADVIEW;
            break;
        }
        m->result = ob_webview_text(v->web, m->data, m->dataSize);
        break;
    case WBC_TITLE:
        if (v)
            copyOut(m, v->title);
        else
            m->result = WBERR_BADVIEW;
        break;
    case WBC_URL:
        if (v)
            copyOut(m, v->url);
        else
            m->result = WBERR_BADVIEW;
        break;
    case WBC_RESIZE:
        if (v) {
            v->width = m->arg[0];
            v->height = m->arg[1];
            ob_webview_resize(v->web, v->width, v->height);
        }
        break;
    case WBC_MOUSE:
        if (v)
            ob_webview_mouse(v->web, m->arg[0], m->arg[1], m->arg[2], m->arg[3], 0, 1);
        break;
    case WBC_KEY:
        if (v)
            ob_webview_key(v->web, m->arg[0], m->arg[1], m->text, m->arg[2]);
        break;
    case WBC_SCROLL:
        if (v)
            ob_webview_wheel(v->web, v->width / 2, v->height / 2, m->arg[0], m->arg[1], 0);
        break;
    case WBC_BROWSER:
        m->result = obb_open(m->text, m->arg[0], m->arg[1], m->arg[2], m->arg[3], networkUp) ? 0 : WBERR_NOMEMORY;
        break;
    case WBC_SHUTDOWN:
        return -1;
    }
    return 1;
}

/* wait: in microseconds, negative for no timer. */
static void armTimer(long wait)
{
    if (timerPending) {
        AbortIO((struct IORequest *)timerRequest);
        WaitIO((struct IORequest *)timerRequest);
        timerPending = 0;
    }
    if (wait < 0 || wait > 1000000)
        wait = 1000000;                         /* the waits' time limits are checked each second */
    if (wait < 5000)
        wait = 5000;
    timerRequest->tr_node.io_Command = TR_ADDREQUEST;
    timerRequest->tr_time.tv_secs = (ULONG)wait / 1000000UL;
    timerRequest->tr_time.tv_micro = (ULONG)wait % 1000000UL;
    SendIO((struct IORequest *)timerRequest);
    timerPending = 1;
}

static int serve(int argc, char **argv)
{
    struct MsgPort *port;
    int running = 1, i, network;
    ULONG idleLimit = idleQuitSeconds(), idleSince = seconds();
    (void)argc;
    (void)argv;
    engineTask = FindTask(NULL);
    wakeSignal = AllocSignal(-1);
    timerPort = CreateMsgPort();
    timerRequest = timerPort ? (struct timerequest *)CreateIORequest(timerPort, sizeof *timerRequest) : NULL;
    if (wakeSignal < 0 || !timerRequest || OpenDevice((CONST_STRPTR)TIMERNAME, UNIT_MICROHZ, (struct IORequest *)timerRequest, 0))
        return 20;
    network = networkUp = ob_webcore_init_with_network("PROGDIR:Cookies.db");
    if (!network && !ob_webcore_init())
        return 20;
    if (network)
        ob_webview_load_tls_sessions("PROGDIR:TLSSessions");
    ob_webcore_set_wakeup(wakeUp, NULL);

    Forbid();
    if (FindPort((CONST_STRPTR)WEBBROWSER_ENGINE_PORT)) {    /* one engine only */
        Permit();
        return 5;
    }
    port = CreateMsgPort();
    if (port) {
        port->mp_Node.ln_Name = (char *)WEBBROWSER_ENGINE_PORT;
        port->mp_Node.ln_Pri = 0;
        AddPort(port);
        enginePort = port;
    }
    Permit();
    if (!port)
        return 20;

    while (running) {
        struct WBMessage *m;
        long next;
        ULONG got;
        ob_webcore_cycle();
        while ((m = (struct WBMessage *)GetMsg(port))) {
            int done;
            current = m;
            done = handle(m);
            current = NULL;
            if (done < 0) {
                running = 0;
                ReplyMsg(&m->message);
                break;
            }
            if (done)
                ReplyMsg(&m->message);
        }
        checkWaits();
        obb_update();
        if (!running)
            break;
        if (!idle())
            idleSince = seconds();
        else if (idleLimit && seconds() - idleSince >= idleLimit)
            break;
        next = ob_webcore_next_timer_micros();
        if (next == 0)
            continue;
        armTimer(next);
        got = Wait((1UL << port->mp_SigBit) | (1UL << wakeSignal) | (1UL << timerPort->mp_SigBit) | obb_signals()
            | SIGBREAKF_CTRL_C);
        if (got & SIGBREAKF_CTRL_C)
            running = 0;
        if (timerPending && CheckIO((struct IORequest *)timerRequest)) {
            WaitIO((struct IORequest *)timerRequest);
            timerPending = 0;
        }
    }

    /* Leave: no new messages, then answer what is still here. */
    Forbid();
    RemPort(port);
    enginePort = NULL;
    Permit();
    {
        struct WBMessage *m;
        while ((m = (struct WBMessage *)GetMsg(port))) {
            m->result = WBERR_NOENGINE;
            ReplyMsg(&m->message);
        }
    }
    for (i = 0; i < MAX_WAITS; i++)
        if (waits[i]) {
            waits[i]->result = WBERR_NOENGINE;
            ReplyMsg(&waits[i]->message);
        }
    for (i = 0; i < MAX_VIEWS; i++)
        if (views[i].web)
            ob_webview_destroy(views[i].web);
    obb_close();
    DeleteMsgPort(port);
    if (timerPending) {
        AbortIO((struct IORequest *)timerRequest);
        WaitIO((struct IORequest *)timerRequest);
    }
    CloseDevice((struct IORequest *)timerRequest);
    DeleteIORequest((struct IORequest *)timerRequest);
    DeleteMsgPort(timerPort);
    if (network) {
        ob_webview_save_tls_sessions("PROGDIR:TLSSessions");
        ob_webcore_shutdown();
    } else
        ob_webcore_stop_threads();
    return 0;
}

int main(int argc, char **argv)
{
    (void)version;
    findCode();
    return oam_run_with_stack(2 * 1024 * 1024, serve, argc, argv);
}
