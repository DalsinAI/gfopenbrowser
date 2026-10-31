# The OpenBrowser plan

This is the one place that says where OpenBrowser is going and what comes
next. Every PR that finishes a step, or changes a decision, updates this file
in the same PR. The older documents (PHASE_PLAN.md, JS_ENGINE_PLAN.md,
WEBCORE_PORT.md) explain how we got here. Where they disagree with this file,
this file is right.

Last checked: 22 October 2026, against the engine built from main at 96f8c89
(`OpenBrowser.engine`, 90.2 MB stripped) and its link map.

## The decisions

Taken on 17 October 2026 (design 0.5) and still in force:

1. **OpenBrowser is an Amiga program.** It is WebKit's `PORT=Amiga`, WebCore
   and JavaScriptCore on the 68040 with its FPU. Once step 4 lands,
   JavaScript runs in javascript.library's engine process
   (JavaScriptEngine), as the javascript.library design decided on 22
   October; everything else stays in the browser's process. Extra power
   comes from discrete services (autoconfig cores, the services card, a LAN
   Cradle), never from a browser running on the host.
2. **Nothing third-party except SQLite.** Every other library the engine
   links is one of ours, including what sits behind our libraries (no
   HarfBuzz inside openfont.library, for example). ICU is the one licence
   exception, as unicode.library, until step 7 replaces its code.
3. **Text is OpenFont** (openfont.library). It replaces FreeType, HarfBuzz
   and fontconfig. The Unicode work WebKit gives ICU belongs to
   unicode.library (22 October), not OpenFont.
4. **Drawing is OpenGfx and OpenGPU.** No cairo, no pixman.
5. **The network is openweb.fetch on OpenTLS** (openamigaservice). No curl,
   nghttp2 or libpsl.
6. **Every decoder is a datatype.** Pictures go through datatypes.library
   (and so does encoding, such as canvas `toDataURL` to PNG),
   and compressed bodies through gzip.datatype and brotli.datatype.
   Acceleration goes into the datatypes, never into side decoders in the
   browser.
7. **XML is OXP,** our own parser (openamigaxml).
8. **JavaScript has a 68k JIT,** our template JIT in WebKit's patches. It is
   also shared with other programs as javascript.library.

## The documents this file overrides

The design documents disagree with each other (inventory of 22 October
2026). Where they disagree, the decisions above are the ones in force:

- amigachrome `docs/design/Design-OpenBrowser-JIT-on-core.md` 0.5 and
  `Design-OpenBrowser-Lean-Engine.md` 0.2 (17 October) are current: only
  our own libraries and SQLite.
- `Design-OpenBrowser.md` 0.2 and the migration design 0.1 are older. They
  keep curl on OpenTLS, a trimmed ICU 78.3, and link OpenBrowser with
  pixman. None of that holds.
- **Decided 22 October (Dale):** unicode.library owns line and word
  breaking, NFC, IDNA, case mapping and the other Unicode properties.
  OpenFont stays fonts and shaping only, as `Design-OpenFont.md` 0.1 §3
  says. The browser designs that give breaking to OpenFont are overridden.

## Where each decision stands

| Decision | Our part | State of our part | In today's engine? | What still links the old one |
| --- | --- | --- | --- | --- |
| XML is OXP | OXP, openamigaxml #7-#9 (passes the W3C suite) | done | **yes** (patch 0030, #13) | nothing |
| Text is OpenFont | openfont.library 1.4 (openamigafont #15, #16): own reader, rasteriser and shaper (HarfBuzz out of the library, 1,054,068 to 252,368 bytes), UTF-16 shaping with script, language, direction and features, outlines, metrics, tables, web fonts from memory, a face for a character, exact family matching (OFA_Exact), and colour emoji (CBDT, sbix) decoded through the PNG datatype (256,700 bytes) | done: OpenFontLibTest passed on lab copies of Instance-14 and Instance-32 (1.3, 23 Oct); WebKit patch 0090 (`USE_OPENFONT`) is the font backend: FontCache, FontPlatformData, glyph pages, web fonts and the shaping path, glyphs drawn through cairo user fonts | **yes** (patch 0090, #94) | nothing: FreeType, HarfBuzz, fontconfig and Expat are 0 bytes in the engine's map (dev build, 23 Oct); cairo is built without its FreeType backend (openamigacairo `CAIRO_FONTS=user`) |
| Drawing is OpenGfx/OpenGPU | OpenGfx (openamigagfx: graphics.library 47.100 and layers.library 47.102, clean room), opengpu.library 0.16 (openamigartg), pixman.library 2.0 (25 calls over OpenGPU; builds from openamigapixman main, e4bd3ec) | OpenGfx has graphics.library's API: no anti-aliased paths, gradients, alpha compositing or glyph runs, and nothing deeper than 8 planes yet. OpenGPU's command stream has fill, copy, mask, premultiplied compositing, bilinear and affine scaled images and the 13 Porter-Duff operators (not yet on Vulkan), but no anti-aliased paths, gradients, blur or the PDF/HSL blend modes. openamigagfx main builds graphics.library 47.100 and layers.library 47.102 (rebuilt on the dev machine 22 Oct). No GraphicsContext exists on either; GraphicsContextOpenGfx and DrawGlyphRun are in design documents only | **no** | WebCore's cairo GraphicsContext and image surfaces |
| Network is openweb.fetch | openweb.fetch/1 (openamigaservice `src/webfetch.c`, `docs/OPENWEB_FETCH.md`, v1 12 Oct) | runs on the services card only, and its TLS is the host's OpenSSL 3, not OpenTLS. It has cookies (passes every Set-Cookie through), keep-alive pooling, host-side gzip, deflate and Brotli, and 64 fetches at once. It lacks HTTP/2, WebSocket, proxies, HTTP auth and a LAN transport. No `OWFetch` binary on any drive; no WebCore network backend on it | **no**: today curl 8.22 on opentls.library 1.3 (patch 0022), HTTP/2 through nghttp2 (0023), bsdsocket.library from OpenSocket | WebCore's CurlContext and curl loader |
| Decoders are datatypes | picture datatypes (openamigaimage); stream.datatype, gzip.datatype, brotli.datatype | pictures: yes. The stream class and the gzip and Brotli datatypes are a design only (JIT-on-core 0.5 §5.1), with no code in any repo | partly | libpng (for cairo) and zlib are still linked; curl decodes compressed bodies |
| JavaScript JIT | patches 0020-0052 | in the browser | **yes** | nothing |
| javascript.library | #63, #64; JavaScriptEngine built with the browser (#98) and shared with it (jsimage: #99, #103, #106, #108) | done: JavaScriptEngine 25,374,812 bytes stripped (38,086,144 before, 23 Oct); the browser engine takes JavaScriptCore, WTF and bmalloc from it (49,883,356 bytes stripped, 73,263,356 before); DOM bench no slower on lab copies of Instance-14 and Instance-32 | **yes**, when built with `build-webcore.sh jsimage` (`package-browser.sh` packs it with `Libs/JavaScript/JavaScriptEngine`) | nothing in the jsimage engine; the plain build (`OB_JSIMAGE=0`) still links its own |
| ICU | unicode.library (`src/unicodelib`, #90, #102, #107, #113): ICU 78.3's code and data, one shared copy, 261 calls | done: library 4,032,168 bytes, data 4,597,248 bytes; UnicodeTest passed on lab copies of Instance-14 and Instance-32 (23 Oct); the 0.8.1 engine relinked on it is 71,871,112 bytes stripped (75,787,960 before, 3,916,848 less) and draws Wikipedia. The default build on it (#113, 28 Oct): WebBrowserEngine 69,347,684 bytes and JavaScriptEngine 21,561,160 bytes stripped; all six pages pass on a lab copy of Instance-14; launch to painted 3.42 s, the same as without it (3.42 s), and about 0.14 s less when the library is already in memory | **yes**: the default build (`OB_ICU_DATA=library`); `OB_ICU_DATA=file` links ICU in | ICU's own code, inside unicode.library, until step 7's second half replaces it |

What the old libraries cost in today's engine, from its link map (MiB):

| Library | Size | Goes when |
| --- | --- | --- |
| ICU (code 3.4 + data 5.2) | 8.6 | data: step 2; code: into unicode.library, the default build (step 7, 3.9 MB less, #113, 28 Oct); our own code: step 7 |
| HarfBuzz | 1.16 | step 3 (gone, #94) |
| FreeType | 0.50 | step 3 (gone, #94) |
| fontconfig, with Expat | 0.45 | step 3 (gone, #94) |
| cairo, pixman, libpng | 0.94 | step 6 |
| curl, nghttp2, libpsl | 0.54 | step 5 |
| JavaScriptCore | 21.1 | step 4, into javascript.library (done 23 Oct: the jsimage engine is 23.4 MB smaller) |

WebCore's own cairo, FreeType and curl platform code (inside WebCore's
47 MB) goes along with the libraries.

## The work, in order

Each step is one or more PRs here (in `webkit/` patches and `src/`). A step
is done when its "done when" holds on the lab instance.

1. **Write this down.** This file. Done when it is merged and linked from
   the README.
2. **ICU data from the file in both engines.** The browser's build already
   defaults to it (`OB_ICU_DATA=file`, #52); a build folder configured
   before #52 has to be reconfigured. JavaScriptEngine needs the work
   itself: `scripts/build-javascript-engine.sh` reuses the jsc shell's link,
   so it needs #52's loader (`ob_icudata.c`) and stub (`ob_icustub.c`) and a
   load of the data file at start. Done when neither binary links the
   `libicudata.a` object (the stub's empty `icudt78_dat` stays, by design),
   and both still pass their tests. Saves 5.2 MB in each. The browser
   engine is done (the 22 October test build, 89.7 MB).

   **2a. Link and code shape (the rest of L0 and L2).** Decided on 17
   October in the Lean Engine design, sections 5.3, 6.1 and 7, and missed
   until 22 October. None of it changes WebCore's source:
   - fold COMDAT copies at link time: 5.8 MB of duplicate template copies
     (measured);
   - PC-relative calls and addresses (`bsr.l`, `(bd,pc)`) in place of
     absolute ones: about 0.79 million fewer relocations, 3.2 MB less of
     relocation tables (measured count);
   - `ALWAYS_INLINE` as plain `inline` and no `flatten` on the 68k, with
     the hot interpreter and style paths inlined by name: the JSON parser
     (3.06 MB) and the typed-array functions (1.67 MB) stop being giant
     single functions; 4 to 8 MB (estimate);
   - the Web Inspector out of the engine into webinspector.library, loaded
     on demand: 2.6 MB.
   - JSON.parse through json.library (patch 0058), so LiteralParser is no
     longer built: 1.27 MB.

   Done so far: folding (#78), the inspector left out of the build (patch
   0057, #80, 3.26 MB; no webinspector.library exists yet, so loading it
   on demand is still to do) and JSON.parse through json.library (patch
   0058, #81, 1,267,592 bytes;
   LiteralParser was about 1.0 MB in the map, not the 3.06 MB above). The
   engine now needs json.library in `LIBS:`. Patch 0056 (#83) makes
   forced inlining a plain inline outside JavaScriptCore, WTF and WebCore's
   style, layout and DOM code: 1,163,792 bytes, with the DOM bench within
   timer noise. Measured together, main with 0057 and 0058 is 79,535,312
   bytes stripped and folded, and 78,371,520 with 0056. Pc-relative code
   (`OB_PCREL=1`: -mpcrel -fPIC, linked as one hunk; #85) takes it to
   75,787,212 (2,584,308 bytes less): 864,276 fewer relocations (1,494,434
   to 630,158), for 0.70 MB more code, with the DOM bench within timer
   noise (1,527 against 1,567 ms, nine runs each). `OB_PCREL` is on by
   default since 23 October: main d60ecb4 built from a clean tree with
   it (and `OB_GC_SECTIONS=1`) passed the test pages on lab copies of
   Instance-32 and Instance-14, the window showed Wikipedia on both, and
   the DOM bench's JavaScript took 1,509 and 1,462 ms (nine runs each).

   **One engine (0.8.2 test, 23 October).** The package carries only
   `WebBrowserEngine` (75,811,000 bytes stripped), which webbrowser.library
   starts and which has OpenBrowser's window; `OpenBrowser.engine` is no
   longer packaged or built by `build-browser` (`build-window-engine` keeps
   it for tests). Its fonts, fontconfig, blocklist and ICU data go once, in
   `Libs/WebBrowser/` beside it. The drawer goes from 173,019,958 to
   86,801,839 bytes, and the archive from 80,034,607 to about 47.3 MB
   (measured on the 0.8.2 test package, built from main 8ba1e04 with this
   change, before patches 0070-0074). With 0070 the drawer also carries
   `Libs/WebBrowser/Network/public-suffixes`, once, beside the engine.

   Section GC (`OB_GC_SECTIONS`, #45) is off by default; the 22 October
   test build turned it on, and it saves almost nothing on its own; with
   the duplicate template copies folded (`OB_FOLD`, on with it) the
   stripped engine is 5.95 MB smaller (89.7 to 83.7 MB); -O2 JavaScriptCore
   costs about 6 MB for its 1.1-1.65x. Done when each change is measured
   on the engine's map, and the test pages pass on lab copies of real
   instances.
3. **Text on OpenFont.** First, replace the HarfBuzz copy inside
   openfont.library's shaper with our own, and finish what it lacks for
   WebCore: UTF-16 input, shaping with script, language, direction and
   features, fonts opened from memory (web fonts), glyph outlines or a
   path call, and a passing OpenFontLibTest on a lab. Then a `PORT=Amiga`
   font backend (FontCache, FontPlatformData, GlyphPage and the shaping
   path) that calls it. Done when FreeType, HarfBuzz, fontconfig and Expat
   are gone from the engine's map and the benchmark pages render the same.
   **Done 23 October 2026** (openamigafont #15 and #16, openamigacairo #8,
   #94, WebKit patch 0090): the engine built on dev in 1,792 s with
   FreeType, HarfBuzz, harfbuzz-icu, fontconfig and Expat at 0 bytes in
   both maps. Emoji come from openfont.library 1.4 through the PNG datatype,
   so FreeType's libpng use went with it. The benchmark pages on the lab
   copies of Instance-14 and Instance-32 are recorded in
   `labs/webcore-openfont-20261023/` as they finish.
   **Checked on Instance-32 and Instance-14, 28 October 2026** (#100, #111,
   #114 with WebKit patches 0091-0093, and openamigafont #21). The package's
   own openfont.library and fonts are found from the drawer (before this the
   engine aborted with none in `LIBS:`). Fallback fonts no longer read every
   font file again for each character. Results, in seconds:
   - Instance-14 Wikipedia painted in 2.46 s, against 14.96 s before and
     2.14 s on the curl build.
   - Instance-32 from a cold boot, launch to painted: test1 0.64 s (0.8.2:
     0.64 s) and DOM 2.37 s (0.8.2: 2.36 s). The engine is 73,264,340 B,
     against 0.8.2's 75,787,960 B.
   - Emoji: U+1F600 through the PNG datatype on both copies.
4. **The browser on javascript.library.** Before the switch, build the
   library the way the browser is built: MinSizeRel with -O2 for
   JavaScriptCore (#57), and with the 68k JIT patches. Today
   JavaScriptEngine is built Release (-O3), and whether it carries the JIT
   patches is not checked. Done when the browser engine links no
   JavaScriptCore of its own (about 22.6 MB smaller, 90.2 to about 67.6 MB)
   and the benchmarks are no slower.

   **Done 23 October 2026** (#98, #99, #103, #106, #108, #110 and this
   step's last PR). JavaScriptEngine is built the browser's way:
   MinSizeRel, -O2 JavaScriptCore, JIT patches 0020-0058, one pc-relative
   hunk. It is 25,374,812 bytes stripped, down from 38,086,144. The browser
   engine is a jsimage client (`scripts/jsimage.py`, `src/jslib/jsimage.h`):
   it is linked without JavaScriptCore, WTF and bmalloc, and at start-up it
   loads JavaScriptEngine and runs on its copy. It is 49,883,356 bytes
   stripped, down from 73,263,356 for the same build linked plain (23.4 MB
   less), with a 769,152-byte fixup table. The engine looks for the image
   in `LIBS:JavaScript/` first (javascript.library's, when it is this
   build's: the image id is read from the file before loading), then in
   its drawer (`Libs/JavaScript/JavaScriptEngine`). Times on dev's lab
   copies (JSIMG6, quiet machine, 9 runs each, alternating with plain
   0.8.2):

   | | DOM bench JS, jsimage | DOM bench JS, plain 0.8.2 | 0.8.1 |
   | --- | --- | --- | --- |
   | Instance-14 | 1.473 s (median 1.48) | 1.496 s (median 1.48) | 1.46 s |
   | Instance-32 | median 1.58 s (1.52-1.60) | median 1.58 s (1.50-1.60) | 1.51 s |

   The Instance-32 row's 0.8.1 time is an earlier day's: on one fresh
   Instance-32 copy (REL82, 5 runs each) 0.8.1 has a median of 1.56 s and
   0.8.2 1.52 s, so 0.8.2 is no slower than 0.8.1 either.
   Wikipedia and Reddit load on both copies; with `-settle 4` Wikipedia
   loads all 14 requests on both builds (15.50 s with the shared image,
   15.34 s plain, Instance-14).
5. **Network on openweb.fetch.** First, give openweb.fetch what the
   browser needs: TLS through OpenTLS instead of the host's OpenSSL,
   HTTP/2, WebSocket, proxies, HTTP auth, and a transport that works
   without a services card (bsdsocket.library on the Amiga itself, or a LAN
   Cradle). Next, build the datatypes stream class with gzip.datatype and
   brotli.datatype, so compressed bodies are decoded the Amiga way. Then a
   WebCore network backend in place of CurlContext and the curl loader.
   Done when curl, nghttp2 and libpsl are gone from the engine's map.

   The WebCore side goes in the order of `STEP5-6-WEBCORE.md` (#91),
   patches 0070-0079. Done so far: libpsl is out (patch 0070): public
   suffixes come from our own reader of Mozilla's list,
   `PROGDIR:Network/public-suffixes`. Patch 0071 stops building curl's
   unused WebSocket stream. Patches 0072 (the response types without curl)
   and 0073 (the openweb.fetch backend beside curl) are written, not yet
   built. Patch 0075 takes curl out: openweb.fetch does every load (no
   `OB_NET`), and curl and nghttp2 leave the engine's link; written, not
   yet built (the map before and after is still to see).
6. **Drawing on OpenGfx and OpenGPU.** Much of what a GraphicsContext
   needs already exists; reuse it, don't write it again (check of 22 October):
   - anti-aliased path fill and clip: openamigafont `of_raster_a8()` (A8
     mask, curves, non-zero winding; even-odd winding still to add, for
     SVG, CSS and canvas `evenodd`), moved into OpenGPU, with its mask
     composite for clipping;
   - scaled and bilinear images: OpenGPU COMPOSITE BILINEAR and
     COMPOSITE_AFFINE (add premultiplied-with-transform and PAD edges);
   - the 13 Porter-Duff operators: OpenGPU COMPOSITE_PD (add the Vulkan
     back end);
   - glyphs: OpenGPU MASK (add a batched glyph run and a glyph cache);
   - shadows: openamigagfx `oc_shadow()`.

   Done so far: PNG encoding through the PNG datatype's write (DTM_WRITE,
   patch 0074; openpng.datatype writes from 47.4); OpenGPU 0.18's stream v1.6 (openamigartg #109) has
   anti-aliased path fill (non-zero and even-odd), strokes, gradients,
   image patterns, blur and the blend modes, on its CPU core.

   Still to write: anti-aliased strokes, even-odd fill, linear, radial and
   conic gradients, blur, the PDF/HSL blend modes, OpenGfx bitmaps deeper
   than 8 planes, PNG encoding through the PNG datatype (canvas
   `toDataURL` and `toBlob`, today libpng through cairo), then a
   GraphicsContext in place of cairo's. pixman.library 2.0's
   DESIGN.md section 10 is the same gap list; this step tracks it. Before
   the step starts, clean up the duplication, with one home per job
   (decided by Dale, 22 October):
   - **opengpu.library** does all pixel work on chunky and RTG bitmaps:
     fill, blit, composite, scale, blends, glyphs, gradients, blur, and the
     anti-aliased path rasteriser (moved there from openfont), on its CPU,
     host and Vulkan back ends;
   - **OpenGfx** (graphics.library 47.100) keeps the graphics.library API
     and planar bitmaps only, and calls OpenGPU for RTG bitmaps;
   - **openfont** keeps fonts and shaping, and draws through OpenGPU;
   - OpenRTG `screens.c`, the host's `acrtg.c` and the UI gradient fillers
     (opengadtools, openlook, openintuition, gradientslider) call OpenGPU
     instead of their own copies;
   - the old OpenGfx 1.x code in opengpu.library (openamigartg
     `library/ogfx`) and the copied `ogfx_*` files go;
   - no cairo and no pixman (Dale, 22 October): WebCore draws on OpenGPU
     directly, and nothing in the browser uses pixman.library. This is the largest step: all of WebCore's
   painting goes through it. Done when cairo, pixman and libpng are gone
   from the map.
7. **unicode.library, then ICU out.** unicode.library is made (#90,
   #102, 23 October, `src/unicodelib/README.md`): one shared copy of
   ICU's data and its calls for every program; the engine links 6.8 KB of
   stubs with `OB_ICU_DATA=library`. Since #113 (28 October) that is the
   default build, for the engine and JavaScriptEngine alike:
   `build-webcore.sh` makes the library, `package-browser.sh` puts it in
   the drawer's `Libs/`, and Install copies it to `LIBS:`. Before Install
   the engine opens the drawer's copy (`PROGDIR:Libs/`), which needs
   OS 3.2.3 or an open ROM with OpenRamlib from 26 October on (guest
   13b3ec33). From launch to painted the dom page takes 3.42 s with it
   and without it on a lab copy of Instance-14; opening the library and
   its data costs about 0.07 s once, and the smaller engine loads 0.08 s
   sooner. Left: replace
   ICU's code with our own, in unicode.library: line and word breaking,
   NFC, IDNA, Unicode properties, and the Intl services JavaScriptCore
   uses (collation, number, date and plural formatting, locales and time
   zones, tested by `tests/c/icuprobe.cpp` and the compatibility page's
   Intl checks), so that ICU leaves both engines. Done when the only
   third-party code in the engine's map is SQLite.

Not in the order above, still open:

- **#6, the visit cache (patch 0018).** Open since 7 October: finish it or
  close it.
- **The inventory of 22 October 2026** (every repo, PR, design document and
  lab) is the evidence behind the "where each decision stands" table. It is
  kept outside the repositories, in INVENTORY-20261022.

## How to check

Sizes come from the link maps and from stripping copies with the os32-gcc16
stove's `m68k-amigaos-strip`. A map is grouped by archive (`lib*.a`), and an
archive that is still in the map is still linked. The javascript.library
checks run in a lab copy of an OS 3.2.3 instance (amigachrome's
`scripts/lab_instance.py`; in this repo, `tools/run-probe.sh`).
