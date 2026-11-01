# OpenBrowser

A WebKit-based web browser for AmigaOS 3.2. Needs a 68040 with FPU.

**Status, 4 October 2026:** WebKit's engine, WebCore, draws real pages on
AmigaOS 3.2.3. It lays out HTML and CSS, runs the page's JavaScript
(JavaScriptCore) and paints with cairo, FreeType and HarfBuzz into a 32-bit
bitmap; pictures are decoded by the system's datatypes. The browser window,
**OpenBrowser** (GadTools), and its network layer (openweb.fetch, from
openamigaservice; libcurl on OpenTLS until WebKit patch 0075) are
built and being brought up on the test instance. See
[docs/WEBCORE_PORT.md](docs/WEBCORE_PORT.md).

JavaScriptCore on its own runs in a Shell:

```
1> Stack 4194304
1> jsc -e "print(1+1)"
2
```

**OpenBrowser Lite 0.4.1**, the small browser, is a GadTools window that
opens http:// and https:// pages (bsdsocket.library and OpenTLS) and lays
them out: headings, paragraphs, lists, links you click, pictures through
the system's datatypes and a practical CSS cascade (the page's `<style>`,
`style=` and up to eight linked stylesheets). No JavaScript, no WebKit: it is
about 100 KB. Its page engine, weblayout (`src/weblayout`, on OpenLayout), is
kept behind one header as the bones of a future weblayout.library
([docs/WEBLAYOUT_LIBRARY.md](docs/WEBLAYOUT_LIBRARY.md)). Other programs open
pages in it through its ARexx port, `AMIGACHROME.BROWSER`:

```
1> rx "address AMIGACHROME.BROWSER 'OPENURL http://info.cern.ch/'"
```

See [docs/PLAN.md](docs/PLAN.md) for the plan: the decisions, where each
stands, and the work in order. [docs/PHASE_PLAN.md](docs/PHASE_PLAN.md) has the history.

## What is here

| Folder | What |
| --- | --- |
| `src/` | OpenBrowser Lite: the GadTools window (`ob_main.c`) and HTTP fetching (`ob_http.c`) |
| `src/weblayout/` | weblayout, OpenBrowser Lite's page engine: HTML Lite, CSS Lite, painting, datatype pictures (`weblayout.h` is its whole interface) |
| `src/jsonlib/` | json.library (OpenJSON): JSON parsed into one flat, relocatable block, a writer and a streaming reader, for native programs; here for now, its own repository later ([README](src/jsonlib/README.md)) |
| `src/jslib/` | javascript.library: JavaScript (JavaScriptCore with the 68k JIT) for any program, which hands scripts its own objects as host classes; the JavaScriptEngine program behind it and the `JS` Shell command; here for now, its own repository later ([README](src/jslib/README.md)) |
| `src/webcore/` | OpenBrowser on WebCore: the window (`ob_browser.c`), the page layer (WebCore's clients, `ob_webview.h`), cookies and the network loader, and the test programs `obcore-dump` and `obcore-view` |
| `third_party/openmail/` | Network, HTML-to-text and stack code shared with OpenMail (MIT, AmigaChrome Team) |
| `webkit/` | The pinned upstream revision and the AmigaOS/m68k patches for WebKit (JavaScriptCore and WebCore) |
| `toolchain/` | CMake toolchain file for the os32-gcc16 compiler |
| `scripts/` | `build-openbrowser.sh`, `build-icu.sh`, `build-jsc.sh` and `build-webcore.sh` |
| `icu/` | ICU 78.3 patch for AmigaOS 3.x |
| `stove/` | Notes and patches for the GCC 16.2 AmigaOS compiler, plus the compat library and headers it needs |
| `tools/` | `obfetch` (fetch a page from a Shell, for tests), probe runner for an AmigaChrome test instance, and stack/map helpers |
| `tests/` | JavaScript and C test programs |
| `docs/` | Porting notes and the phase plan |

No binaries are kept here, and no WebKit, ICU or AmigaOS files.

## Building

OpenBrowser Lite is built with the OpenAmigaGCC 16.2.0-open3 release and
OpenNDK (DalsinAI/openamigasdk), for the 68040 with its FPU (`-m68040
-m68881`; not an LC or EC040), with OpenLayout from DalsinAI/openamigalayout.
Its TLS is OpenTLS: opentls.library's API headers are in `third_party/opentls`,
and no AmiSSL SDK is needed:

```
TOOLCHAIN_DIR=.../openamigagcc-16.2.0-open3-x86_64-linux \
NDK_INCLUDE=.../openamigasdk/OpenNDK/Include_H \
OPENLAYOUT_SRC=.../openamigalayout scripts/build-openbrowser.sh
OPENLAYOUT_SRC=.../openamigalayout tests/run-host-tests.sh
```

(`STOVE=` an os32-gcc16 stove works in place of the first two; `TESTS=1`
adds the Amiga test programs; `OPENLAYOUT=library` uses the resident
openlayout.library 2 instead of building OpenLayout in.) The cloud CI,
`.github/workflows/lite.yml`, does both on every change.

It needs AmigaOS 3.2, a 68040 with its FPU, a TCP/IP stack
(bsdsocket.library) and, for https://, OpenTLS (opentls.library with
opencrypto.library, and the CA list in `S:OpenTLS/ca-bundle.pem`: OpenUp's
OpenTLS part). Pictures need datatypes for their formats (OpenUp's
OpenImage adds PNG and WebP).

The JavaScript engine:

1. Build the os32-gcc16 compiler (see `stove/STOVE-NOTES.txt` and
   `stove/gcc-patches/`), then build `stove/compat` into
   `libos32compat.a`. Copy `stove/include/*.h` into the compiler's
   `m68k-amigaos/sys-include`.
2. Build ICU 78.3 for AmigaOS with `scripts/build-icu.sh`.
3. Check out WebKit at the pinned revision and apply the patch (see
   `webkit/README.md`).
4. Run `WEBKIT_SOURCE=... scripts/build-jsc.sh`.

OpenBrowser on WebCore needs, besides the compiler and ICU above, the
libraries from the OpenAmiga ports (cairo and pixman, libxml2, SQLite, zlib
and libpng; see
https://github.com/DalsinAI/openamiga) built into one prefix. No curl,
nghttp2, libpsl or AmiSSL: the network is openweb.fetch, so
DalsinAI/openamigaservice and DalsinAI/opendatatypes must be checked out
beside this repository (or named by `OPENSERVICE_DIR` and
`OPENDATATYPES_DIR`). cairo is built without FreeType (openamigacairo,
`CAIRO_FONTS=user`) and the prefix holds openfont.library's SDK
(openamigafont): the engine's text is drawn and shaped with openfont.library
1.4, which the Amiga needs in `LIBS:`. Then apply the patches in `webkit/`
and run `WEBKIT_SOURCE=... OAB_DEPS=... scripts/build-webcore.sh`.
Without a services card or a Cradle, the Amiga needs a TCP/IP stack, and
for https:// opentls.library 1.2 or newer and its CA bundle in
`S:OpenTLS/ca-bundle.pem`; stream.datatype, gzip.datatype and
brotli.datatype undo compressed pages.
The programs are large (about 76 MB each, stripped) and need a 68040 with
FPU (hard float, `-m68881`) and plenty of Fast RAM. OpenBrowser's package
(`scripts/package-browser.sh`) carries one of them: `WebBrowserEngine`,
which webbrowser.library starts and which has OpenBrowser's window; the
`OpenBrowser` icon starts a small launcher that asks the library for it.

The build has two halves. The engine (WebKit's WebCore, JavaScriptCore,
WTF, PAL and bmalloc) is built by `build-webcore.sh build-engine`, and only
when its fingerprint changes: the WebKit tree, the compiler, the libraries'
headers or the build options. OpenBrowser's own code is built by
`build-webcore.sh build-browser` (`WebBrowserEngine` and the launcher;
`build-tests` for `obcore-view` and `obcore-dump`; `build-window-engine`
for `OpenBrowser.engine`, the same window as a program of its own, which
the package no longer carries), which compiles only OpenBrowser's files and relinks: about
15 seconds for a one-file change. The compiler runs through ccache when it is
installed, and a new build folder uses Ninja when it is installed. Every
build adds a line to `build-report.txt` in the build folder. The commands
and settings are listed at the top of `scripts/build-webcore.sh`.

## Licence

OpenBrowser's own code and documents are MIT, Copyright (c) 2026 AmigaChrome
Team (see [LICENSE](LICENSE)). WebKit/JavaScriptCore, ICU and GCC parts keep
their own licences (see [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)).

If you fork this or base work on it, please keep the credit and say what your
work is based on.

## Contributors

OpenBrowser is created and maintained by [SacredTrees](https://github.com/SacredTrees) with The AmigaChrome Team, copyright AmigaChrome Team. Everyone whose work it includes is credited in [`CONTRIBUTORS.md`](CONTRIBUTORS.md).
