/*
 * GFOpenBrowser's window: its five toolbar buttons, drawn by
 * GFOpenGadTools' toolbar (see ob_toolbar.h).
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include <string.h>
#include <exec/types.h>
#include <intuition/intuition.h>
#include <intuition/screens.h>
#include <proto/intuition.h>

#include "ogt_draw.h"
#include "ogt_icons.h"
#include "ogt_iconset.h"
#include "ogt_look.h"
#include "ogt_toolbar.h"

#include "ob_toolbar.h"

#define BACKGROUND "window"

/* Each button: its name (shown by the Buttons look that has names), the
 * icon drawn when there is no picture, and its picture in the Chrome set.
 * Find stands a little apart: it is not one of the four that go somewhere. */
static const ogt_tool tools[OBT_COUNT] = {
    [OBT_BACK]    = { OBT_BACK + 1, "Back", OGT_ICON_GO_BACK, 0, 0, 0, "back", 0 },
    [OBT_FORWARD] = { OBT_FORWARD + 1, "Forward", OGT_ICON_GO_FORWARD, 0, 0, 0, "forward", 0 },
    [OBT_RELOAD]  = { OBT_RELOAD + 1, "Reload", OGT_ICON_RELOAD, 0, 0, 0, "reload", 0 },
    [OBT_STOP]    = { OBT_STOP + 1, "Stop", OGT_ICON_CANCEL, 0, 0, 0, "stop", 0 },
    [OBT_FIND]    = { OBT_FIND + 1, "Find", OGT_ICON_SEARCH, 1, 0, 0, "find", 0 },
};

static ogt_theme theme;
static ogt_ctx context;
static ogt_toolbar bar;
static int isOpen, style;
static struct Gadget *added;
static int addedCount;

int obt_open(struct Screen *screen)
{
    int mode = OGT_LIGHT;
    if (isOpen)
        return 1;
    ogt_look_load(&theme, &mode);
    if (!ogt_ctx_init(&context, screen, &theme, mode)) {
        ogt_theme_free(&theme);
        return 0;
    }
    ogt_iconset_attach();
    style = ogt_buttons_style();
    ogt_toolbar_set(&bar, tools, OBT_COUNT, style);
    isOpen = 1;
    return 1;
}

void obt_close(void)
{
    if (!isOpen)
        return;
    ogt_ctx_free(&context);
    ogt_theme_free(&theme);
    isOpen = 0;
}

int obt_layout(struct Window *window, int left, int top, int room, int *width)
{
    int height;
    /* A narrower window may have dropped the names; a wider one has them back. */
    bar.style = style;
    height = ogt_toolbar_layout(&bar, window->RPort, left, top, room);
    *width = bar.w;
    return height;
}

void obt_move(int top)
{
    int i;
    for (i = 0; i < bar.n; i++)
        bar.box[i].y = top;
    bar.y = top;
}

void obt_background(struct Window *window, int left, int top, int width, int height)
{
    ogt_fill(&context, window->RPort, BACKGROUND, left, top, width, height);
}

void obt_add(struct Window *window, int firstId)
{
    struct Gadget *g;
    added = ogt_toolbar_gadgets(&bar, firstId);
    addedCount = 0;
    for (g = added; g; g = g->NextGadget)
        addedCount++;
    if (added)
        AddGList(window, added, (UWORD)-1, addedCount, NULL);
}

void obt_remove(struct Window *window)
{
    if (added)
        RemoveGList(window, added, addedCount);
    added = NULL;
    addedCount = 0;
    bar.pressed = -1;
}

void obt_draw(struct Window *window)
{
    ogt_toolbar_draw(&bar, &context, window->RPort, BACKGROUND);
}

int obt_which(int gadgetId)
{
    return added ? ogt_toolbar_index(&bar, gadgetId) : -1;
}

int obt_enabled(int which)
{
    return which >= 0 && which < bar.n && !bar.tool[which].disabled;
}

void obt_enable(struct Window *window, int which, int enabled)
{
    if (which < 0 || which >= bar.n || obt_enabled(which) == (enabled != 0))
        return;
    ogt_toolbar_enable(&bar, which + 1, enabled);
    if (!enabled && bar.pressed == which)
        bar.pressed = -1;
    if (added)
        ogt_toolbar_draw_one(&bar, &context, window->RPort, which, bar.pressed == which, BACKGROUND);
}

void obt_press(struct Window *window, int which, int down)
{
    if (which < 0 || which >= bar.n)
        return;
    if (down && !obt_enabled(which))
        return;
    bar.pressed = down ? which : -1;
    ogt_toolbar_draw_one(&bar, &context, window->RPort, which, down, BACKGROUND);
}

void obt_settle(struct Window *window)
{
    if (added && bar.pressed >= 0 && !(bar.gad[bar.pressed].Flags & GFLG_SELECTED))
        obt_press(window, bar.pressed, 0);
}
