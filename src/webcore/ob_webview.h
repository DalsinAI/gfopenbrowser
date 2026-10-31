/*
 * OpenBrowser: the C interface between the Amiga window (GadTools, C) and
 * a WebCore page (C++). Everything here runs on the program's main task,
 * except ob_webcore_set_wakeup()'s callback, which other threads may call.
 * MIT, Copyright (c) 2026 AmigaChrome Team.
 */
#ifndef OB_WEBVIEW_H
#define OB_WEBVIEW_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct OBWebView OBWebView;

/* What the page tells its window. Text is UTF-8; any callback may be NULL. */
typedef struct {
    void *context;
    void (*invalidate)(void *context, int x, int y, int width, int height);
    void (*title)(void *context, const char *title);
    void (*url)(void *context, const char *url);
    void (*status)(void *context, const char *text);
    void (*loading)(void *context, int loading, int percent);
    void (*failed)(void *context, const char *url, const char *description);
    void (*alert)(void *context, const char *message);
    int (*confirm)(void *context, const char *message);
    /* Fills result (at most resultSize bytes, UTF-8); returns 0 for Cancel. */
    int (*prompt)(void *context, const char *message, const char *defaultValue, char *result, int resultSize);
    /* The page's console: script errors and console.log(). */
    void (*console)(void *context, const char *message, int line, const char *source);
    /* A request for the page or one of its parts starts (started = 1) or
     * ends (started = 0, with error NULL or the reason it failed). */
    void (*resource)(void *context, const char *url, int started, const char *error);
    /* After each rendering update (style and layout of the page): the page
     * is laid out, and ob_webview_dirty_rects() gives out what changed. */
    void (*rendered)(void *context);
} OBWebViewCallbacks;

/* Mouse buttons and the qualifiers that go with events. */
enum { OB_BUTTON_NONE, OB_BUTTON_LEFT, OB_BUTTON_MIDDLE, OB_BUTTON_RIGHT };
enum {
    OB_QUAL_SHIFT = 1 << 0,
    OB_QUAL_CONTROL = 1 << 1,
    OB_QUAL_ALT = 1 << 2,
    OB_QUAL_AMIGA = 1 << 3,     /* the right Amiga key, WebCore's Meta */
    OB_QUAL_CAPSLOCK = 1 << 4
};
enum { OB_MOUSE_MOVE, OB_MOUSE_DOWN, OB_MOUSE_UP };

/* Once per program, on the main task, before any view. 0 on failure.
 * ob_webcore_init() loads nothing from the network; the _with_network form
 * loads through openweb.fetch and keeps cookies in cookieDatabase (an
 * AmigaDOS name, or ":memory:"). */
int ob_webcore_init(void);
int ob_webcore_init_with_network(const char *cookieDatabase);
void ob_webcore_shutdown(void);
/* Ends WebKit's helper threads, so the program can exit (libpthread waits
 * for every thread). ob_webcore_shutdown() does this too. */
void ob_webcore_stop_threads(void);

/* TLS sessions kept across runs (on the main task, with the network up):
 * load returns how many came back. openweb.fetch keeps none yet, so load
 * returns 0 and save writes nothing. */
int ob_webview_load_tls_sessions(const char *path);
void ob_webview_save_tls_sessions(const char *path);

/* The run loop: do what is due, then ask when the next timer is (seconds
 * from now; 0 means at once, a negative number means no timer). The _micros
 * form gives whole microseconds (at most LONG_MAX) and no floating point. */
void ob_webcore_cycle(void);
double ob_webcore_next_timer(void);
long ob_webcore_next_timer_micros(void);
/* Called, from any thread, when work is queued for the main task. */
void ob_webcore_set_wakeup(void (*wakeup)(void *context), void *context);

OBWebView *ob_webview_create(int width, int height, const OBWebViewCallbacks *callbacks);
void ob_webview_destroy(OBWebView *view);

void ob_webview_load(OBWebView *view, const char *url);
void ob_webview_load_html(OBWebView *view, const char *html, const char *baseURL);
/* As load_html, with the MIME type given ("application/xhtml+xml", "image/svg+xml"...). */
void ob_webview_load_data(OBWebView *view, const char *text, const char *mimeType, const char *baseURL);
void ob_webview_back(OBWebView *view);
void ob_webview_forward(OBWebView *view);
void ob_webview_reload(OBWebView *view);
void ob_webview_stop(OBWebView *view);
int ob_webview_can_go_back(OBWebView *view);
int ob_webview_can_go_forward(OBWebView *view);

void ob_webview_resize(OBWebView *view, int width, int height);
/* Paints a part of the view into a 32-bit buffer of the whole view's size:
 * bytes A, R, G, B (cybergraphics' RECTFMT_ARGB), stride bytes per row. */
/* The page's text (innerText of the document), UTF-8, cut to fit size;
 * returns its full length in bytes. */
int ob_webview_text(OBWebView *view, char *buffer, int size);
/* Bytes the view's requests have received since it was created, as WebCore
 * reports them arriving (for tools/bench; 0 if the loader reports none). */
unsigned long ob_webview_bytes_received(OBWebView *view);

void ob_webview_paint(OBWebView *view, unsigned char *argb, int stride, int x, int y, int width, int height);
/* The display-list experiment: paints the area directly into `direct` and,
 * through WebKit's display list (recorded, then replayed with cairo), into
 * `replayed`, and prints OBVIEW_DL lines: what the commands are, roughly
 * how many bytes they take, and how long painting, recording and replaying
 * took. */
void ob_webview_report_display_list(OBWebView *view, unsigned char *direct, unsigned char *replayed, int stride,
    int x, int y, int width, int height);
/* The area changed since the last call (width 0 when none). */
void ob_webview_dirty(OBWebView *view, int *x, int *y, int *width, int *height);
/* The areas changed, up to max of them (x, y, width, height each in rects),
 * once they are laid out: none while the page's layout is still due (for
 * at most two rendering updates; after that they come to be painted laid out
 * first), and while a page loads at most once per rendering update or
 * second. Returns how many. Paint them with ob_webview_paint_laid_out(). */
int ob_webview_dirty_rects(OBWebView *view, int *rects, int max);
/* As ob_webview_paint(), without laying the page out first: where layout is
 * still due it paints nothing, keeps the area changed and returns 0. */
int ob_webview_paint_laid_out(OBWebView *view, unsigned char *argb, int stride, int x, int y, int width, int height);
/* 1 while a rendering update (style and layout) is waiting to run. */
int ob_webview_rendering_pending(OBWebView *view);

void ob_webview_mouse(OBWebView *view, int type, int x, int y, int button, int qualifiers, int clickCount);
void ob_webview_wheel(OBWebView *view, int x, int y, int deltaX, int deltaY, int qualifiers);
/* rawKey is Intuition's IDCMP_RAWKEY code (without the up bit); text the
 * characters MapRawKey() made of it (UTF-8, may be empty). */
void ob_webview_key(OBWebView *view, int down, int rawKey, const char *text, int qualifiers);
void ob_webview_focus(OBWebView *view, int focused);

/* Page options, both on at first: scripts (JavaScript) and pictures. Off,
 * pages load much faster on a 68k. They apply from the next page loaded. */
void ob_webview_set_scripts(OBWebView *view, int enabled);
void ob_webview_set_pictures(OBWebView *view, int enabled);
/* Off at first: web fonts (pages use the Liberation and DejaVu fonts), and
 * Lite, which asks sites for their mobile pages. */
void ob_webview_set_web_fonts(OBWebView *view, int enabled);
void ob_webview_set_lite(OBWebView *view, int enabled);

#ifdef __cplusplus
}
#endif

#endif
