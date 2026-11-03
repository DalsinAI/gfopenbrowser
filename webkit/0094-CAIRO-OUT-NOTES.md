# Patch 0094 (cairo out): state on 3 November 2026

Work in progress from Team 3, handed on as it is.

- **The patch:** `webkit/patches/0094-amiga-cairo-out.patch` (USE_AMIGA_GRAPHICS). It was regenerated against main's patches plus 0082c from the working tree `~/WorkingSpace/t3up-20261103/w83/WebKit` on the build machine. That's a git tree with commits "base" and "base + 0082c"; the 0094 changes are in the working tree, 54 files changed and not committed.
- **New files:**
  - `platform/graphics/amiga/` AmigaImage, NativeImageAmiga, ImageBufferAmigaBackend, PathAmiga, GraphicsAmiga, ShareableBitmapAmiga and FontCascadeAmigaInlines.h;
  - `openfont/FontOpenFont.cpp`;
  - a rewritten GraphicsContextOpenGPU.
- **Browser side** (this branch):
  - AmigaWebView paints through `AmigaImage::wrap` and GraphicsContextOpenGPU, with `paint_us`;
  - obcore-view writes its PNG with `obdt_encode_png`;
  - DumpMain;
  - CMakeLists drops cairo, pixman and png.
- **Builds:** it compiles and links. These compile errors were fixed on the way:
  - an ambiguous `NativeImage::create` (fixed with PlatformImagePtr);
  - CheckedSize types and private PathStream methods;
  - distanceSquared;
  - a GraphicsContext constructor argument;
  - strokeThickness shadowing;
  - a missing ImageBuffer.h include.
- **The open symptom:** on the i32 lab obcore-view paints almost nothing. The PNGs are tiny and nearly blank on test1 and on network pages, where main paints the full page.
- **Not yet checked:** whether GraphicsContextOpenGPU's batches reach the surface at all with no cairo surface behind it (compare `gpu_draws`/`gpu_flushes` in `paint_us` with main's), and whether AmigaImage's surface is the one obcore-view encodes.
- **Patch number:** 0094 is claimed in `~/WorkingSpace/patch-numbers.txt`.
