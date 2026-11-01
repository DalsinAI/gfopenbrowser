/* host tests: see exec/types.h. MIT, Copyright (c) 2026 AmiCradle Team. */
#ifndef GRAPHICS_RASTPORT_H
#define GRAPHICS_RASTPORT_H
#include <graphics/gfx.h>
struct Layer;
struct RastPort { struct Layer *Layer; struct BitMap *BitMap; struct TextFont *Font; WORD cp_x, cp_y; UWORD FgPen; };
#endif
