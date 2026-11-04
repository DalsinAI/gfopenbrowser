/*
 * GFOpenBrowser: the browser's dialogs and panels, described in HTML and CSS
 * and drawn by GFOpenLayout UI (gfopenlayout.library 11, opened at run
 * time, so they take the theme GFOpenPrefs Look chose):
 *
 *  - the page's alert, confirm and prompt, and a server's log-in (HTTP
 *    Basic or Digest): windows of their own over the browser's, which wait
 *    for their answer while the browser's window takes no input;
 *  - the find bar: a strip of the browser's window over its status line;
 *  - the downloads window and the preferences window: windows of their own
 *    that share the browser window's IDCMP port, so the browser's one wait
 *    covers them (obd_owns() says whose a message is).
 *
 * Without gfopenlayout.library 11 the alert and confirm are EasyRequests,
 * as before, a prompt and a log-in are cancelled, and the rest say so.
 * Text in and out is UTF-8; AmigaDOS paths are Latin-1.
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#ifndef OB_DIALOGS_H
#define OB_DIALOGS_H

struct Window;
struct IntuiMessage;

/* Opens gfopenlayout.library 11 once: 1 when it is there. */
int obd_open(void);
/* Closes the panels still open and the library. */
void obd_close(void);

/* --- dialogs that wait for their answer ----------------------------------- */

void obd_alert(struct Window *parent, const char *host, const char *message);
int obd_confirm(struct Window *parent, const char *host, const char *message);
/* result: at most resultSize bytes; 0 for Cancel. */
int obd_prompt(struct Window *parent, const char *host, const char *message, const char *defaultValue,
    char *result, int resultSize);
/* user comes in with a name to start with (may be empty); 0 for Cancel. */
int obd_login(struct Window *parent, const char *host, const char *realm, int previousFailures,
    char *user, int userSize, char *password, int passwordSize);

/* --- panels that stay open ------------------------------------------------- */

/* The window's messages for them: 1 when one of them took msg (before it
 * is replied: the keymap reads its dead keys). */
int obd_owns(struct IntuiMessage *msg);
int obd_event(struct IntuiMessage *msg);
/* Draws what changed in them. */
void obd_draw(void);
/* The panels' windows (downloads, preferences) and the find bar close
 * before the window they share the port with. */
void obd_close_panels(void);

/* The find bar: what it asks the browser to do. */
enum { OBD_FIND_TYPED, OBD_FIND_NEXT, OBD_FIND_PREVIOUS, OBD_FIND_CLOSED };
typedef void (*obd_find_fn)(void *context, int what, const char *text);
/* Opens the bar in window, height pixels tall, bottom pixels above the
 * inside's bottom edge (over the status line), with the keyboard in its
 * field. Returns its height, 0 when it can't be shown. Opened already, it
 * takes the keyboard again and selects its text. */
int obd_find_show(struct Window *window, int bottom, obd_find_fn fn, void *context);
void obd_find_hide(void);
/* After the browser has laid its window out again or refreshed it. */
void obd_find_redraw(void);
int obd_find_shown(void);
/* "3 of 17", "No matches"; count -1 shows nothing. */
void obd_find_result(int current, int count);
/* The height the bar needs in window's font. */
int obd_find_height(struct Window *window);

/* The downloads window (Chrome's list: each with its state, Cancel, and
 * Show in folder when done). cancel is called with a download's number. */
typedef void (*obd_cancel_fn)(void *context, int id);
void obd_downloads_show(struct Window *parent, obd_cancel_fn cancel, void *context);
/* One download's news (ob_webview.h's OB_DOWNLOAD_...): it is listed when
 * it starts, and the window opens then if it isn't. */
void obd_downloads_update(struct Window *parent, int id, int state, const char *name, const char *path,
    unsigned long received, unsigned long total, const char *error);

/* The preferences (ENV:GFOpenBrowser/Prefs, saved in ENVARC: too) */
typedef struct {
    char homePage[512];
    char downloadFolder[256];
    int scripts, pictures, webFonts, lite;
} obd_prefs;
/* The defaults: example.com, RAM:, JavaScript and pictures on. */
void obd_prefs_init(obd_prefs *prefs);
/* Fills prefs from ENV: (or ENVARC:), keeping what's in it for anything
 * not there: 1 when a file was read. */
int obd_prefs_load(obd_prefs *prefs);
/* The window: Save (ENV: and ENVARC:), Use (ENV:) and Cancel. apply is
 * called with the new preferences on Save and Use. */
typedef void (*obd_prefs_fn)(void *context, const obd_prefs *prefs);
void obd_prefs_show(struct Window *parent, const obd_prefs *current, obd_prefs_fn apply, void *context);

#endif
