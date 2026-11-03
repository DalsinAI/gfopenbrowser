/*
 * GFOpenBrowser: the dialogs' contents, in HTML and CSS for GFOpenLayout UI,
 * and what their widgets do. Plain C on olui's calls only (no Intuition),
 * so tests/dialogs builds it on the host against GFOpenLayout's sources and
 * draws each dialog to look at; ob_dialogs.c builds it on the Amiga with the
 * calls going to gfopenlayout.library (its OLUI_ names).
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* --- text ------------------------------------------------------------------ */

typedef struct {
    char *text;
    size_t length, room;
    int failed;
} obd_text;

static void tx_add_n(obd_text *t, const char *s, size_t n)
{
    if (t->failed)
        return;
    if (t->length + n + 1 > t->room) {
        size_t room = t->room ? t->room : 256;
        char *grown;
        while (t->length + n + 1 > room)
            room *= 2;
        grown = (char *)realloc(t->text, room);
        if (!grown) {
            t->failed = 1;
            return;
        }
        t->text = grown;
        t->room = room;
    }
    memcpy(t->text + t->length, s, n);
    t->length += n;
    t->text[t->length] = 0;
}

static void tx_add(obd_text *t, const char *s)
{
    tx_add_n(t, s, strlen(s));
}

/* Text as HTML's text or an attribute's value in quotes: & < > " escaped,
 * a newline a <br> when lines is set. */
static void tx_escape(obd_text *t, const char *s, int lines)
{
    for (; s && *s; s++) {
        switch (*s) {
        case '&': tx_add(t, "&amp;"); break;
        case '<': tx_add(t, "&lt;"); break;
        case '>': tx_add(t, "&gt;"); break;
        case '"': tx_add(t, "&quot;"); break;
        case '\r': break;
        case '\n': tx_add(t, lines ? "<br>" : " "); break;
        default: tx_add_n(t, s, 1); break;
        }
    }
}

static char *tx_take(obd_text *t)
{
    if (t->failed) {
        free(t->text);
        return NULL;
    }
    return t->text;
}

static void obd_copy(char *out, int size, const char *in)
{
    if (!out || size <= 0)
        return;
    strncpy(out, in ? in : "", (size_t)size - 1);
    out[size - 1] = 0;
}

/* --- the look ---------------------------------------------------------------- */

/* After the theme's CSS (GFOpenPrefs Look), so only sizes and spacing. */
static const char obd_css[] =
    "window{padding:12px}"
    "#all{gap:10px}"
    ".buttons{gap:8px;align-items:center}"
    "button{padding:3px 14px}"
    ".head{font-weight:bold}"
    ".note{color:#b01818}"
    ".label{width:96px}"
    ".field{gap:6px;align-items:center}"
    "fieldset{gap:6px;padding:6px 10px}"
    /* the find bar, a strip over the status line */
    "#find{gap:6px;padding:2px 6px;align-items:center}"
    "#count{width:104px;text-align:right}"
    /* downloads */
    "#list{flex:1;overflow:auto;gap:8px;padding:4px}"
    ".download{gap:8px;align-items:center}"
    ".download .name{font-weight:bold}"
    ".download progress{height:8px}"
    ".download .failed{color:#b01818}";

/* --- dialogs that wait for their answer ------------------------------------------ */

typedef struct {
    int done;
    int ok;
} obd_answer;

static void obd_answered(olui *ui, const olui_event *ev, void *data)
{
    obd_answer *answer = (obd_answer *)data;
    (void)ui;
    answer->done = 1;
    answer->ok = !strcmp(ev->id, "ok") || ev->type == OLUI_SUBMIT;
}

/* "example.com says" over the page's message; with an input, a prompt's
 * field; with cancel, a Cancel button beside OK. */
static char *obd_markup_message(const char *host, const char *message, int input, int cancel)
{
    obd_text t = { 0 };
    tx_add(&t, "<column id=all><div class=head>");
    if (host && *host) {
        tx_escape(&t, host, 0);
        tx_add(&t, " says");
    } else
        tx_add(&t, "This page says");
    tx_add(&t, "</div><p id=message>");
    tx_escape(&t, message, 1);
    tx_add(&t, "</p>");
    if (input)
        tx_add(&t, "<input id=value>");
    tx_add(&t, "<row class=buttons><spacer></spacer><button id=ok>OK</button>");
    if (cancel)
        tx_add(&t, "<button id=cancel>Cancel</button>");
    tx_add(&t, "</row></column>");
    return tx_take(&t);
}

/* Chrome's "Sign in": the server's name and realm, the two fields, and why
 * it asks again when the last log-in was refused. */
static char *obd_markup_login(const char *host, const char *realm, int previousFailures)
{
    obd_text t = { 0 };
    tx_add(&t, "<column id=all><div class=head>Sign in</div><p>");
    tx_escape(&t, host, 0);
    tx_add(&t, " asks for a user name and password.");
    if (realm && *realm) {
        tx_add(&t, " It says: &quot;");
        tx_escape(&t, realm, 0);
        tx_add(&t, "&quot;");
    }
    tx_add(&t, "</p>");
    if (previousFailures > 0)
        tx_add(&t, "<p class=note>The user name or password was not accepted. Try again.</p>");
    tx_add(&t, "<row class=field><label for=user class=label>User name</label><input id=user style='flex:1'></row>"
               "<row class=field><label for=password class=label>Password</label>"
               "<input id=password type=password style='flex:1'></row>"
               "<row class=buttons><spacer></spacer><button id=ok>Sign in</button><button id=cancel>Cancel</button></row>"
               "</column>");
    return tx_take(&t);
}

/* Loads markup into ui with the dialogs' CSS, and OK, Cancel and Return in
 * a field answer. 1 when it loaded. */
static int obd_setup_modal(olui *ui, const char *markup, obd_answer *answer)
{
    char err[128];
    if (!markup || !olui_load(ui, markup, obd_css, err, sizeof err))
        return 0;
    olui_on(ui, "ok", OLUI_CLICK, obd_answered, answer);
    olui_on(ui, "cancel", OLUI_CLICK, obd_answered, answer);
    olui_on(ui, "value", OLUI_SUBMIT, obd_answered, answer);
    olui_on(ui, "user", OLUI_SUBMIT, obd_answered, answer);
    olui_on(ui, "password", OLUI_SUBMIT, obd_answered, answer);
    return 1;
}

/* --- the find bar ------------------------------------------------------------------ */

static const char obd_find_markup[] =
    "<row id=find><label for=text>Find</label><input id=text placeholder='Find in page' style='flex:1'>"
    "<span id=count></span><button id=previous title='Previous match (Shift+Return)'>Previous</button>"
    "<button id=next title='Next match (Return)'>Next</button><button id=close title='Close (Esc)'>Close</button></row>";

static void obd_find_count_text(char *out, int size, int current, int count)
{
    if (count < 0)
        obd_copy(out, size, "");
    else if (!count)
        obd_copy(out, size, "No matches");
    else if (count >= 1000)
        snprintf(out, (size_t)size, "%d of 1000+", current);
    else
        snprintf(out, (size_t)size, "%d of %d", current, count);
}

/* --- downloads ---------------------------------------------------------------------- */

static const char obd_downloads_markup[] =
    "<div id=empty>Nothing downloaded yet. Downloads go to the folder the preferences name.</div>"
    "<column id=list></column>"
    "<row class=buttons><button id=clear title='Take finished downloads off the list'>Clear list</button>"
    "<spacer></spacer><button id=close>Close</button></row>";

/* "1.2 MB" and the like */
static void obd_size_text(char *out, int size, unsigned long bytes)
{
    if (bytes < 1024UL)
        snprintf(out, (size_t)size, "%lu B", bytes);
    else if (bytes < 1024UL * 1024UL)
        snprintf(out, (size_t)size, "%lu.%lu KB", bytes / 1024UL, (bytes % 1024UL) * 10UL / 1024UL);
    else
        snprintf(out, (size_t)size, "%lu.%lu MB", bytes / (1024UL * 1024UL), (bytes % (1024UL * 1024UL)) * 10UL / (1024UL * 1024UL));
}

/* A download's line under its name. */
static void obd_download_state_text(char *out, int size, int state, unsigned long received, unsigned long total,
    const char *error)
{
    char got[24], all[24];
    obd_size_text(got, sizeof got, received);
    obd_size_text(all, sizeof all, total);
    switch (state) {
    case OB_DOWNLOAD_DONE:
        snprintf(out, (size_t)size, "Done, %s", got);
        break;
    case OB_DOWNLOAD_FAILED:
        snprintf(out, (size_t)size, "Failed: %s", error && *error ? error : "the download stopped");
        break;
    case OB_DOWNLOAD_CANCELLED:
        obd_copy(out, size, "Cancelled");
        break;
    default:
        if (total)
            snprintf(out, (size_t)size, "%s of %s", got, all);
        else
            snprintf(out, (size_t)size, "%s", got);
        break;
    }
}

/* One download's row, its widgets named by its number. */
static char *obd_markup_download(int id, const char *name, const char *path)
{
    obd_text t = { 0 };
    char n[96];
    snprintf(n, sizeof n, "<row class=download id=dl%d><column style='flex:1'><div class=name title=\"", id);
    tx_add(&t, n);
    tx_escape(&t, path, 0);
    tx_add(&t, "\">");
    tx_escape(&t, name, 0);
    snprintf(n, sizeof n, "</div><progress id=bar%d max=1000></progress><div id=state%d>Starting</div></column>", id, id);
    tx_add(&t, n);
    snprintf(n, sizeof n, "<button id=cancel%d>Cancel</button><button id=show%d hidden>Show in folder</button></row>", id, id);
    tx_add(&t, n);
    return tx_take(&t);
}

/* --- preferences ---------------------------------------------------------------------- */

static void obd_prefs_defaults(obd_prefs *prefs)
{
    memset(prefs, 0, sizeof *prefs);
    obd_copy(prefs->homePage, sizeof prefs->homePage, "https://example.com/");
    obd_copy(prefs->downloadFolder, sizeof prefs->downloadFolder, "RAM:");
    prefs->scripts = prefs->pictures = 1;
}

/* KEY=VALUE lines, as tool types are written. */
static void obd_prefs_parse(obd_prefs *prefs, const char *text)
{
    while (text && *text) {
        const char *end = strchr(text, '\n'), *eq;
        size_t n = end ? (size_t)(end - text) : strlen(text);
        char line[600], *value;
        if (n >= sizeof line)
            n = sizeof line - 1;
        memcpy(line, text, n);
        line[n] = 0;
        while (n && (line[n - 1] == '\r' || line[n - 1] == ' '))
            line[--n] = 0;
        eq = strchr(line, '=');
        if (eq && line[0] != ';' && line[0] != '#') {
            value = line + (eq - line) + 1;
            line[eq - line] = 0;
            if (!strcmp(line, "HOMEPAGE"))
                obd_copy(prefs->homePage, sizeof prefs->homePage, value);
            else if (!strcmp(line, "DOWNLOADS"))
                obd_copy(prefs->downloadFolder, sizeof prefs->downloadFolder, value);
            else if (!strcmp(line, "JAVASCRIPT"))
                prefs->scripts = strcmp(value, "NO") != 0;
            else if (!strcmp(line, "PICTURES"))
                prefs->pictures = strcmp(value, "NO") != 0;
            else if (!strcmp(line, "WEBFONTS"))
                prefs->webFonts = !strcmp(value, "YES");
            else if (!strcmp(line, "LITE"))
                prefs->lite = !strcmp(value, "YES");
        }
        text = end ? end + 1 : NULL;
    }
}

static int obd_prefs_format(const obd_prefs *prefs, char *out, int size)
{
    return snprintf(out, (size_t)size,
        "; GFOpenBrowser's preferences (its Preferences window writes them)\n"
        "HOMEPAGE=%s\nDOWNLOADS=%s\nJAVASCRIPT=%s\nPICTURES=%s\nWEBFONTS=%s\nLITE=%s\n",
        prefs->homePage, prefs->downloadFolder, prefs->scripts ? "YES" : "NO", prefs->pictures ? "YES" : "NO",
        prefs->webFonts ? "YES" : "NO", prefs->lite ? "YES" : "NO");
}

static const char obd_prefs_markup[] =
    "<column id=all>"
    "<fieldset><legend>Start</legend>"
    "<row class=field><label for=home class=label>Home page</label><input id=home style='flex:1'></row></fieldset>"
    "<fieldset><legend>Pages</legend>"
    "<checkbox id=scripts title='Off, pages load much faster on a 68k'>JavaScript</checkbox>"
    "<checkbox id=pictures>Pictures</checkbox>"
    "<checkbox id=webfonts title='Off, pages use the Liberation and DejaVu fonts'>Web fonts</checkbox>"
    "<checkbox id=lite title='Ask sites for their mobile pages'>Lite (mobile pages)</checkbox></fieldset>"
    "<fieldset><legend>Downloads</legend>"
    "<row class=field><label for=folder class=label>Save in</label><input id=folder style='flex:1'></row></fieldset>"
    "<row class=buttons><button id=save>Save</button><spacer></spacer><button id=use>Use</button>"
    "<spacer></spacer><button id=cancel>Cancel</button></row>"
    "</column>";

static void obd_prefs_to_ui(olui *ui, const obd_prefs *prefs)
{
    olui_set_text(ui, olui_find(ui, "home"), prefs->homePage);
    olui_set_text(ui, olui_find(ui, "folder"), prefs->downloadFolder);
    olui_set_checked(ui, olui_find(ui, "scripts"), prefs->scripts);
    olui_set_checked(ui, olui_find(ui, "pictures"), prefs->pictures);
    olui_set_checked(ui, olui_find(ui, "webfonts"), prefs->webFonts);
    olui_set_checked(ui, olui_find(ui, "lite"), prefs->lite);
}

static void obd_prefs_from_ui(olui *ui, obd_prefs *prefs)
{
    const char *text = olui_get_text(olui_find(ui, "home"));
    obd_copy(prefs->homePage, sizeof prefs->homePage, text && *text ? text : "https://example.com/");
    text = olui_get_text(olui_find(ui, "folder"));
    obd_copy(prefs->downloadFolder, sizeof prefs->downloadFolder, text && *text ? text : "RAM:");
    prefs->scripts = olui_get_checked(olui_find(ui, "scripts"));
    prefs->pictures = olui_get_checked(olui_find(ui, "pictures"));
    prefs->webFonts = olui_get_checked(olui_find(ui, "webfonts"));
    prefs->lite = olui_get_checked(olui_find(ui, "lite"));
}
