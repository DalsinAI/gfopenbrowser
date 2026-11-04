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

## 3 November 2026, 21:35 GMT: the blank page was the PNG writer

On i32 (labs t3up-pf-c94-i32 and t3up-pf-base-i32) the paint report matches main: test1 has 370 draws against 373, wiki 1,103 against 1,134, and no calls are left undrawn. Wiki paints in 301,012 us against main's 643,834. The PNGs were palette files in the Workbench's 4 colours. obdt_encode_png (patch 0074) still asks for `openpng`, but the datatype is called gfopenpng.datatype since the rename. So OS 3.2's png.datatype wrote the picture, remapped to 4 pens. Main's obcore-view never showed this because it wrote its PNGs with cairo. 0094 now asks for `gfopenpng` first, and that fixes canvas toDataURL/toBlob as well.

## 4 November 2026, 02:00 GMT: paths with arcs, rectangles and rounded corners were empty

The OB_PAINT_TRACE run on wiki showed the photo drawn. Inside the icons' layers, though, the clip was `0,0-0,0`, and the scrollbar's fillPaths reported drawn while nothing landed. The cause is PathAmiga. It kept WebKit's segments as they came, and PathArc, PathArcTo, PathEllipse, PathEllipseInRect, PathRect, PathRoundedRect and PathContinuousRoundedRect can't give elements (`canApplyElements` is false). So `Path::applyElements` gave nothing for any path holding one of them: rounded rectangles (the scrollbar), arcs (SVG icons), and rectangle and rounded clips (an empty mask, which is the empty clip). PathAmiga now turns those segments into moves, lines, Beziers and closes as they are added, the way cairo's path held them. Arcs follow cairo_arc and cairo_arc_negative, arcTo follows PathCairo's working, and rounded rectangles use PathImpl::beziersForRoundedRect.
