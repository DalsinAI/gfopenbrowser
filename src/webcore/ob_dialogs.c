/*
 * GFOpenBrowser: the dialogs and panels in Intuition windows, through
 * gfopenlayout.library 11 (see ob_dialogs.h). Their contents and what their
 * widgets do are ob_dialogs_ui.c's, built here with olui's calls going to
 * the library.
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <exec/memory.h>
#include <dos/dos.h>
#include <intuition/intuition.h>
#include <workbench/workbench.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/wb.h>
#include <proto/gfopenlayout.h>

#include "ob_dialogs.h"
#include "ob_webview.h"

struct Library *OpenLayoutBase;

/* ob_dialogs_ui.c's olui calls, through the library */
#define olui_new OLUI_New
#define olui_free OLUI_Free
#define olui_load OLUI_Load
#define olui_on OLUI_On
#define olui_find OLUI_Find
#define olui_get_text OLUI_GetText
#define olui_set_text OLUI_SetText
#define olui_get_checked OLUI_GetChecked
#define olui_set_checked OLUI_SetChecked
#define olui_set_hidden OLUI_SetHidden
#define olui_set_progress OLUI_SetProgress
#define olui_layout OLUI_Layout
#define olui_box OLUI_Box
#define olui_focus OLUI_Focus
#define olui_add OLUI_Add
#define olui_remove OLUI_Remove
#include "ob_dialogs_ui.c"

static int libraryTried;

int obd_open(void)
{
    if (!libraryTried) {
        libraryTried = 1;
        OpenLayoutBase = OpenLibrary((CONST_STRPTR)OPENLAYOUTLIB_NAME, OPENLAYOUTLIB_DESKTOP_VERSION);
    }
    return OpenLayoutBase != NULL;
}

/* --- windows ---------------------------------------------------------------- */

/* A window over parent (centred on it), inner width by height. share: the
 * port it takes its messages on (the browser's), else its own. */
static struct Window *openOver(struct Window *parent, const char *title, int width, int height, int sizeable,
    struct MsgPort *share)
{
    struct Screen *screen = parent->WScreen;
    int left = parent->LeftEdge + (parent->Width - width) / 2;
    int top = parent->TopEdge + parent->BorderTop + 40;
    struct Window *win;
    if (left < 0)
        left = 0;
    win = OpenWindowTags(NULL, WA_Title, (ULONG)title, WA_CustomScreen, (ULONG)screen, WA_Left, left, WA_Top, top,
        WA_InnerWidth, width, WA_InnerHeight, height, WA_AutoAdjust, TRUE,
        WA_MinWidth, 200, WA_MinHeight, 80, WA_MaxWidth, -1, WA_MaxHeight, -1,
        WA_Flags, WFLG_CLOSEGADGET | WFLG_DRAGBAR | WFLG_DEPTHGADGET | WFLG_ACTIVATE | WFLG_SMART_REFRESH
            | WFLG_REPORTMOUSE | (sizeable ? WFLG_SIZEGADGET | WFLG_SIZEBBOTTOM : 0),
        WA_IDCMP, share ? 0 : OLUI_AmigaIdcmp(), TAG_DONE);
    if (win && share) {
        win->UserPort = share;
        ModifyIDCMP(win, OLUI_AmigaIdcmp());
    }
    return win;
}

/* A window on a shared port: its messages still waiting are replied first. */
static void closeShared(struct Window *win)
{
    struct IntuiMessage *msg;
    struct Node *next;
    Forbid();
    msg = (struct IntuiMessage *)win->UserPort->mp_MsgList.lh_Head;
    while ((next = msg->ExecMessage.mn_Node.ln_Succ)) {
        if (msg->IDCMPWindow == win) {
            Remove((struct Node *)msg);
            ReplyMsg((struct Message *)msg);
        }
        msg = (struct IntuiMessage *)next;
    }
    win->UserPort = NULL;
    ModifyIDCMP(win, 0);
    Permit();
    CloseWindow(win);
}

/* The height ui's #all needs at the window's inner width, and the window
 * made that tall. */
static void fitHeight(struct Window *win, olui *ui)
{
    olui_rect r;
    int inner = win->Width - win->BorderLeft - win->BorderRight, height;
    olui_el *all = OLUI_Find(ui, "all");
    if (!all || !OLUI_Layout(ui, inner, 2000) || !OLUI_Box(ui, all, &r))
        return;
    height = r.y + r.h + 12 + win->BorderTop + win->BorderBottom;
    if (height > win->WScreen->Height - win->TopEdge)
        height = win->WScreen->Height - win->TopEdge;
    if (height != win->Height)
        ChangeWindowBox(win, win->LeftEdge, win->TopEdge, win->Width, height);
}

/* --- dialogs that wait --------------------------------------------------------- */

/* Runs a dialog: ui (loaded, with answer's handlers) in a window over
 * parent, which takes no input meanwhile (an empty requester and the busy
 * pointer), until a button or Return answers, or Escape or the close
 * gadget cancels. focus: the widget with the keyboard at first. */
static int runModal(struct Window *parent, const char *title, olui *ui, obd_answer *answer, int width,
    const char *focus, void (*read)(olui *ui, void *data), void *data)
{
    struct Requester block;
    int blocked;
    olui_amiga *a;
    struct Window *win = openOver(parent, title, width, 120, 0, NULL);
    if (!win)
        return 0;
    a = OLUI_AmigaNew(ui, win);
    if (!a) {
        CloseWindow(win);
        return 0;
    }
    OLUI_AmigaTheme(a, NULL);
    InitRequester(&block);
    blocked = Request(&block, parent);
    SetWindowPointer(parent, WA_BusyPointer, TRUE, TAG_DONE);
    fitHeight(win, ui);
    if (focus)
        OLUI_Focus(ui, OLUI_Find(ui, focus));
    OLUI_AmigaLayout(a);

    while (!answer->done) {
        struct IntuiMessage *msg;
        WaitPort(win->UserPort);
        while (!answer->done && (msg = (struct IntuiMessage *)GetMsg(win->UserPort))) {
            int used;
            if (msg->Class == IDCMP_VANILLAKEY && msg->Code == 27) {
                answer->done = 1;
                answer->ok = 0;
                ReplyMsg((struct Message *)msg);
                break;
            }
            used = OLUI_AmigaEvent(a, msg);
            ReplyMsg((struct Message *)msg);
            if (used == OLUI_AMIGA_CLOSE) {
                answer->done = 1;
                answer->ok = 0;
            }
        }
        if (!answer->done)
            OLUI_AmigaDraw(a);
    }
    if (answer->ok && read)
        read(ui, data);

    SetWindowPointer(parent, TAG_DONE);
    if (blocked)
        EndRequest(&block, parent);
    OLUI_AmigaFree(a);
    CloseWindow(win);
    return answer->ok;
}

/* Without the library: Intuition's own requester, as before. */
static LONG easy(struct Window *parent, const char *title, const char *text, const char *buttons)
{
    struct EasyStruct es = { sizeof(struct EasyStruct), 0, (UBYTE *)title, (UBYTE *)"%s", (UBYTE *)buttons };
    char latin1[1024];
    const unsigned char *p = (const unsigned char *)text;
    int n = 0;
    while (*p && n < (int)sizeof latin1 - 1) {
        unsigned long c = *p++;
        if (c >= 0xc0 && c < 0xe0 && (*p & 0xc0) == 0x80)
            c = ((c & 0x1f) << 6) | (*p++ & 0x3f);
        else if (c >= 0xe0) {
            while ((*p & 0xc0) == 0x80)
                p++;
            c = '?';
        }
        latin1[n++] = c > 0xff ? '?' : (char)c;
    }
    latin1[n] = 0;
    return EasyRequest(parent, &es, NULL, (ULONG)latin1);
}

static int messageDialog(struct Window *parent, const char *host, const char *message, int cancel)
{
    obd_answer answer = { 0, 0 };
    olui *ui;
    char *markup;
    int ok;
    if (!obd_open() || !(ui = OLUI_New()))
        return -1;
    markup = obd_markup_message(host, message, 0, cancel);
    if (!obd_setup_modal(ui, markup, &answer)) {
        free(markup);
        OLUI_Free(ui);
        return -1;
    }
    free(markup);
    ok = runModal(parent, "GFOpenBrowser", ui, &answer, 400, "ok", NULL, NULL);
    OLUI_Free(ui);
    return ok;
}

void obd_alert(struct Window *parent, const char *host, const char *message)
{
    if (messageDialog(parent, host, message, 0) < 0)
        easy(parent, "The page says", message, "OK");
}

int obd_confirm(struct Window *parent, const char *host, const char *message)
{
    int ok = messageDialog(parent, host, message, 1);
    if (ok < 0)
        ok = easy(parent, "The page asks", message, "OK|Cancel") == 1;
    return ok;
}

typedef struct {
    char *out;
    int size;
    char *out2;
    int size2;
} obd_read;

static void readValue(olui *ui, void *data)
{
    obd_read *r = (obd_read *)data;
    obd_copy(r->out, r->size, OLUI_GetText(OLUI_Find(ui, "value")));
}

int obd_prompt(struct Window *parent, const char *host, const char *message, const char *defaultValue,
    char *result, int resultSize)
{
    obd_answer answer = { 0, 0 };
    obd_read r = { result, resultSize, NULL, 0 };
    olui *ui;
    olui_el *field;
    char *markup;
    int ok;
    if (!obd_open() || !(ui = OLUI_New()))
        return 0;
    markup = obd_markup_message(host, message, 1, 1);
    if (!obd_setup_modal(ui, markup, &answer)) {
        free(markup);
        OLUI_Free(ui);
        return 0;
    }
    free(markup);
    field = OLUI_Find(ui, "value");
    OLUI_SetText(ui, field, defaultValue ? defaultValue : "");
    OLUI_SetSelection(ui, field, 0, defaultValue ? strlen(defaultValue) : 0);
    ok = runModal(parent, "GFOpenBrowser", ui, &answer, 400, "value", readValue, &r);
    OLUI_Free(ui);
    return ok;
}

static void readLogin(olui *ui, void *data)
{
    obd_read *r = (obd_read *)data;
    obd_copy(r->out, r->size, OLUI_GetText(OLUI_Find(ui, "user")));
    obd_copy(r->out2, r->size2, OLUI_GetText(OLUI_Find(ui, "password")));
}

int obd_login(struct Window *parent, const char *host, const char *realm, int previousFailures,
    char *user, int userSize, char *password, int passwordSize)
{
    obd_answer answer = { 0, 0 };
    obd_read r = { user, userSize, password, passwordSize };
    olui *ui;
    char *markup;
    int ok;
    if (!obd_open() || !(ui = OLUI_New()))
        return 0;
    markup = obd_markup_login(host, realm, previousFailures);
    if (!obd_setup_modal(ui, markup, &answer)) {
        free(markup);
        OLUI_Free(ui);
        return 0;
    }
    free(markup);
    OLUI_SetText(ui, OLUI_Find(ui, "user"), user ? user : "");
    ok = runModal(parent, "Sign in", ui, &answer, 380, user && *user ? "password" : "user", readLogin, &r);
    OLUI_Free(ui);
    return ok;
}

/* --- the find bar ------------------------------------------------------------------ */

static struct {
    olui *ui;
    olui_amiga *a;
    struct Window *window;
    obd_find_fn fn;
    void *context;
    int height;
} find;

static void findEvent(olui *ui, const olui_event *ev, void *data)
{
    (void)data;
    if (!find.fn)
        return;
    if (!strcmp(ev->id, "text")) {
        if (ev->type == OLUI_CHANGE)
            find.fn(find.context, OBD_FIND_TYPED, OLUI_GetText(ev->el));
        else if (ev->type == OLUI_SUBMIT)
            find.fn(find.context, OBD_FIND_NEXT, OLUI_GetText(ev->el));
        return;
    }
    if (ev->type != OLUI_CLICK)
        return;
    if (!strcmp(ev->id, "next"))
        find.fn(find.context, OBD_FIND_NEXT, OLUI_GetText(OLUI_Find(ui, "text")));
    else if (!strcmp(ev->id, "previous"))
        find.fn(find.context, OBD_FIND_PREVIOUS, OLUI_GetText(OLUI_Find(ui, "text")));
    else if (!strcmp(ev->id, "close"))
        find.fn(find.context, OBD_FIND_CLOSED, NULL);
}

int obd_find_height(struct Window *window)
{
    /* the field's line, its border and padding, and the bar's own */
    return window->WScreen->Font->ta_YSize + 14;
}

int obd_find_show(struct Window *window, int bottom, obd_find_fn fn, void *context)
{
    char err[128];
    olui_el *text;
    if (find.ui) {
        text = OLUI_Find(find.ui, "text");
        OLUI_Focus(find.ui, text);
        OLUI_SetSelection(find.ui, text, 0, strlen(OLUI_GetText(text)));
        OLUI_AmigaDraw(find.a);
        return find.height;
    }
    if (!obd_open() || !(find.ui = OLUI_New()))
        return 0;
    find.a = OLUI_AmigaNew(find.ui, window);
    if (!find.a || !OLUI_Load(find.ui, obd_find_markup, obd_css, err, sizeof err)) {
        obd_find_hide();
        return 0;
    }
    OLUI_AmigaTheme(find.a, NULL);
    OLUI_On(find.ui, NULL, 0, findEvent, NULL);
    find.window = window;
    find.fn = fn;
    find.context = context;
    find.height = obd_find_height(window);
    OLUI_AmigaStrip(find.a, 0, -(bottom + find.height), 0, find.height);
    OLUI_AmigaKeys(find.a, OLUI_AMIGA_KEYS_FOCUS);
    OLUI_Focus(find.ui, OLUI_Find(find.ui, "text"));
    OLUI_AmigaLayout(find.a);
    return find.height;
}

void obd_find_hide(void)
{
    if (find.a)
        OLUI_AmigaFree(find.a);
    if (find.ui)
        OLUI_Free(find.ui);
    memset(&find, 0, sizeof find);
}

int obd_find_shown(void)
{
    return find.ui != NULL;
}

void obd_find_result(int current, int count)
{
    char text[32];
    if (!find.ui)
        return;
    obd_find_count_text(text, sizeof text, current, count);
    OLUI_SetText(find.ui, OLUI_Find(find.ui, "count"), text);
    OLUI_AmigaDraw(find.a);
}

/* The browser window's message for the bar: 1 when the bar took it. */
static int findMessage(struct IntuiMessage *msg)
{
    int used;
    if (!find.a || msg->IDCMPWindow != find.window)
        return 0;
    /* Esc closes the bar, Shift+Return goes back, while its field has the keys */
    if (msg->Class == IDCMP_RAWKEY && OLUI_Focused(find.ui) && find.fn) {
        if (msg->Code == 0x45) {
            find.fn(find.context, OBD_FIND_CLOSED, NULL);
            return 1;
        }
        if ((msg->Code == 0x44 || msg->Code == 0x43) && (msg->Qualifier & (IEQUALIFIER_LSHIFT | IEQUALIFIER_RSHIFT))) {
            find.fn(find.context, OBD_FIND_PREVIOUS, OLUI_GetText(OLUI_Find(find.ui, "text")));
            return 1;
        }
        if ((msg->Code & 0x7f) == 0x45 || (msg->Code & 0x7f) == 0x44 || (msg->Code & 0x7f) == 0x43)
            return 1; /* their releases */
    }
    used = OLUI_AmigaEvent(find.a, msg);
    return used == OLUI_AMIGA_USED;
}

/* --- downloads ----------------------------------------------------------------------- */

/* Closing a panel from its own handler waits for the handler to return
 * (obd_draw closes it). */
static int closeDownloads, closePrefs;

static struct {
    olui *ui;
    olui_amiga *a;
    struct Window *win;
    obd_cancel_fn cancel;
    void *context;
    int rows;
    struct { int id, state; char name[120], path[300]; } list[64];
} dl;

static int dlIndex(int id)
{
    int i;
    for (i = 0; i < dl.rows; i++)
        if (dl.list[i].id == id)
            return i;
    return -1;
}

static void dlSetUI(void);

/* The folder a download is in, opened on Workbench (workbench.library 44) */
static void showInFolder(const char *path)
{
    char folder[300];
    struct Library *WorkbenchBase;
    STRPTR end;
    obd_copy(folder, sizeof folder, path);
    end = PathPart((STRPTR)folder);
    *end = 0;
    WorkbenchBase = OpenLibrary((CONST_STRPTR)"workbench.library", 44);
    if (!WorkbenchBase)
        return;
    OpenWorkbenchObjectA((STRPTR)folder, NULL);
    CloseLibrary(WorkbenchBase);
}

static void dlEvent(olui *ui, const olui_event *ev, void *data)
{
    int id, i;
    (void)ui;
    (void)data;
    if (ev->type != OLUI_CLICK)
        return;
    if (!strcmp(ev->id, "close")) {
        closeDownloads = 1;
        return;
    }
    if (!strcmp(ev->id, "clear")) {
        /* the finished ones off the list */
        for (i = dl.rows - 1; i >= 0; i--) {
            char row[16];
            if (dl.list[i].state < OB_DOWNLOAD_DONE)
                continue;
            snprintf(row, sizeof row, "dl%d", dl.list[i].id);
            OLUI_Remove(dl.ui, OLUI_Find(dl.ui, row));
            memmove(&dl.list[i], &dl.list[i + 1], sizeof dl.list[0] * (size_t)(dl.rows - i - 1));
            dl.rows--;
        }
        OLUI_SetHidden(dl.ui, OLUI_Find(dl.ui, "empty"), dl.rows > 0);
        return;
    }
    if (sscanf(ev->id, "cancel%d", &id) == 1 && dl.cancel)
        dl.cancel(dl.context, id);
    else if (sscanf(ev->id, "show%d", &id) == 1 && (i = dlIndex(id)) >= 0)
        showInFolder(dl.list[i].path);
}

void obd_downloads_show(struct Window *parent, obd_cancel_fn cancel, void *context)
{
    char err[128];
    if (cancel) {
        dl.cancel = cancel;
        dl.context = context;
    }
    if (dl.win) {
        WindowToFront(dl.win);
        ActivateWindow(dl.win);
        return;
    }
    if (!obd_open() || !(dl.ui = OLUI_New()))
        return;
    if (!OLUI_Load(dl.ui, obd_downloads_markup, obd_css, err, sizeof err)) {
        OLUI_Free(dl.ui);
        dl.ui = NULL;
        return;
    }
    dl.win = openOver(parent, "Downloads", 460, 240, 1, parent->UserPort);
    dl.a = dl.win ? OLUI_AmigaNew(dl.ui, dl.win) : NULL;
    if (!dl.a) {
        if (dl.win)
            closeShared(dl.win);
        OLUI_Free(dl.ui);
        dl.ui = NULL;
        dl.win = NULL;
        return;
    }
    OLUI_AmigaTheme(dl.a, NULL);
    OLUI_On(dl.ui, NULL, OLUI_CLICK, dlEvent, NULL);
    dlSetUI();
    OLUI_AmigaLayout(dl.a);
}

/* The rows for what the list holds (the window opened after they started). */
static void dlSetUI(void)
{
    int i;
    char err[128];
    for (i = 0; i < dl.rows; i++) {
        char *row = obd_markup_download(dl.list[i].id, dl.list[i].name, dl.list[i].path);
        if (row)
            OLUI_Add(dl.ui, OLUI_Find(dl.ui, "list"), row, err, sizeof err);
        free(row);
    }
    OLUI_SetHidden(dl.ui, OLUI_Find(dl.ui, "empty"), dl.rows > 0);
}

void obd_downloads_update(struct Window *parent, int id, int state, const char *name, const char *path,
    unsigned long received, unsigned long total, const char *error)
{
    int i = dlIndex(id);
    char text[160], widget[16];
    if (i < 0) {
        if (state != OB_DOWNLOAD_STARTED && state != OB_DOWNLOAD_FAILED)
            return;
        if (dl.rows == (int)(sizeof dl.list / sizeof dl.list[0])) {
            /* full: the oldest goes */
            snprintf(widget, sizeof widget, "dl%d", dl.list[0].id);
            if (dl.ui)
                OLUI_Remove(dl.ui, OLUI_Find(dl.ui, widget));
            memmove(&dl.list[0], &dl.list[1], sizeof dl.list[0] * (size_t)(dl.rows - 1));
            dl.rows--;
        }
        i = dl.rows++;
        dl.list[i].id = id;
        obd_copy(dl.list[i].name, sizeof dl.list[i].name, name);
        obd_copy(dl.list[i].path, sizeof dl.list[i].path, path);
        if (!dl.win)
            obd_downloads_show(parent, NULL, NULL);
        else {
            char err[128], *row = obd_markup_download(id, name, path);
            if (row)
                OLUI_Add(dl.ui, OLUI_Find(dl.ui, "list"), row, err, sizeof err);
            free(row);
            OLUI_SetHidden(dl.ui, OLUI_Find(dl.ui, "empty"), 1);
        }
    }
    dl.list[i].state = state;
    if (path && *path)
        obd_copy(dl.list[i].path, sizeof dl.list[i].path, path);
    if (!dl.ui || !dl.win)
        return;
    obd_download_state_text(text, sizeof text, state, received, total, error);
    snprintf(widget, sizeof widget, "state%d", id);
    OLUI_SetText(dl.ui, OLUI_Find(dl.ui, widget), text);
    snprintf(widget, sizeof widget, "bar%d", id);
    OLUI_SetProgress(dl.ui, OLUI_Find(dl.ui, widget),
        state == OB_DOWNLOAD_DONE ? 1000 : total ? (int)(received / (total / 1000UL + 1UL)) : 0);
    if (state >= OB_DOWNLOAD_DONE) {
        snprintf(widget, sizeof widget, "cancel%d", id);
        OLUI_SetHidden(dl.ui, OLUI_Find(dl.ui, widget), 1);
        snprintf(widget, sizeof widget, "show%d", id);
        OLUI_SetHidden(dl.ui, OLUI_Find(dl.ui, widget), state != OB_DOWNLOAD_DONE);
    }
    OLUI_AmigaDraw(dl.a);
}

/* --- preferences ------------------------------------------------------------------------ */

static struct {
    olui *ui;
    olui_amiga *a;
    struct Window *win;
    obd_prefs_fn apply;
    void *context;
} pw;

#define PREFS_ENV "ENV:GFOpenBrowser/Prefs"
#define PREFS_ENVARC "ENVARC:GFOpenBrowser/Prefs"

static int readPrefsFile(const char *name, obd_prefs *prefs)
{
    char text[2048];
    LONG n;
    BPTR file = Open((CONST_STRPTR)name, MODE_OLDFILE);
    if (!file)
        return 0;
    n = Read(file, text, sizeof text - 1);
    Close(file);
    if (n < 0)
        return 0;
    text[n] = 0;
    obd_prefs_parse(prefs, text);
    return 1;
}

void obd_prefs_init(obd_prefs *prefs)
{
    obd_prefs_defaults(prefs);
}

int obd_prefs_load(obd_prefs *prefs)
{
    return readPrefsFile(PREFS_ENV, prefs) || readPrefsFile(PREFS_ENVARC, prefs);
}

static int writePrefsFile(const char *folder, const char *name, const obd_prefs *prefs)
{
    char text[2048];
    int n = obd_prefs_format(prefs, text, sizeof text);
    BPTR lock = Lock((CONST_STRPTR)folder, ACCESS_READ), file;
    if (!lock)
        lock = CreateDir((CONST_STRPTR)folder);
    if (lock)
        UnLock(lock);
    file = Open((CONST_STRPTR)name, MODE_NEWFILE);
    if (!file)
        return 0;
    n = Write(file, text, n) == n;
    Close(file);
    return n;
}

static void prefsEvent(olui *ui, const olui_event *ev, void *data)
{
    obd_prefs prefs;
    (void)data;
    if (ev->type != OLUI_CLICK)
        return;
    if (!strcmp(ev->id, "save") || !strcmp(ev->id, "use")) {
        obd_prefs_defaults(&prefs);
        obd_prefs_from_ui(ui, &prefs);
        writePrefsFile("ENV:GFOpenBrowser", PREFS_ENV, &prefs);
        if (!strcmp(ev->id, "save"))
            writePrefsFile("ENVARC:GFOpenBrowser", PREFS_ENVARC, &prefs);
        if (pw.apply)
            pw.apply(pw.context, &prefs);
        closePrefs = 1;
    } else if (!strcmp(ev->id, "cancel"))
        closePrefs = 1;
}

void obd_prefs_show(struct Window *parent, const obd_prefs *current, obd_prefs_fn apply, void *context)
{
    char err[128];
    if (pw.win) {
        WindowToFront(pw.win);
        ActivateWindow(pw.win);
        return;
    }
    if (!obd_open() || !(pw.ui = OLUI_New()))
        return;
    if (!OLUI_Load(pw.ui, obd_prefs_markup, obd_css, err, sizeof err)) {
        OLUI_Free(pw.ui);
        pw.ui = NULL;
        return;
    }
    obd_prefs_to_ui(pw.ui, current);
    pw.win = openOver(parent, "GFOpenBrowser preferences", 420, 320, 0, parent->UserPort);
    pw.a = pw.win ? OLUI_AmigaNew(pw.ui, pw.win) : NULL;
    if (!pw.a) {
        if (pw.win)
            closeShared(pw.win);
        OLUI_Free(pw.ui);
        memset(&pw, 0, sizeof pw);
        return;
    }
    pw.apply = apply;
    pw.context = context;
    OLUI_AmigaTheme(pw.a, NULL);
    OLUI_On(pw.ui, NULL, OLUI_CLICK, prefsEvent, NULL);
    fitHeight(pw.win, pw.ui);
    OLUI_AmigaLayout(pw.a);
}

/* --- the panels' messages ------------------------------------------------------------------ */

int obd_owns(struct IntuiMessage *msg)
{
    return (dl.win && msg->IDCMPWindow == dl.win) || (pw.win && msg->IDCMPWindow == pw.win)
        || (find.a && msg->IDCMPWindow == find.window && msg->Class != IDCMP_GADGETUP && msg->Class != IDCMP_MENUPICK
            && msg->Class != IDCMP_CLOSEWINDOW && msg->Class != IDCMP_NEWSIZE && msg->Class != IDCMP_REFRESHWINDOW);
}

int obd_event(struct IntuiMessage *msg)
{
    int used = 0;
    if (dl.win && msg->IDCMPWindow == dl.win) {
        if (OLUI_AmigaEvent(dl.a, msg) == OLUI_AMIGA_CLOSE)
            closeDownloads = 1;
        used = 1;
    } else if (pw.win && msg->IDCMPWindow == pw.win) {
        if (OLUI_AmigaEvent(pw.a, msg) == OLUI_AMIGA_CLOSE)
            closePrefs = 1;
        used = 1;
    } else if (find.a)
        used = findMessage(msg);
    return used;
}

static void closeMarked(void)
{
    if (closeDownloads || closePrefs) {
        if (closeDownloads && dl.win) {
            OLUI_AmigaFree(dl.a);
            closeShared(dl.win);
            OLUI_Free(dl.ui);
            dl.a = NULL;
            dl.ui = NULL;
            dl.win = NULL;
        }
        if (closePrefs && pw.win) {
            OLUI_AmigaFree(pw.a);
            closeShared(pw.win);
            OLUI_Free(pw.ui);
            memset(&pw, 0, sizeof pw);
        }
        closeDownloads = closePrefs = 0;
    }
}

void obd_draw(void)
{
    closeMarked();
    if (dl.a)
        OLUI_AmigaDraw(dl.a);
    if (pw.a)
        OLUI_AmigaDraw(pw.a);
    if (find.a)
        OLUI_AmigaDraw(find.a);
}

void obd_close_panels(void)
{
    closeDownloads = dl.win != NULL;
    closePrefs = pw.win != NULL;
    closeMarked();
    obd_find_hide();
}

void obd_find_redraw(void)
{
    if (find.a)
        OLUI_AmigaLayout(find.a);
}

void obd_close(void)
{
    obd_close_panels();
    if (OpenLayoutBase)
        CloseLibrary(OpenLayoutBase);
    OpenLayoutBase = NULL;
    libraryTried = 0;
}
