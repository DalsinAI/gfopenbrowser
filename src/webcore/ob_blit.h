/*
 * GFOpenBrowser: putting the page's 32-bit pixels into a window (ob_blit.c).
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#ifndef OB_BLIT_H
#define OB_BLIT_H

struct Screen;
struct RastPort;

/* For the screen the window is on; returns 1 for a true-colour (graphics
 * card) screen, 0 for a native one, where pages are dithered to pens. */
int ob_blit_open(struct Screen *screen);
void ob_blit_close(void);
int ob_blit_truecolour(void);

/* Copies width x height pixels (bytes A, R, G, B) to the RastPort at x, y. */
void ob_blit(struct RastPort *rp, int x, int y, const unsigned char *argb, int stride, int width, int height);

#endif
