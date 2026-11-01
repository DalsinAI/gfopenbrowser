/* host tests: see exec/types.h. MIT, Copyright (c) 2026 AmiCradle Team. */
#ifndef GRAPHICS_TEXT_H
#define GRAPHICS_TEXT_H
#include <exec/types.h>
#define FSF_UNDERLINED 1
#define FSF_BOLD 2
#define FSF_ITALIC 4
struct TextFont { UWORD tf_YSize, tf_XSize, tf_Baseline; };
#endif
