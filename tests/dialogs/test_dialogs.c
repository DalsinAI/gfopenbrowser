/*
 * The browser's dialogs on the host (src/webcore/ob_dialogs_ui.c): each one
 * loads, lays out in its window's width, answers its buttons and keys, and
 * is drawn as ops for GFOpenLayout's tests/ui_render.py, to look at.
 *
 *   tests/dialogs/run.sh [GFOPENLAYOUT_DIR] [OUT_DIR]
 *
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gfopenlayout_ui.h"
#include "ob_webview.h"
#include "ob_dialogs.h"
#include "ob_dialogs_ui.c"

static int failures;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); ++failures; } } while (0)

/* As GFOpenLayout's test_ui: every character 7 px wide (8 bold) at 13 px. */
static int measure(void *userdata, const char *utf8, size_t length, const ol_style *style, ol_text_metrics *m)
{
    size_t i, glyphs = 0;
    (void)userdata;
    for (i = 0; i < length; ++i)
        if (((unsigned char)utf8[i] & 0xc0u) != 0x80u) ++glyphs;
    m->width = (ol_unit)glyphs * style->font_size * ((style->text_flags & OL_TEXT_BOLD) ? 8 : 7) / 13;
    m->ascent = style->font_size * 10 / 13;
    m->descent = style->font_size * 3 / 13;
    return 1;
}

/* A theme like GFOpenPrefs Look's default, so the pictures look as on the Amiga. */
static const char theme[] =
    "window{background:#d8dce4;color:#101418;font-size:13px}"
    "button{background:linear-gradient(#f4f6fa,#c8ced8);border:1px solid #606878;border-radius:3px;padding:2px 8px}"
    "button:active{background:#9aa6b8}"
    "input{background:#ffffff;border:1px solid #606878;padding:2px 4px}"
    "input:focus{border-color:#2860c0}"
    "fieldset{border:1px solid #8890a0}"
    "progress{background:#ffffff;border:1px solid #606878;accent-color:#2860c0}"
    "checkbox{mark-background:#ffffff;accent-color:#2860c0}";

static FILE *ops;
static void draw(void *userdata, const olui_draw_op *op)
{
    int i;
    (void)userdata;
    switch (op->kind) {
    case OLUI_DRAW_BOX:
        fprintf(ops, "box %ld %ld %ld %ld %d %08lx %d %d", (long)op->box.x, (long)op->box.y, (long)op->box.w,
            (long)op->box.h, op->border, (unsigned long)op->border_argb, op->radius, op->stops);
        for (i = 0; i < op->stops; ++i)
            fprintf(ops, " %08lx %d", (unsigned long)op->stop_argb[i], op->stop_pos[i]);
        fputc('\n', ops);
        break;
    case OLUI_DRAW_TEXT:
        fprintf(ops, "text %ld %ld %d %08lx %lu %.*s\n", (long)op->box.x, (long)op->baseline, op->font_pixels,
            (unsigned long)op->argb, (unsigned long)op->text_flags, (int)op->length, op->text);
        break;
    case OLUI_DRAW_MARK:
        fprintf(ops, "mark %ld %ld %ld %ld %d %08lx\n", (long)op->box.x, (long)op->box.y, (long)op->box.w,
            (long)op->box.h, op->mark, (unsigned long)op->argb);
        break;
    case OLUI_DRAW_CLIP:
        fprintf(ops, "clip %ld %ld %ld %ld\n", (long)op->clip.x, (long)op->clip.y, (long)op->clip.w, (long)op->clip.h);
        break;
    default:
        break;
    }
}

static const char *outDir = ".";

/* Lays ui out width wide as the glue does (the height its #all needs, or
 * height) and writes its ops to OUT_DIR/name.ops. Returns the height. */
static int show(olui *ui, const char *name, int width, int height)
{
    char path[512];
    olui_rect r;
    olui_el *all = olui_find(ui, "all");
    CHECK(olui_layout(ui, width, height ? height : 2000));
    if (!height) {
        CHECK(all && olui_box(ui, all, &r));
        height = all ? r.y + r.h + 12 : 200;
        CHECK(olui_layout(ui, width, height));
    }
    snprintf(path, sizeof path, "%s/%s.ops", outDir, name);
    ops = fopen(path, "w");
    CHECK(ops != NULL);
    if (!ops)
        return height;
    fprintf(ops, "size %d %d\n", width, height);
    olui_draw(ui, NULL, draw, NULL);
    fclose(ops);
    printf("%s: %d x %d\n", name, width, height);
    return height;
}

static olui *new_ui(void)
{
    olui *ui = olui_new();
    olui_set_measure(ui, measure, NULL);
    olui_add_css(ui, theme);
    return ui;
}

/* The centre of a widget, for a click. */
static void click(olui *ui, const char *id)
{
    olui_rect r;
    olui_el *el = olui_find(ui, id);
    CHECK(el && olui_box(ui, el, &r));
    if (!el)
        return;
    olui_mouse(ui, r.x + r.w / 2, r.y + r.h / 2, OLUI_MOUSE_MOVE);
    olui_mouse(ui, r.x + r.w / 2, r.y + r.h / 2, OLUI_MOUSE_DOWN);
    olui_mouse(ui, r.x + r.w / 2, r.y + r.h / 2, OLUI_MOUSE_UP);
}

static void type(olui *ui, const char *text)
{
    for (; *text; text++)
        olui_key(ui, (unsigned char)*text, 0);
}

static void test_alert_confirm_prompt(void)
{
    obd_answer answer = { 0, 0 };
    olui *ui = new_ui();
    char *markup = obd_markup_message("example.com", "Are you sure you want to leave?\nChanges <b>are</b> lost & gone.", 0, 1);
    CHECK(obd_setup_modal(ui, markup, &answer));
    free(markup);
    show(ui, "confirm", 400, 0);
    click(ui, "cancel");
    CHECK(answer.done && !answer.ok);
    answer.done = 0;
    click(ui, "ok");
    CHECK(answer.done && answer.ok);
    olui_free(ui);

    ui = new_ui();
    memset(&answer, 0, sizeof answer);
    markup = obd_markup_message("", "Something happened.", 0, 0);
    CHECK(obd_setup_modal(ui, markup, &answer));
    free(markup);
    CHECK(olui_find(ui, "cancel") == NULL);
    show(ui, "alert", 400, 0);
    olui_focus(ui, olui_find(ui, "ok"));
    olui_key(ui, OLUI_KEY_RETURN, 0);
    CHECK(answer.done && answer.ok);
    olui_free(ui);

    ui = new_ui();
    memset(&answer, 0, sizeof answer);
    markup = obd_markup_message("www.bbc.co.uk", "Your name, please:", 1, 1);
    CHECK(obd_setup_modal(ui, markup, &answer));
    free(markup);
    olui_layout(ui, 400, 300);
    olui_set_text(ui, olui_find(ui, "value"), "Dale");
    olui_focus(ui, olui_find(ui, "value"));
    type(ui, " K");
    show(ui, "prompt", 400, 0);
    CHECK(!strcmp(olui_get_text(olui_find(ui, "value")), "Dale K"));
    olui_key(ui, OLUI_KEY_RETURN, 0);
    CHECK(answer.done && answer.ok);
    olui_free(ui);
}

static void test_login(void)
{
    obd_answer answer = { 0, 0 };
    olui *ui = new_ui();
    char *markup = obd_markup_login("intranet.example.org", "Staff \"only\" area", 1);
    CHECK(obd_setup_modal(ui, markup, &answer));
    free(markup);
    olui_layout(ui, 380, 300);
    olui_focus(ui, olui_find(ui, "user"));
    type(ui, "dale");
    olui_key(ui, OLUI_KEY_TAB, 0);
    type(ui, "s3cret");
    show(ui, "login", 380, 0);
    CHECK(!strcmp(olui_get_text(olui_find(ui, "user")), "dale"));
    CHECK(!strcmp(olui_get_text(olui_find(ui, "password")), "s3cret"));
    olui_key(ui, OLUI_KEY_RETURN, 0);
    CHECK(answer.done && answer.ok);
    olui_free(ui);
}

static int findNext, findTyped;
static void on_find(olui *ui, const olui_event *ev, void *data)
{
    (void)ui;
    (void)data;
    if (ev->type == OLUI_SUBMIT || !strcmp(ev->id, "next"))
        ++findNext;
    else if (ev->type == OLUI_CHANGE)
        ++findTyped;
}

static void test_find(void)
{
    char err[128], count[32];
    olui *ui = new_ui();
    CHECK(olui_load(ui, obd_find_markup, obd_css, err, sizeof err));
    olui_on(ui, "text", 0, on_find, NULL);
    olui_on(ui, "next", OLUI_CLICK, on_find, NULL);
    olui_layout(ui, 760, 26);
    olui_focus(ui, olui_find(ui, "text"));
    type(ui, "amiga");
    CHECK(findTyped == 5);
    obd_find_count_text(count, sizeof count, 3, 17);
    CHECK(!strcmp(count, "3 of 17"));
    CHECK(olui_set_text(ui, olui_find(ui, "count"), count));
    olui_key(ui, OLUI_KEY_RETURN, 0);
    click(ui, "next");
    CHECK(findNext == 2);
    show(ui, "find", 760, 26);
    obd_find_count_text(count, sizeof count, 0, 0);
    CHECK(!strcmp(count, "No matches"));
    olui_free(ui);
}

static void test_downloads(void)
{
    char err[128], state[96];
    olui *ui = new_ui();
    char *row;
    olui_el *list;
    CHECK(olui_load(ui, obd_downloads_markup, obd_css, err, sizeof err));
    list = olui_find(ui, "list");
    CHECK(list != NULL);
    olui_set_hidden(ui, olui_find(ui, "empty"), 1);
    row = obd_markup_download(1, "AmigaOS-3.2.3-update.lha", "RAM:AmigaOS-3.2.3-update.lha");
    CHECK(row && olui_add(ui, list, row, err, sizeof err));
    free(row);
    row = obd_markup_download(2, "report <draft>.pdf", "RAM:report <draft>.pdf");
    CHECK(row && olui_add(ui, list, row, err, sizeof err));
    free(row);
    obd_download_state_text(state, sizeof state, OB_DOWNLOAD_PROGRESS, 1310720UL, 3670016UL, NULL);
    CHECK(!strcmp(state, "1.2 MB of 3.5 MB"));
    olui_set_text(ui, olui_find(ui, "state1"), state);
    olui_set_progress(ui, olui_find(ui, "bar1"), 352);
    obd_download_state_text(state, sizeof state, OB_DOWNLOAD_DONE, 48213UL, 48213UL, NULL);
    CHECK(!strcmp(state, "Done, 47.0 KB"));
    olui_set_text(ui, olui_find(ui, "state2"), state);
    olui_set_progress(ui, olui_find(ui, "bar2"), 1000);
    olui_set_hidden(ui, olui_find(ui, "cancel2"), 1);
    olui_set_hidden(ui, olui_find(ui, "show2"), 0);
    show(ui, "downloads", 460, 220);
    CHECK(olui_remove(ui, olui_find(ui, "dl2")));
    CHECK(olui_find(ui, "state2") == NULL);
    olui_free(ui);
}

static void test_prefs(void)
{
    char err[128], text[1024];
    obd_prefs prefs, back;
    olui *ui = new_ui();
    obd_prefs_defaults(&prefs);
    obd_prefs_parse(&prefs, "; comment\nHOMEPAGE=https://www.bbc.co.uk/news\r\nJAVASCRIPT=NO\nLITE=YES\nDOWNLOADS=Work:Downloads\n");
    CHECK(!strcmp(prefs.homePage, "https://www.bbc.co.uk/news"));
    CHECK(!prefs.scripts && prefs.pictures && !prefs.webFonts && prefs.lite);
    CHECK(!strcmp(prefs.downloadFolder, "Work:Downloads"));
    obd_prefs_format(&prefs, text, sizeof text);
    obd_prefs_defaults(&back);
    obd_prefs_parse(&back, text);
    CHECK(!memcmp(&prefs, &back, sizeof prefs));

    CHECK(olui_load(ui, obd_prefs_markup, obd_css, err, sizeof err));
    obd_prefs_to_ui(ui, &prefs);
    olui_layout(ui, 420, 400);
    click(ui, "pictures");
    obd_prefs_from_ui(ui, &back);
    CHECK(!back.pictures && back.lite && !strcmp(back.homePage, prefs.homePage));
    show(ui, "prefs", 420, 0);
    olui_free(ui);
}

int main(int argc, char **argv)
{
    if (argc > 1)
        outDir = argv[1];
    test_alert_confirm_prompt();
    test_login();
    test_find();
    test_downloads();
    test_prefs();
    if (failures) {
        printf("%d failed\n", failures);
        return 1;
    }
    printf("dialogs: all passed\n");
    return 0;
}
