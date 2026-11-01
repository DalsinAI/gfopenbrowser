/* host tests: see exec/types.h. MIT, Copyright (c) 2026 AmiCradle Team. */
#ifndef GRAPHICS_GFX_H
#define GRAPHICS_GFX_H
#include <exec/types.h>
struct BitMap { UWORD BytesPerRow, Rows; };
struct Rectangle { WORD MinX, MinY, MaxX, MaxY; };
#endif
