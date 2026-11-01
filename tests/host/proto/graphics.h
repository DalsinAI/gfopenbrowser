/*
 * host tests: the graphics.library calls weblayout's painter makes, as
 * plain functions a host test defines (tests/test_wl_document.c).
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#ifndef PROTO_GRAPHICS_H
#define PROTO_GRAPHICS_H
#include <graphics/rastport.h>
#include <graphics/regions.h>
#include <graphics/text.h>
void SetFont(struct RastPort *rp, struct TextFont *font);
ULONG TextLength(struct RastPort *rp, STRPTR s, ULONG n);
void Text(struct RastPort *rp, STRPTR s, ULONG n);
void Move(struct RastPort *rp, LONG x, LONG y);
void Draw(struct RastPort *rp, LONG x, LONG y);
void SetAPen(struct RastPort *rp, ULONG pen);
void RectFill(struct RastPort *rp, LONG x0, LONG y0, LONG x1, LONG y1);
ULONG SetSoftStyle(struct RastPort *rp, ULONG style, ULONG enable);
struct Region *NewRegion(void);
void DisposeRegion(struct Region *r);
BOOL OrRectRegion(struct Region *r, const struct Rectangle *rect);
#endif
