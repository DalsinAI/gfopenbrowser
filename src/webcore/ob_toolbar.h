/*
 * GFOpenBrowser's window: Back, Forward, Reload, Stop and Find as one
 * group of icon buttons at the left of the toolbar. The buttons are
 * GFOpenGadTools' toolbar (lib/ogt_toolbar.c) with the Chrome icon set the
 * office programs use: pictures from PROGDIR:Images/Toolbar/ (then
 * SYS:Prefs/Presets/Toolbar/), sized from the font, and GFOpenGadTools' own
 * drawn icons where a screen or theme shows no pictures (four colours, the
 * Classic theme, no strips). The user's Buttons look (GFOpenPrefs Look:
 * icons, icons with names, names only) is honoured.
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#ifndef OB_TOOLBAR_H
#define OB_TOOLBAR_H

enum { OBT_BACK, OBT_FORWARD, OBT_RELOAD, OBT_STOP, OBT_FIND, OBT_COUNT };

/* Where the address field goes beside a group of buttons barWidth wide and
 * barHeight tall that starts at left, top; the window's inside ends at
 * right (the first pixel past it). The field is a line of text and its
 * border tall and centred on the row, as the group is when the field is the
 * taller. It keeps OBT_FIELD_MIN pixels at least. */
#define OBT_FIELD_GAP 4
#define OBT_FIELD_MIN 96

typedef struct obt_row {
    int height;                 /* the row's: the taller of the two */
    int barTop;                 /* the group's top edge */
    int fieldLeft, fieldTop, fieldWidth, fieldHeight;
} obt_row;

static inline void obt_place(int left, int top, int right, int barWidth, int barHeight, int fontHeight, obt_row *row)
{
    row->fieldHeight = fontHeight + 6;
    row->height = barHeight > row->fieldHeight ? barHeight : row->fieldHeight;
    row->barTop = top + (row->height - barHeight) / 2;
    row->fieldTop = top + (row->height - row->fieldHeight) / 2;
    row->fieldLeft = left + barWidth + OBT_FIELD_GAP;
    row->fieldWidth = right - OBT_FIELD_GAP - row->fieldLeft;
    if (row->fieldWidth < OBT_FIELD_MIN)
        row->fieldWidth = OBT_FIELD_MIN;
}

/* The room the group may take in a window inside pixels wide, so that the
 * field keeps its least width: buttons that don't fit are left out (the
 * menus and keys still have them). */
static inline int obt_room(int inside)
{
    int room = inside - 2 * OBT_FIELD_GAP - OBT_FIELD_MIN - 4;
    return room > 0 ? room : 1;
}

struct Screen;
struct Window;

/* The theme and its pens on the window's screen. 1 when the buttons can be
 * drawn there; 0 leaves the caller its text buttons. */
int obt_open(struct Screen *screen);
void obt_close(void);

/* Places the group at left, top with room pixels at most. Returns its
 * height and gives its width. */
int obt_layout(struct Window *window, int left, int top, int room, int *width);
/* Moves the placed group down to top (centring it on a taller row). */
void obt_move(int top);
/* The row's background, in the theme's window colour. */
void obt_background(struct Window *window, int left, int top, int width, int height);
/* The buttons' gadgets, ids from firstId, on and off the window. */
void obt_add(struct Window *window, int firstId);
void obt_remove(struct Window *window);
void obt_draw(struct Window *window);

/* The button a gadget id names (OBT_*), or -1. */
int obt_which(int gadgetId);
void obt_enable(struct Window *window, int which, int enabled);
int obt_enabled(int which);
/* Shows a button held down, or let go. */
void obt_press(struct Window *window, int which, int down);
/* Lets go of a button whose press ended off it (no IDCMP_GADGETUP comes then). */
void obt_settle(struct Window *window);

#endif
