/*
 * GFOpenBrowser: the two cybergraphics.library calls it uses, for graphics
 * card screens (Picasso96 and CyberGraphX both provide the library). The
 * names, numbers and call offsets are the library's published interface.
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#ifndef OB_CGX_H
#define OB_CGX_H

#include <exec/types.h>
#include <exec/libraries.h>

struct BitMap;
struct RastPort;

extern struct Library *CyberGfxBase;

#define CYBRMATTR_DEPTH      0x80000007UL
#define CYBRMATTR_ISCYBERGFX 0x80000008UL
#define RECTFMT_ARGB         2UL

/* GetCyberMapAttr(bitMap, attribute): offset -96, a0 / d0. */
static inline ULONG ob_GetCyberMapAttr(struct BitMap *bitMap, ULONG attribute)
{
    register ULONG result __asm("d0") = attribute;
    register struct BitMap *a0 __asm("a0") = bitMap;
    register struct Library *a6 __asm("a6") = CyberGfxBase;
    __asm volatile ("jsr a6@(-96:W)" : "+r" (result), "+r" (a0) : "r" (a6) : "d1", "a1", "fp0", "fp1", "cc", "memory");
    return result;
}

/* WritePixelArray(src, srcX, srcY, srcMod, rp, x, y, w, h, format):
 * offset -126, a0 d0 d1 d2 a1 d3 d4 d5 d6 d7. */
static inline ULONG ob_WritePixelArray(APTR src, UWORD srcX, UWORD srcY, UWORD srcMod, struct RastPort *rp,
    UWORD x, UWORD y, UWORD width, UWORD height, UBYTE format)
{
    register ULONG result __asm("d0") = srcX;
    register ULONG d1 __asm("d1") = srcY;
    register ULONG d2 __asm("d2") = srcMod;
    register ULONG d3 __asm("d3") = x;
    register ULONG d4 __asm("d4") = y;
    register ULONG d5 __asm("d5") = width;
    register ULONG d6 __asm("d6") = height;
    register ULONG d7 __asm("d7") = format;
    register APTR a0 __asm("a0") = src;
    register struct RastPort *a1 __asm("a1") = rp;
    register struct Library *a6 __asm("a6") = CyberGfxBase;
    __asm volatile ("jsr a6@(-126:W)"
        : "+r" (result), "+r" (d1), "+r" (a0), "+r" (a1)
        : "r" (d2), "r" (d3), "r" (d4), "r" (d5), "r" (d6), "r" (d7), "r" (a6)
        : "fp0", "fp1", "cc", "memory");
    return result;
}

#endif
