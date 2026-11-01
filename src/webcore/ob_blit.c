/*
 * GFOpenBrowser: putting the page's 32-bit pixels into a window.
 *
 * On a graphics card's screen (Picasso96 or CyberGraphX), cybergraphics'
 * WritePixelArray() copies them as they are. On a native (AGA or ECS)
 * screen they are dithered to a 6x6x6 colour cube of pens obtained from
 * the screen's colour map (ObtainBestPen), and written with
 * WriteChunkyPixels(); the fewer colours the screen has, the coarser the
 * picture.
 *
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include <exec/memory.h>
#include <graphics/gfx.h>
#include <graphics/rastport.h>
#include <graphics/view.h>
#include <intuition/screens.h>
#include <proto/exec.h>
#include <proto/graphics.h>
#include <string.h>

#include "ob_blit.h"
#include "ob_cgx.h"

struct Library *CyberGfxBase;

static struct Screen *blitScreen;
static int useCyberGfx;
static LONG pens[216];
static int pensObtained;
static UBYTE *chunky;
static ULONG chunkySize;
static struct RastPort tempRP;
static struct BitMap *tempBM;
static int tempWidth;

static const UBYTE bayer[4][4] = { { 0, 8, 2, 10 }, { 12, 4, 14, 6 }, { 3, 11, 1, 9 }, { 15, 7, 13, 5 } };

int ob_blit_open(struct Screen *screen)
{
    blitScreen = screen;
    useCyberGfx = 0;
    if (!CyberGfxBase)
        CyberGfxBase = OpenLibrary((CONST_STRPTR)"cybergraphics.library", 41);
    if (CyberGfxBase && ob_GetCyberMapAttr(screen->RastPort.BitMap, CYBRMATTR_ISCYBERGFX)
        && ob_GetCyberMapAttr(screen->RastPort.BitMap, CYBRMATTR_DEPTH) > 8)
        useCyberGfx = 1;
    if (!useCyberGfx && !pensObtained) {
        int i;
        for (i = 0; i < 216; i++) {
            ULONG r = (i / 36) * 51, g = ((i / 6) % 6) * 51, b = (i % 6) * 51;
            pens[i] = ObtainBestPen(screen->ViewPort.ColorMap, r * 0x01010101UL, g * 0x01010101UL, b * 0x01010101UL,
                OBP_Precision, PRECISION_IMAGE, TAG_DONE);
        }
        pensObtained = 1;
    }
    return useCyberGfx;
}

void ob_blit_close(void)
{
    if (pensObtained && blitScreen) {
        int i;
        for (i = 0; i < 216; i++) {
            if (pens[i] >= 0)
                ReleasePen(blitScreen->ViewPort.ColorMap, pens[i]);
        }
    }
    pensObtained = 0;
    if (tempBM) {
        WaitBlit();
        FreeBitMap(tempBM);
        tempBM = NULL;
    }
    if (chunky)
        FreeVec(chunky);
    chunky = NULL;
    chunkySize = 0;
    if (CyberGfxBase)
        CloseLibrary(CyberGfxBase);
    CyberGfxBase = NULL;
    blitScreen = NULL;
}

int ob_blit_truecolour(void)
{
    return useCyberGfx;
}

void ob_blit(struct RastPort *rp, int x, int y, const unsigned char *argb, int stride, int width, int height)
{
    int row, col;
    if (width <= 0 || height <= 0)
        return;
    if (useCyberGfx) {
        ob_WritePixelArray((APTR)argb, 0, 0, stride, rp, x, y, width, height, RECTFMT_ARGB);
        return;
    }
    /* Native screens: one row at a time through a one-line chunky buffer. */
    if ((ULONG)width > chunkySize) {
        if (chunky)
            FreeVec(chunky);
        chunkySize = (width + 15) & ~15;
        chunky = AllocVec(chunkySize, MEMF_ANY);
        if (!chunky) {
            chunkySize = 0;
            return;
        }
    }
    if (!tempBM || tempWidth < width) {
        if (tempBM) {
            WaitBlit();
            FreeBitMap(tempBM);
        }
        tempWidth = (width + 15) & ~15;
        tempBM = AllocBitMap(tempWidth, 1, rp->BitMap->Depth, 0, rp->BitMap);
        InitRastPort(&tempRP);
        tempRP.BitMap = tempBM;
    }
    for (row = 0; row < height; row++) {
        const unsigned char *p = argb + row * stride;
        const UBYTE *dither = bayer[(y + row) & 3];
        for (col = 0; col < width; col++, p += 4) {
            int d = dither[(x + col) & 3] * 16;
            int index = ((p[1] * 5 + d) >> 8) * 36 + ((p[2] * 5 + d) >> 8) * 6 + ((p[3] * 5 + d) >> 8);
            chunky[col] = (UBYTE)pens[index];
        }
        WritePixelLine8(rp, x, y + row, width, chunky, &tempRP);
    }
}
