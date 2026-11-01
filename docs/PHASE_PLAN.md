# GFOpenBrowser: phase plan

Copyright (c) 2026 AmiCradle Team. MIT licence for this document and our own code; WebKit and JavaScriptCore parts keep their LGPL/BSD notices.

Written 4 Oct 2026 after our direction, relayed word for word by the main session: "the webkit browser project needs to move to a new phase, a gadtools ui, datatype support, and get it a useable browser."

## Where it stands (4 Oct 2026)

- JavaScriptCore (JSCOnly, C-loop interpreter) builds for **AmigaOS 3.2** with the `os32-gcc16` stove (bebbo amiga-gcc, GCC 16.2, libnix, libpthread) as a 71 MB hunk executable.
- On an AmiCradle test instance (OS 3.2.3, AC090 68040), `print(1+1)` prints `2` and exits with rc 0. Strings, arrays, JSON, keywords and try/catch work.
- Functions, recursion, loops, JSON, RegExp, Date, try/catch and double-precision maths pass a first test set (tests/js).
- The big-endian and 2-byte-alignment fixes found here (JSValue/Register layout, LLInt frame slots, TypeInfo blob, RapidHash, keyword compares, LazyProperty tags) apply to the AROS m68k port too.
- AC090 emulator bug: the JIT's x87 FPU path loops in ICU code. Probes run with `JIT_NOX87=1`.

## Rules carried in

- OS 3.x UI is GadTools (MUI acceptable). No ReAction.
- Images and documents decode through **datatypes**, not bundled decoders, wherever the system can do it.
- libnix ignores `__stack`: use the StackSwap helper pattern (body takes no arguments), as in `oap_stack.c` / `oam_stack.c`.
- Never call GT_SetGadgetAttrs on gadgets that are not attached yet; rebuild with "fill state, build, AddGList".
- Keep the paint path behind one interface, so GFOpen RTG's GFOpenGPU (openrtg DESIGN.md section 5) can take page drawing onto the host later.
- Binaries, ICU data and test evidence stay out of the public repo; WebKit is a patch series against pinned upstream `54fe1539718e`.

## Reuse from the other Open… projects (MIT, AmiCradle Team, notices kept)

| Need | Source |
| --- | --- |
| GadTools layout, font-sensitive groups, listviews | OpenAmigaPrint `include/oap_gt.h`, `src/ui/oap_gt.c` |
| Datatypes in a GadTools window, scrollers, AppWindow drops | OpenAmigaView `src/viewer/oav_main.c` (in openamigaprint) |
| bsdsocket + TLS (GFOpenTLS; AmiSSL 5 until 19 October 2026), signal waits, Ctrl-C | OpenAmigaMail `platform/` and engine net/TLS |
| HTML as text with numbered links (fallback) | OpenAmigaMail `oam_html` |
| Opening links from mail | browser hook, shape agreed with the OpenAmigaMail session |

## Phases

### Phase 0: JSC solid (finish now)
1. Fix JS function calls (LLInt call path on big-endian) and double precision (FPCR set per thread).
2. Run a small conformance set (JS basics, then a test262 subset). Record pass rates.
3. Remove the temporary tracing; land the AmigaOS 3 platform layer as a clean patch series; package `jsc` as a standalone hunk executable with its hash.
4. Report the AC090 x87 JIT bug with a repro; carry the shared big-endian fixes to the AROS lane.

**Exit:** jsc runs the conformance subset on 3.2.3 and exits cleanly.

### Phase 1: first page on screen, without WebCore ("OAB Lite")
A small native browser that is useful early while WebCore is brought up.
1. GadTools shell: URL string gadget, Back/Forward/Reload/Stop, status line, a page area with scrollers. Built on `oap_gt`.
2. Networking: HTTP/1.1 and HTTPS through OpenAmigaMail's net/TLS layer (GFOpenTLS; AmiSSL 5 until 19 October 2026), redirects, gzip later.
3. Page view: HTML to styled text with numbered links (`oam_html` grown into a simple flow renderer: headings, paragraphs, lists, links, bold/italic), drawn with graphics.library text.
4. Images inline through datatypes (PNG/JPEG/GIF/IFF), scaled to the page width.
5. Browser hook for OpenAmigaMail and an ARexx port later.

**Milestone 1a:** fetch `http://` page and show it as text in a GadTools window.
**Milestone 1b:** HTTPS plus clickable links and history.
**Milestone 1c:** inline images through datatypes.

Status, 4 October 2026: 1a is done and most of 1b with it. GFOpenBrowser Lite
(`src/`) opens http:// and https:// pages on AmigaOS 3.2.3 and shows them as
wrapped text in a GadTools listview; clicking a line follows its first link,
Links... lists them all, Back and Reload work, and the ARexx port
`AMICRADLE.BROWSER` takes OPENURL. Still to do for 1b: Forward, Stop, a
window title from the page's `<title>`, and fetching without blocking the
window. Known problem: under AmiCradle's ACNet bsdsocket.library 4.1,
WaitSelect returns at once after the first connection, so second and later
pages fail with "The server stopped answering"; reported to the ACNet side.

### Phase 2: WebCore bring-up (headless first)
1. Build WebCore for AmigaOS 3.2 (new `PLATFORM(AMIGA)` port, modelled on the WPE/JSCOnly split): HTML parser, DOM, CSS, layout, JS bindings. No GPU, no media, no WebGL/WebGPU/WebAssembly, no multi-process.
2. Graphics backend: a software rasteriser into an RGB buffer (Cairo with its image surface, or WebKit's own Skia-free path if small enough), blitted with `WritePixelArray`/Picasso96. One paint interface, so GFOpenGPU can replace it.
3. Fonts: FreeType + HarfBuzz over a small bundled font set; Amiga bitmap fonts only as a fallback.
4. Images: a WebCore `ImageDecoder` built on datatypes.
5. Loader: a single-process resource loader on the Phase 1 network layer (no libsoup/curl).
6. Memory budget measured early (binary size and heap per page) against 256 MB Z3 RAM.

**Milestone 2a:** WebCore loads a local HTML file and dumps the render tree to the console.
**Milestone 2b:** WebCore paints a page into an offscreen bitmap saved as an ILBM/PNG.

Status, 4 October 2026: 2a and 2b are done on AmigaOS 3.2.3 (Instance-24):
`obcore-dump` prints a page's render tree and saves the page as PNG, and
`obcore-view` runs the browser's page layer (JavaScript, the DOM, timers)
without a window. Choices made on the way, with the reasons in
`WEBCORE_PORT.md`: cairo with pixman (Skia refuses big-endian CPUs), the
Liberation and DejaVu fonts through fontconfig, pictures through the
system's datatypes, and libcurl for the loader (on GFOpenTLS since 19 October
2026, AmiSSL before), since WebCore already has a curl backend with cookies,
redirects and TLS.

### Phase 3: WebCore in the GadTools shell
1. Swap the Phase 1 text view for the WebCore view inside the same shell: scrolling, link clicks, keyboard focus, text selection, forms (text fields, buttons, checkboxes, selects drawn natively).
2. Cookies, history, bookmarks, downloads to disk, a disk cache.
3. Stop/reload, progress, error pages, Ctrl-C/Break handling.

**Milestone 3:** Browse a few simple real sites (text-heavy, light JS) end to end.

### JavaScript speed and the heavy work (design 0.5, 17 Oct 2026)

AmiCradle's `docs/design/Design-OpenBrowser-JIT-on-core.md` 0.5 (PRs #564 and #565)
replaces 0.1's engine on the host, which the user turned down. GFOpenBrowser
stays an Amiga program, WebKit's `PORT=Amiga`, on the Team's own libraries
and SQLite. Extra power comes from discrete services on autoconfig boards
(the services card, GFOpenMulticore's cores) or a paired LAN Cradle.

- **JavaScript, J1 to J4 (decided):**
  - **J1:** this plan's Step 1 finished: the interpreter's hot registers in
    68k registers.
  - **J2:** a 68k Baseline JIT, the RegExp JIT and inline caches, in the
    program.
  - **J3:** `js.compile/1`, an optimising tier as a service. Bytecode and
    profiles go in; unboxed 68k code comes out.
  - **J4:** Web Workers on extra 68k cores through Exec across cores.
  - On the AC090, plain 68k code runs at about a quarter to a fifth of host
    speed, so unboxed 68k code gains up to 50 to 130 times over the
    interpreter (measured).
- **Every decoder is a datatype:** pictures, compression, and anything else
  that turns one format into another. This browser decodes through
  `datatypes.library`, never with side decoders of its own. Acceleration
  lives inside the datatypes system, which may hand a decode to a core or a
  LAN Cradle.
- **Pictures:** our datatypes, PNG and JPEG first, then WebP.
- **Compressed bodies** (`Content-Encoding`) go through a new stream class in
  datatypes.library: our own `brotli.datatype` (clean room, from RFC 7932)
  and `gzip.datatype` on openzlib. `openweb.fetch`'s Amiga side decodes
  through them too.
- **No pixman in GFOpenBrowser:** drawing and image code target GFOpenGfx and
  GFOpenGPU directly.
- **Text** uses GFOpenFont, which also takes over the functions WebKit took
  from ICU.
- **The network** is `openweb.fetch` on GFOpenTLS.

### Phase 4: usable
1. Multiple windows (tabs later), find-in-page, zoom/text size, printing through OpenAmigaPrint.
2. Performance: profile on 3.2.3, cut startup time (71 MB binary loads slowly), consider a JIT-less fast path for common JS, cache compiled bytecode.
3. Packaging: Installer script, `.info` icons, an Aminet-style archive, an AmiCradle recipe.
4. GFOpenGPU paint path on AmiCradle when GFOpen RTG is ready.

**Milestone 4:** Daily-usable for text and light sites on a 68040 + RTG system with 256 MB.

## Risks
- **Size and speed.** WebCore is large; the C-loop interpreter is slow on 68k. Phase 1 keeps something useful in hand while that is measured.
- **libnix gaps.** These are filled in the stove's compat library: aligned_alloc, long double maths, fenv, uchar, statvfs.
- **Emulator bugs.** Keep JIT-vs-interpreter checks in the probe tooling.
- **Big-endian assumptions in WebCore.** Expect more of what was found in JSC: unions, packed blobs and character compares.
