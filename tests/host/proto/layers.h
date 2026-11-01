/* host tests: see proto/graphics.h. MIT, Copyright (c) 2026 AmiCradle Team. */
#ifndef PROTO_LAYERS_H
#define PROTO_LAYERS_H
#include <graphics/regions.h>
struct Layer;
struct Region *InstallClipRegion(struct Layer *layer, struct Region *region);
#endif
