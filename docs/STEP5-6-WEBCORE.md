# Steps 5 and 6 in WebCore: what goes, and what replaces it

Copyright (c) 2026 AmigaChrome Team. MIT licence for this document.

This is the WebCore half of steps 5 (network on openweb.fetch) and 6
(drawing on OpenGfx and OpenGPU) of [PLAN.md](PLAN.md). It lists every file
the `PORT=Amiga` build compiles that uses curl, nghttp2, libpsl, cairo,
pixman or libpng, the WebCore interface above each one, what a replacement
must provide, and an order of patches. PLAN.md stays the plan; this file
is the map for it.

Checked on 23 October 2026, against WebKit 54fe1539 (`webkit/UPSTREAM`)
with every patch in `webkit/patches` (0001 to 0058) applied. The tree was a
blobless sparse checkout of `Source/WebCore` and `Source/cmake`; each patch
was applied with `git apply --include` limited to those two folders, so
their JavaScriptCore, WTF and bmalloc parts were left out, by design. All
54 patches applied.

## How the file lists were made

The Amiga build compiles:

- the unified lists `Sources.txt`, `platform/SourcesCairo.txt` and
  `platform/SourcesAdwaita.txt`, less `inspector/` (patch 0057);
- `WebCore_SOURCES` from `platform/Curl.cmake`, `platform/FreeType.cmake`
  and `PlatformAmiga.cmake`, less `CurlSSLVerifier.cpp` and
  `OpenSSLHelper.cpp` (patch 0022);
- `platform/network/amiga/ResourceHandleAmiga.cpp`, which is not listed but
  included by `NetworkStateNotifierAmiga.cpp`.

That is 3,909 files. Each was searched for cairo, curl and libpsl names
(`cairo_`, `USE(CAIRO)`, `curl_`, `CURL`, `Curl[A-Z]`, `psl_`). Line counts
are `wc -l`, comments and blank lines included. Sizes in the engine's link
map are not in this file: the map is on the build PC, and the cairo files
are unified (several to one object), so their bytes show as
`UnifiedSource-*.o`, not by file. The curl files are plain
`WebCore_SOURCES`, so each has an object of its own in the map.

No WebCore file calls libpng, pixman or nghttp2. libpng comes in through
cairo's PNG writer (`cairo_surface_write_to_png_stream`), pixman through
cairo, and nghttp2 through curl. zlib is not in either step: WebCore itself
uses it for `CompressionStream` (`Modules/compression/ZStream.h`), WOFF
fonts (`WOFFFileFormat.cpp`) and WebSocket compression
(`WebSocketDeflater.cpp`).

## A. Drawing

### The files

20 cairo files are compiled, 4,479 lines, plus 11 cairo headers in use
(966 lines). Six more cairo headers (430 lines: the painting engine and
operation recorder) belong to coordinated graphics and are not used. The
compiled files call 143 distinct cairo functions; 30 of them are about
fonts and glyphs.

| Job | Files (lines) | WebCore interface | Skia's version (lines) | CG's version (lines) |
| --- | --- | --- | --- | --- |
| GraphicsContext | `GraphicsContextCairo.cpp` (410) and `.h` (127); `CairoOperations.cpp` (1,339) and `.h` (176); `CairoUtilities.cpp` (391) and `.h` (118); `DrawErrorUnderline.h` (106); `FloatRectCairo.cpp` (49), `IntRectCairo.cpp` (44); `RefPtrCairo.cpp` (108) and `.h` (73), `CairoUniquePtr.h` (54) | `GraphicsContext` (`GraphicsContext.h`, 37 pure virtual calls); `PlatformGraphicsContext` | `GraphicsContextSkia.cpp` (1,387), `SkiaUtilities.cpp` (293) | `GraphicsContextCG.cpp` (1,644) |
| Paths | `PathCairo.cpp` (476) and `.h` (96) | `PathImpl` (`PathImpl.h`), chosen as `PlatformPathImpl` in `PlatformPath.h` | `PathSkia.cpp` (391) | `PathCG.cpp` (683) |
| Images and surfaces | `ImageBufferCairoBackend.cpp` (84), `ImageBufferCairoImageSurfaceBackend.cpp` (131), `ImageBufferCairoSurfaceBackend.cpp` (121), with headers (174); `NativeImageCairo.cpp` (167); `image-decoders/cairo/ImageBackingStoreCairo.cpp` (49); `ShareableBitmapCairo.cpp` (144) | `ImageBufferBackend` (`ImageBufferPlatformBackend.h`); `NativeImage` with `PlatformImagePtr` (`PlatformImage.h`); `ImageBackingStore::image()`; `ShareableBitmap` | `ImageBufferSkiaBackend.cpp` (44), `ImageBufferSkiaUnacceleratedBackend.cpp` (73), `ImageBufferSkiaSurfaceBackend.cpp` (164), `NativeImageSkia.cpp` (227), `ImageBackingStoreSkia.cpp` (46), `ShareableBitmapSkia.cpp` (142) | `ImageBufferCGBackend.cpp` (90), `ImageBufferCGBitmapBackend.cpp` (161), `NativeImageCG.cpp` (482) |
| Gradients and patterns | `GradientCairo.cpp` (279), `PatternCairo.cpp` (61) | `Gradient::createPattern()` (`Gradient.h`); `PlatformPatternPtr` (`Pattern.h`) | `GradientSkia.cpp` (217), `PatternSkia.cpp` (66) | `GradientCG.cpp` (193), `PatternCG.cpp` (102) |
| Drawing glyphs | `FontCairo.cpp` (154), `FontCairoHarfbuzzNG.cpp` (151), `FontRenderOptionsCairo.cpp` (120), `FontCascadeCairoInlines.h` (42) | `FontCascade::drawGlyphs()`, `Font::platformPathForGlyph()`, `FontRenderOptions` | `FontCascadeSkia.cpp` (175), `FontSkia.cpp` (270) | `FontCascadeCoreText.cpp` (527) |
| PNG encoding | `ImageUtilitiesCairo.cpp` (110) | `platformEncodeData()` (`ImageUtilities.h`), for canvas `toDataURL` and `toBlob` | `ImageUtilitiesSkia.cpp` (151) | `ImageUtilitiesCG.cpp` (584) |
| WebGL | `GraphicsContextGLCairo.cpp` (91) | `GraphicsContextGL` | | |

`GraphicsContextGLCairo.cpp` is compiled but empty: its code is inside
`ENABLE(WEBGL)`, which is off on the Amiga.

**Filters have no cairo file.** The software filter appliers
(`platform/graphics/filters/software/`, 20 files, 3,055 lines) work on
WebCore's `PixelBuffer` in WebCore's own code; 15 of them draw their
inputs through `GraphicsContext::drawImageBuffer()` and `fillRect()`.
Shadows use WebCore's own `ShadowBlur.cpp` (958 lines; `CairoOperations.cpp`
calls it 15 times). Skia adds five faster appliers of its own
(`filters/skia/`, 396 lines: Gaussian blur, drop shadow, colour matrix,
component transfer, source graphic), chosen through
`FilterEffect::createAcceleratedApplier()`; an OpenGPU port can do the same
later, but does not need to.

**Fonts are step 3 as much as step 6.** The FreeType and HarfBuzz files
hold cairo too: `FontPlatformDataFreeType.cpp` (59 cairo names in 363
lines; the font is a `cairo_scaled_font_t` made with cairo-ft),
`SimpleFontDataFreeType.cpp` (31 in 250), `FontCacheFreeType.cpp` (16 in
654), `FontCustomPlatformDataFreeType.cpp` (11 in 234),
`ComplexTextControllerHarfBuzz.cpp` (11 in 392),
`GlyphPageTreeNodeFreeType.cpp` (6 in 89) and `FontSetCache.cpp` (2 in
114). `FontPlatformData.h` and `FontCustomPlatformData.h` choose the
cairo types with `USE(CAIRO)`. So cairo cannot leave the map until the
font backend no longer makes cairo scaled fonts: either step 3's OpenFont
backend lands first, or step 6 brings a FreeType font object without
cairo-ft.

**Where the port is chosen.** 63 `#if USE(CAIRO)` in 30 files outside the
cairo folder pick the types at compile time: `PlatformImagePtr`,
`PlatformPathImpl`, `PlatformPatternPtr`, `PlatformGraphicsContext`,
`ImageBufferPlatformBackend`, `DashArrayElement`, the font types, and a few
behaviours. Skia, for comparison, has 211 `USE(SKIA)` in 81 files, most of
them for its GPU compositing, which the Amiga does not build. These types
are one choice per build, so cairo's image surfaces, paths and patterns
cannot be swapped one file at a time; the order below works round that.

Five `USE(CAIRO)` sites change behaviour, not types, and change back to
WebKit's default when cairo goes:

- `CanvasRenderingContext2DBase.cpp`: with cairo, canvas skips the
  transparency layer it uses for some `globalCompositeOperation` values;
- `RenderLayerFilters.cpp`, `LegacyRenderSVGResourceFilter.cpp`,
  `SVGFilterRenderer.cpp`, `FEDropShadow.h`: with cairo, filters run in
  sRGB instead of linear RGB;
- `GraphicsContextState.h`: with cairo, setting a state property to the
  value it already has is not skipped.

Two of OpenBrowser's own files draw with cairo: `src/webcore/AmigaWebView.cpp`
(`paint()` wraps the window's ARGB buffer in
`cairo_image_surface_create_for_data` and a `GraphicsContextCairo`; the
display-list experiment too) and `src/webcore/DumpMain.cpp` (paints into a
cairo surface and writes it with `cairo_surface_write_to_png`).
`src/webcore/CMakeLists.txt` links `cairo pixman-1 png`.

### What an OpenGPU port must provide

Following Skia's split, four classes and three helpers:

- **GraphicsContextOpenGPU** (Skia's is 1,387 lines): the 37 pure virtual
  calls of `GraphicsContext`, plus `save`/`restore`, transparency layers,
  `clipBounds`, and `drawGlyphs`. The last matters: cairo's
  `FontCascade::drawGlyphs()` calls `context.platformContext()`, which only
  a `GraphicsContextCairo` has, so any other context must draw glyph runs
  itself.
- **PathOpenGPU** as `PlatformPathImpl`: the 13 `add()` calls,
  `applyElements`, `transform`, `currentPoint`, `boundingRect` and
  `fastBoundingRect` of `PathImpl`, plus what `Path` asks of the platform
  path: `addPath`, `contains` (with a wind rule), `strokeContains` and
  `strokeBoundingRect`. WebCore's own `PathStream` (322 lines) already keeps
  the segments; the platform part is hit testing and flattening.
- **ImageBufferOpenGPUBackend**: `context()`, `copyNativeImage()`,
  `createNativeImageReference()`, `getPixelBuffer()`, `putPixelBuffer()`,
  `bytesPerRow()`, `canMapBackingStore()` and `debugDescription()`, over
  an ARGB32 premultiplied surface in memory (Skia's unaccelerated backend
  is 73 lines on top of 44 shared).
- **NativeImage** on an OpenGPU surface: size, `hasAlpha`,
  `singlePixelSolidColor`, `draw`, and `ImageBackingStore::image()` for
  decoded pictures (49 lines for cairo, 46 for Skia).
- **Gradient** and **Pattern** objects the context can draw with, and
  `platformEncodeData()` for PNG, which PLAN.md gives to the PNG datatype.

Cairo's ARGB32 is a native-endian 32-bit premultiplied word, so on the 68k
its bytes are A, R, G, B: the same as OpenGPU's `OGPU_FMT_ARGB32` with
`COMPOSITE_PD`. The window's buffer, decoded pictures and canvases can
therefore be shared between cairo and OpenGPU while both are linked, with
no conversion.

### OpenGPU operations each job needs

"Today" is stream v1.5 (`openamigartg/include/opengpu/stream.h`). "v1.6"
is the vector work (paths, strokes, gradients, blur, blend modes) being
added on `team3/opengpu-vector-20261023` in openamigartg, which was not
pushed when this was written; its opcodes are not checked here.

| WebCore needs | Used by | OpenGPU today | Still to add |
| --- | --- | --- | --- |
| Solid rectangle, premultiplied OVER or any operator | `fillRect`, `clearRect`, backgrounds | `FILL` (opaque), `COMPOSITE_PD` with `OGPU_PD_SOLID` | anti-aliased edges for rectangles not on whole pixels (an A8 mask or the path fill) |
| Path fill, non-zero and even-odd, anti-aliased | `fillPath`, `fillRoundedRect`, `fillEllipse`, SVG, canvas, borders | `MASK` and `COMPOSITE_PD` with an A8 mask, once a rasteriser makes the mask | the A8 path rasteriser in OpenGPU (openfont's `of_raster_a8()`, non-zero only today); even-odd |
| Strokes: width, butt/round/square caps, miter/round/bevel joins, miter limit, dashes with offset | `strokePath`, `strokeRect`, `drawLine`, borders, focus rings, text decorations, SVG | none | v1.6 strokes; dashes need checking against v1.6 |
| Linear, radial (two circles) and conic gradients; pad, repeat and reflect; colour stops | `fillRect(Gradient)`, gradient fills and strokes, CSS `conic-gradient` (cairo draws it as a mesh pattern) | none | v1.6 gradients, as a source for `COMPOSITE_PD`, clipped by a mask |
| Images with an affine transform, bilinear or nearest, premultiplied source, global alpha, any operator; pad edges | `drawNativeImage`, `drawImageBuffer`, scaled pictures, canvas `drawImage` | `COMPOSITE` (scale, bilinear), `COMPOSITE_AFFINE` (SDL blend modes only, not premultiplied) | premultiplied `COMPOSITE_AFFINE` with Porter-Duff operators and pad edges (PLAN.md step 6) |
| Image tiling with a transform and spacing | `drawPattern`, CSS backgrounds, canvas patterns | `COMPOSITE_PD` with `SRC_REPEAT` (no transform) | repeat in the affine composite |
| Clip to rectangle | `clip(FloatRect)` | `CLIP` | |
| Clip to path, clip out, rounded clips, clip to an image's alpha | `clipPath`, `clipOut`, `clipRoundedRect`, `clipToImageBuffer` | A8 slots, `COMPOSITE_PD` `OGPU_PD_IN` to intersect masks | a clip stack of A8 masks in the context (WebCore side) |
| Transparency layers with opacity, operator and blend mode | `beginTransparencyLayer`, CSS `opacity`, canvas, SVG groups | an offscreen `SURFACE`, then `COMPOSITE_PD` with `OGPU_PD_MASK_SOLID` | slot use: `OGPU_MAX_SLOTS` is 16, and nested layers, masks and image sources together can need more; the context has to rebind slots |
| Blurred shadows | `ShadowBlur` (box shadows, text shadows, canvas `shadowBlur`), `FEGaussianBlur`, `FEDropShadow` | none (`ShadowBlur` runs on the CPU and works today) | v1.6 blur, to speed `ShadowBlur` and the filters |
| The 13 Porter-Duff operators | `CompositeOperator` Clear to XOR, PlusLighter (`ADD`) | `COMPOSITE_PD` (CPU; not yet on Vulkan) | `PlusDarker` and `Difference` as operators |
| Separable and non-separable blend modes | `BlendMode` Multiply to Luminosity (15 modes) | none (the SDL blend modes are other formulas) | v1.6 blend modes |
| Glyph runs: many A8 glyph masks in one colour, at subpixel positions | `drawGlyphs` | `MASK`, one glyph per command | a batched glyph run and a glyph cache (PLAN.md step 6) |
| Stroked and shadowed text | `-webkit-text-stroke`, text shadows | | glyph outlines as paths (from OpenFont), then the stroke and blur above |
| Pixel read-back and write | `getPixelBuffer`, `putPixelBuffer` (canvas `getImageData`, filters) | the surface is memory: no OpenGPU call | |
| PNG out | canvas `toDataURL`, `toBlob` | not OpenGPU's job: the PNG datatype | the PNG datatype's encoder |

## B. Network

### The files

| Group | Files (lines) | WebCore interface above it |
| --- | --- | --- |
| Goes with curl | `CurlContext.cpp` (1,046) and `.h` (368): the context, the easy, multi and share handles, the network thread's settings (patches 0004, 0010, 0022, 0023); `CurlRequest.cpp` (693) and `.h` (171), `CurlRequestClient.h` (49), `CurlResponse.h` (76); `CurlRequestScheduler.cpp` (292) and `.h` (97), `CurlRequestSchedulerClient.h` (35); `CurlFormDataStream.cpp` (180) and `.h` (65); `CurlMultipartHandle.cpp` (331), `.h` (97), `CurlMultipartHandleClient.h` (42); `CurlProxySettings.cpp` (154) and `.h` (96); `CurlSSLHandle.cpp` (106) and `.h` (110), `amiga/CurlSSLHandleAmiga.cpp` (42); `CurlStream.cpp` (201) and `.h` (91), `CurlStreamScheduler.cpp` (194) and `.h` (73) | none directly: `ResourceHandle` reaches them through `ResourceHandleAmiga.cpp` |
| Goes with libpsl | `PublicSuffixStoreCurl.cpp` (75) | `PublicSuffixStore::platformIsPublicSuffix()` and `platformTopPrivatelyControlledDomain()` |
| Stays, without curl | `amiga/ResourceHandleAmiga.cpp` (461: requests, redirects, cookies, the HTTP cache hook, files, synchronous loads); `ResourceErrorCurl.cpp` (92) and `ResourceError.h` (70) (the error domain is `CURLcode`); `ResourceResponseCurl.cpp` (145) and `ResourceResponse.h` (69) (built from a `CurlResponse`); `AuthenticationChallengeCurl.cpp` (136) and `.h` (62) (built from a `CurlResponse`) | `ResourceHandle` and `ResourceHandleClient`; `ResourceError`, `ResourceResponse`, `AuthenticationChallenge` |
| Stays as it is (no curl calls, despite the folder) | `CookieJarDB.cpp` (679) and `.h` (111) (SQLite), `CookieUtil.cpp` (190) and `.h` (45), `DNSResolveQueueCurl.cpp` (60) and `.h` (47) (empty: no DNS prefetch), `ProtectionSpaceCurl.cpp` (40) and `.h` (66), `ResourceRequestCurl.cpp` (67) and `ResourceRequest.h` (87), `CertificateInfoCurl.cpp` (84) and `CertificateInfo.h` (68) | `CookieJar` through `src/webcore/AmigaCookieJar.cpp`; `ResourceRequest`; `CertificateInfo` |

So 24 files and 4,684 lines go with curl (11 `.cpp`, 3,314 lines; 13
headers, 1,370 lines), one file of 75 lines goes with libpsl, 7 files and
1,035 lines lose their curl types, and 12 files and 1,544 lines stay. They
call 22 curl functions; `CurlContext.cpp` alone sets 56 `CURLOPT_` options.
Six `#if USE(CURL)` outside the curl folder (`MIMETypeRegistry.cpp`,
`WebCorePersistentCoders.cpp`, `DNSResolveQueue.cpp`, `ProtectionSpace.h`,
`ResourceHandle.cpp`, `SynchronousLoaderClient.cpp`) choose the curl
port's types; the other types are picked by the include path
(`platform/network/curl`).

Findings:

- **There are no WebSockets today.** `CurlStream` and
  `CurlStreamScheduler` (559 lines) serve WebKit's network process, which
  this port does not have. Nothing in WebCore calls them, and the page is
  made with `pageConfigurationWithEmptyClients()`, whose
  `EmptySocketProvider` returns no channel. So a WebSocket has no channel
  on the Amiga today, and replacing curl loses no WebSocket support. A
  WebSocket later needs a `SocketProvider` that makes a
  `ThreadableWebSocketChannel`, over a byte stream openweb.fetch does not
  have yet.
- **No authentication dialogue.** `ResourceHandleAmiga.cpp` gives curl a
  user and password only when the URL has them (HTTP Basic), and every
  `AuthenticationChallenge` call is empty. A 401 is shown as the page.
- **Proxies** come from `CurlProxySettings` (environment variables), and
  patch 0010's `OB_FETCH_PROXY` experiment is inside `CurlContext.cpp`.
- **Redirects and cookies are already WebCore's.** `ResourceHandleAmiga.cpp`
  follows redirects itself and asks `AmigaCookieJar` for `Cookie` and hands
  it each `Set-Cookie`. openweb.fetch was built the same way (it never
  follows a redirect and keeps no cookie), so neither changes.
- **Compressed bodies** are decoded by curl today. With openweb.fetch the
  host decodes them (`WF_DECODE`); PLAN.md step 5 moves that to
  gzip.datatype and brotli.datatype on the Amiga.

### What a backend on openweb.fetch must provide

The backend is the request half of `ResourceHandleAmiga.cpp`: what
`amigaStartCurlRequest()`, `cancel()` and the `ResourceHandleCurlDelegate`
do today. `AmigaLoaderStrategy.cpp` (the queue of six requests per host)
and the HTTP cache hook (patch 0026) stay above it.

| ResourceHandle needs | curl today | openweb.fetch/1 |
| --- | --- | --- |
| Method, URL, headers | `CurlRequest` | `wf_open()` with the request text from `wf_request()` |
| Request body: bytes, files, blobs, streamed | `CurlFormDataStream` | `WF_BODY` with the first part, then `wf_send()` |
| Status, headers (every `Set-Cookie`), HTTP version, length | `CurlResponse`, `ResourceResponse(CurlResponse&)` | `wf_head()` and `wf_header()`; `ResourceResponse` needs a constructor from it |
| Body chunks; WebCore takes its time over the response | `curlDidReceiveData`; curl paused until `completeDidReceiveResponse()` | `wf_read()`; simply not reading waits (the host reads 1 MB ahead) |
| End, errors, cancel | `curlDidComplete`, `curlDidFailWithError`, `cancel()` | `wf_read()`'s end word, `WF_E_*`, `wf_cancel()` and `wf_close()`; `ResourceError` needs a domain for `WF_E_*` |
| TLS through OpenTLS, certificate summary | curl's OpenTLS backend (patch 0022) | the host's OpenSSL 3 today; the info text has the subject, issuer and expiry |
| HTTP/2, many requests on one connection | nghttp2 (patch 0023) | no: kept HTTP/1.1 connections, 6 per host |
| Proxies | `CurlProxySettings` | no |
| HTTP auth | Basic from the URL only | no, but it is only headers: Basic can be added by WebCore; Digest needs WebCore's challenge code |
| `multipart/x-mixed-replace` | `CurlMultipartHandle` | no: the parser would move to the Amiga side |
| Load timings | `curl_easy_getinfo` | none; `NetworkLoadMetrics` can be timed on the Amiga |
| Waiting | curl's own thread, `curl_multi_poll` | each `wf_*` call is a `DoIO()` on one IORequest per `wf_service`; the backend needs `SendIO()`-style calls and a signal bit for WebKit's run loop, or a task of its own that posts results to the main run loop as curl's thread does |
| Without a services card | bsdsocket.library on the Amiga | not yet (no LAN transport, no Amiga-side TLS) |

The last five rows are the first half of PLAN.md step 5 (TLS through
OpenTLS, HTTP/2, proxies, auth, a transport without a services card).
WebSocket is in that list too, but the port has none to lose.

### Public suffixes without libpsl

WebCore asks two things (`PublicSuffixStore`): is a name a public suffix,
and what is its registrable domain. `PublicSuffixStore.cpp` caches the
answers and calls the two platform functions. Users: `CookieJarDB.cpp`
(refuses cookies set on a public suffix), `SecurityOrigin.cpp` and
`Document.cpp` (sites, `document.domain`), `CookieStore.cpp`,
`ContentSecurityPolicySourceList.cpp`, `ResourceLoadStatistics.cpp`,
`Quirks.cpp` and `URLMatch.cpp`.

The data is Mozilla's Public Suffix List, which is data, not a library.
On 23 October 2026 it had 10,397 rules (wildcard and exception rules
included) in 147,930 bytes of rule text, 44,260 bytes gzipped. A host
script can turn it into a small sorted file (for example reversed labels,
with the wildcard and exception marks), read once into memory, and the
lookup is the list's own published algorithm: the longest matching rule,
exceptions first, `*` for an unlisted top-level name. The file is updated
by running the script again, not by a new engine. IDN names arrive as
Punycode from WTF's URL parser, so the file keeps the list's names in
Punycode too.

## C. The order of patches

Each patch builds on its own and is checked with the test pages on lab
copies of real instances, as PLAN.md says. "Map" is what leaves the
engine's link map.

### Step 5, network (first: it is smaller)

1. **Public suffixes from a file** (patch 0070). `PublicSuffixStoreAmiga.cpp` in place
   of `PublicSuffixStoreCurl.cpp`, the host script and the data file in
   `PROGDIR:`. Map: `libpsl.a`. Needs nothing from openweb.fetch.
2. **Unused curl code out** (patch 0071; patch 0010 stays until curl goes in 5). `CurlStream` and `CurlStreamScheduler` (and
   `CurlContext::streamScheduler()`), and patch 0010's proxy experiment if
   no one still uses it. Map: their objects (559 lines of source for the
   first two).
3. **The types without curl** (patch 0072). `ResourceError` with its own domain,
   `ResourceResponse` and `AuthenticationChallenge` built from a plain
   response head. curl still does the work. Map: nothing; this is what makes 4 and 5 small.
4. **The openweb.fetch backend beside curl** (patch 0073; `OB_NET=openweb` until patch 0075). The request half of
   `ResourceHandleAmiga.cpp` on `wf_*` (asynchronous, as above), with
   `CurlFormDataStream`'s job done by a `FormData` reader feeding
   `wf_send()`. A switch picks curl or openweb.fetch, so both are measured
   on one build. Map: grows by the client (`webfetch.c` is 249 lines).
   Needs the first half of step 5 in openamigaservice before it can be
   the default.
5. **curl out** (patch 0075; written, not yet built). `platform/Curl.cmake` no longer included; the files that
   stay are listed in `PlatformAmiga.cmake`; `CurlContext`, `CurlRequest`,
   `CurlRequestScheduler`, `CurlFormDataStream`, `CurlProxySettings`,
   `CurlSSLHandle` and `CurlSSLHandleAmiga` go, and `CurlMultipartHandle`
   goes too (`multipart/x-mixed-replace` arrives as one body, as it did on
   patch 0073's backend). `AmigaNetworkSetup.c` opens no bsdsocket.library:
   the native transport opens its own. Every load asks for `WF_DECODE`
   (the native transport decodes through the datatypes, openamigaservice
   #15). Map:
   `libcurl.a`, `libnghttp2.a` and the curl objects. Step 5 is done when
   the map shows them gone.

### Step 6, drawing

The platform types (`PlatformImagePtr`, `PlatformPathImpl` and the rest)
are one choice per build, so cairo's surfaces and paths go in one patch,
the last but one. Before that, a new `GraphicsContext` takes over the
drawing one job at a time. It draws into the same ARGB memory as cairo
(the byte order matches, see above), keeps its own copy of the state (the
transform, the clip stack, save and restore), and hands any call it does
not do yet to a `GraphicsContextCairo` on the same pixels with the same
state. It counts the calls it hands on, so each patch shows how much of a
page's painting is still cairo's, beside `OBVIEW_BENCH`'s `paint_ms`, and
the pictures are compared with cairo's pixel for pixel as in the display-list
experiment (`WEBCORE_PORT.md`).

1. **PNG through the PNG datatype** (patch 0074). `platformEncodeData()` and
   `DumpMain.cpp` write PNG through datatypes.library. Map: `libpng.a` and
   cairo's PNG object, as long as nothing else in cairo pulls that object
   in (to be checked on the map).
2. **GraphicsContextOpenGPU for the window, rectangles and pictures.**
   `AmigaWebView::paint()` uses it. It does solid fills, `clearRect`,
   rectangle clips, and images (the pixels of a cairo image surface are
   ARGB memory) through `FILL`, `COMPOSITE_PD` and the affine composite;
   everything else goes to cairo. Map: nothing yet; measured by `paint_ms`
   and the count.
3. **Paths and fills.** Path fill (non-zero and even-odd), strokes with
   caps, joins and dashes, clips to paths as A8 masks, gradients and
   patterns, read from WebCore's own `Path` segments and `Gradient` stops,
   so `PathCairo` is not needed. Needs OpenGPU's rasteriser and v1.6
   strokes and gradients.
4. **Text.** `drawGlyphs` as batched glyph runs from a glyph cache, with
   the glyph masks from step 3's OpenFont backend (or, if step 3 is not
   ready, from the FreeType face cairo-ft holds, as FreeType is still
   linked then). Needs the batched glyph run in OpenGPU.
5. **Layers, shadows and blend modes.** Transparency layers on offscreen
   surfaces, `clipToImageBuffer`, the shadows (`ShadowBlur` on OpenGPU
   blur), and the 15 blend modes and the last two operators. After this
   the count of calls handed to cairo should be zero on the test pages.
6. **cairo out.** `USE(CAIRO)` off and the port's own types on:
   `ImageBufferOpenGPUBackend`, `NativeImage` on an OpenGPU surface,
   `PathOpenGPU` on `PathStream`, `ImageBackingStore`, `ShareableBitmap`,
   `Gradient` and `Pattern`; `platform/Cairo.cmake` and
   `SourcesCairo.txt` no longer included; `AmigaWebView.cpp` and the
   programs' link lines without cairo. The five behaviour sites above go
   back to WebKit's defaults (canvas layers, filters in linear RGB), which
   needs checking on the test pages. Needs the font backend without
   cairo-ft (step 3). Map: `libcairo.a`, `libpixman-1.a` and the cairo
   bundles. Step 6 is done.
7. **Faster filters (later, optional).** OpenGPU appliers for Gaussian
   blur, drop shadow and the colour matrix, chosen as Skia's are. Map:
   nothing goes; measured by time on filter-heavy pages.

### Between the two steps

Step 5 does not depend on step 6, and its patches 1 and 2 need nothing
outside this repository. Step 6's patch 6 depends on step 3. Each of the
six libraries leaves the map in exactly one patch: libpsl in 5.1, curl and
nghttp2 in 5.5, libpng in 6.1, cairo and pixman in 6.6.
