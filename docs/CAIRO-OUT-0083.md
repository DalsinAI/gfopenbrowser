# Cairo out, item 7: patch 0083 (cairo, pixman and libpng leave the engine)

Status, 1 November 2026: **Not built: needs the WebKit build PC.** No
Cradle on that PC is reachable from here (Cradle's discovery finds only
daletop, whose stoves are os32, os32-gcc16 and aros68k-gcc16; none builds
WebKit). The patch itself is **not written yet**. What 0083 has to change
is WebKit's platform layer with `USE(CAIRO)` switched off, which can only be
written against the compiler on that PC. This file is the work list,
checked here against the patches 0001 to 0093 and `src/webcore`.

## What still uses cairo after 0082

| Where | What cairo does there | What replaces it |
|---|---|---|
| `GraphicsContextOpenGPU` (0076-0082) | the `m_cairo` fallback: shadows (ShadowBlur), putting transparency layers on the window, `clipToImageBuffer`, spelling markers, stroked/shadowed/gradient text, `drawGlyphsImmediate`, pictures not ARGB32/RGB24, blend modes other than normal, clips cairo alone keeps (outer masks under a layer) | each on the writer: shadows by A8 + `BLUR` (blur sizes need Dale, #133), layers by an owned ARGB32 surface + `COMPOSITE_PD` (opacity by `IN_REVERSE` of a solid alpha, then the clip mask; blend modes by `OGPU_BLEND_*`), `clipToImageBuffer` as an A8 mask from the buffer's alpha, the error underline as a filled path (WebKit's `DrawErrorUnderline.h`), stroked text by `PATH_STROKE` of openfont outlines; the class stops deriving its fallback and `GraphicsContextCairo` goes |
| `NativeImage` / `PlatformImagePtr` | a `cairo_surface_t` (image surface) | an engine image: premultiplied ARGB32 pixels, stride, size, refcounted (`NativeImageAmiga`); the datatypes decoder (`DatatypesImageDecoder`, `ImageBackingStoreCairo.cpp` in 0002) fills it |
| `ImageBuffer` backends | `ImageBufferCairoImageSurfaceBackend` | an ARGB32 memory backend whose `GraphicsContext` is a `GraphicsContextOpenGPU` on its pixels (canvas, filters, layers) |
| `Path` | `PathCairo` | WebKit's platform-independent `PathStream` (what ports without a path library use); `GraphicsContextOpenGPU::beginPath` already reads `Path` elements |
| `Gradient`, `Pattern` | `GradientCairo` (`createPattern`), `PatternCairo` | nothing platform-side: 0079 builds OpenGPU's gradient and pattern words from WebCore's own `Gradient` and `Pattern` |
| Fonts (0090) | `FontPlatformData`'s `cairo_scaled_font_t` and the cairo user font face whose callbacks fill openfont outlines; extents through `cairo_scaled_font_glyph_extents` | glyph extents and advances from openfont (`OF_GlyphExtents`, `OF_GlyphAdvancePx`, already used by `OpenFontFace`); drawing is 0082's `OF_DrawGlyphsA`; colour emoji through `OF_GlyphColorBitmap` into a picture drawn by `obgpu_image` |
| `ImageUtilitiesCairo.cpp` (0074) | reads the `cairo_surface_t` to encode PNG through the datatype | the same `obdt_encode_png` on the engine image's pixels |
| `src/webcore/AmigaWebView.cpp` | wraps the window's ARGB32 pixels in a cairo image surface for `GraphicsContextOpenGPU`, and the display-list check replays with cairo | the context takes the pixels (address, stride, size) directly; the display-list check replays into a second `GraphicsContextOpenGPU` |
| `src/webcore/obcore_view.c`, `DumpMain.cpp` | `cairo_surface_write_to_png` for test PNGs, a cairo surface to paint into | `obdt_encode_png` (as 0074), the pixels directly |
| `src/webcore/CMakeLists.txt` | links `cairo pixman-1 png` and adds `include/cairo` | gone; `OB_PAINT=cairo` goes with it |
| `scripts/build-webcore.sh`, `OAB_DEPS` | cairo, pixman and libpng from the OpenAmiga ports prefix | dropped from the prefix's list; `OptionsAmiga.cmake`: `USE_CAIRO` off |

libpng is linked only for cairo (PNG out is the PNG datatype's since 0074,
#93), and pixman only through cairo, so both leave with it.

## Order on the build PC

1. `NativeImage`, `ImageBuffer` backend, `PathStream`: the engine builds
   with `USE(CAIRO)` off and draws nothing but what OpenGPU draws today.
2. Fonts without the cairo face (0090's `FontPlatformDataOpenFont.cpp`,
   `OpenFontFace.cpp`, `SimpleFontDataOpenFont.cpp`).
3. The fallbacks in `GraphicsContextOpenGPU`, one by one, each with a host
   test against cairo while cairo is still on the host (the tests keep
   cairo as a host-only reference: `tests/run-host-tests.sh` skips them
   without cairo's headers).
4. `src/webcore` and the build scripts; the map file shows no `cairo_`,
   `pixman_` or `png_` symbol.

Each step: the engine built on the WebKit build PC, the test pages
(`obcore-view`, OBTests) compared against the cairo build's PNGs, and the
OBTests run on a lab Amiga through Cradle.
