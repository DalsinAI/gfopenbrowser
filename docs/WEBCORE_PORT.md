# WebCore on AmigaOS 3.2: port notes

Copyright (c) 2026 AmigaChrome Team. MIT licence for this document.

Phase 2 of the [phase plan](PHASE_PLAN.md): WebCore, the engine inside
WebKit, built for AmigaOS 3.2 on 68k and run headless. These notes record
the decisions and the Amiga-specific findings, so later work does not have to
rediscover them.

## The port

WebKit's own ports split work over several processes (WebKit2), and
WebKitLegacy, the single-process API, is Mac-only now. OpenBrowser is a new
**`PORT=Amiga`** build of WebCore in one process, with its own small embedding
layer, the way MorphOS's and Haiku's browsers did it:

| Part | Choice | Why |
| --- | --- | --- |
| Drawing | cairo image surfaces with pixman | Proven on big-endian CPUs; Skia refuses to build for them |
| Text | FreeType, HarfBuzz and fontconfig | WebCore's Cairo backend uses all three |
| Fonts | Liberation and DejaVu, in `PROGDIR:Fonts` | Free licences; Liberation has the metrics of Arial, Times and Courier |
| Network | openweb.fetch (openamigaservice): a services card, a Cradle, or the Amiga's own TCP/IP with OpenTLS and HTTP/2 | `ResourceHandleAmiga.cpp` handles cookies and redirects; libcurl on OpenTLS did the loads until WebKit patch 0075 (AmiSSL 5 until 19 October 2026) |
| Images | the system's datatypes | Our rule: the formats are the ones the user has datatypes for |
| JavaScript | JavaScriptCore's C-loop interpreter | No JIT on 68k |

The port lives in the WebKit tree as a patch series (see `webkit/`). The
library ports are in their own repositories, listed in
[OpenAmiga](https://github.com/DalsinAI/openamiga).

## Findings

**FPU precision.** AmigaOS 3.2's ROM `mathieeesingbas.library` puts the FPU
in single precision (FPCR $40) in every task that opens it, and libraries such
as datatypes may open it while they work. cairo's fixed-point conversion then
fails silently: nothing is drawn. FPCR must be 0 after libraries are opened,
after datatype calls and in every thread. WTF does this at start and in each
thread; the datatypes decoder does it after each call.

**NDK macros.** The NDK's `exec/types.h` defines `IMPORT`, `STATIC`,
`GLOBAL` and `REGISTER` as macros, which break WebCore and JavaScriptCore
names (the parser's `IMPORT` token, for one). curl's headers (and AmiSSL's,
before 19 October 2026) include it, so WebCore's prefix header includes it first and removes those
macros; `VOID` and `CONST` stay because later NDK headers use them.

**libpthread's macros.** Szilard Biro's `pthread.h` turns `read()`,
`close()`, `open()` and others into macros when `unistd.h` came first. That
breaks member functions such as `Document::close()`. Including `pthread.h`
before `unistd.h` keeps them off.

**`size_t` is `unsigned long`.** On AmigaOS it is a type of its own beside
`uint32_t` (`unsigned int`), so WTF's persistent coders gained an
`unsigned long` case.

**Sizes and alignment.** 68k aligns `int` and pointers to 2 bytes, so some of
WebCore's "this struct must stay this size" checks see a smaller struct. They
accept smaller-or-equal on 68k.

**TLS without OpenSSL.** Since 19 October 2026 (WebKit patch 0022) curl does
TLS through OpenTLS (openamigacurl's OpenTLS backend opens opentls.library
itself) and WebCore links no OpenSSL: `USE(OPENSSL)` is off, curl's
OpenSSL-only files are left out, and no cipher list, curve list or CA file is
set (OpenTLS picks its own suites and reads `S:OpenTLS/ca-bundle.pem`).
Responses carry no certificate details, and WebCrypto offers the SHA digests
only. Before that the engine called AmiSSL from C++, built with
`PROTO_AMISSL_H` so calls went through AmiSSL's stub library, and each task
that used it had to open it.

**HTTP/2.** Since 20 October 2026 (WebKit patch 0023) the engine asks curl
for HTTP/2 to https:// servers (`CURL_HTTP_VERSION_2TLS`, falling back to
HTTP/1.1 when the server does not offer h2 in ALPN) and multiplexes requests
to one server as streams on one connection (`CURLPIPE_MULTIPLEX`), so a page
costs one TLS handshake per server. curl needs nghttp2 for it: openamigacurl
pins nghttp2 (1.70.0) and builds it as `libnghttp2.a` beside `libcurl.a`;
copy both into the deps prefix (`OAB_DEPS/m68k-amigaos/lib`). The programs
link nghttp2 after curl when `libnghttp2.a` is there, so a curl built before
openamigacurl had HTTP/2 still links; `build-webcore.sh` stops with a message
when `libcurl.a` uses nghttp2 and `libnghttp2.a` is missing. The loader hands
curl six requests per host at once (four before): over HTTP/2 they are
streams, over HTTP/1.1 the ones beyond curl's two connections per host wait
in curl.

**HTTP disk cache.** WebKit's disk cache (NetworkCache) lives in its network
process, which this single-process port does not have, so until 20 October
2026 a second visit fetched every file again. The engine now keeps one of its
own (`src/webcore/ob_httpcache.c`, joined to WebCore by
`AmigaHttpCache.cpp` and WebKit patch 0026) in `PROGDIR:Cache`: GET
responses with status 200, fresh for as long as `Cache-Control: max-age` or
`Expires` (counted from the server's `Date`) says, then checked with
`If-None-Match`/`If-Modified-Since`, where a 304 reuses the stored copy.
`no-store`, a `Vary` other than `Accept-Encoding`, `Authorization`, ranges
and responses with neither a lifetime nor a validator are not stored; there
is no heuristic freshness. Each entry is written to a temporary file and
renamed when complete. The folder holds at most 64 MB (one entry up to an
eighth of that), and the entries used longest ago go first; an index written
at quit saves reading the folder at the next start. ENV variables:
`OB_HTTP_CACHE=0` turns it off, `OB_HTTP_CACHE_DIR` moves it and
`OB_HTTP_CACHE_MB` sets its size. 64 MB is a starting point to be measured,
not a tuned value.

**Sockets are not file descriptors.** libcurl's configure finds libnix's
`fcntl()` and `pipe()`; on bsdsocket sockets they fail. curl is built to use
`IoctlSocket(FIONBIO)` and a loopback socket pair.

**Datatypes read files.** OS 3.2's picture datatypes reject memory sources
(`DTST_MEMORY` gives `ERROR_OBJECT_WRONG_TYPE`), so the decoder passes each
picture through a file in `T:`. PNG, GIF and BMP decode exactly as on Linux,
including alpha and GIF transparency.

**AmigaDOS paths.** fontconfig and SQLite treat names without a leading `/`
as relative and rebuild them as POSIX paths. Both are patched to keep names
such as `PROGDIR:Fonts` and `T:a.db` as given.

**Shared memory.** AmigaOS has one address space, so WebCore's
`SharedMemory` is plain memory and never makes cross-process handles.

**Build parallelism.** CMake's Makefile generator can run one custom
command for two targets at once, which broke the CSS property generator. The
derived sources are generated first with one job (`make -j1
WebCoreBindings`), then the rest builds in parallel.

**Adding a WebCore source rebuilds all of WebCore.** CMake's Makefile
generator lists every object's precompiled-header options in the target's
`flags.make`, and every object (and the precompiled header) depends on that
file. A new file in `PlatformAmiga.cmake` therefore recompiles all of WebCore,
about an hour on the build PC. New Amiga code goes into a file that is
already listed (by `#include`), or waits for a change that needs a full
rebuild anyway. Ninja has no such file: a build folder made with it
(`GENERATOR=Ninja`, the default when ninja is installed) recompiles only
what changed.

**ccache and the precompiled header (5 October 2026).** WebKit turns ccache
on by itself when it is installed, but on Linux without the `pch_defines`
and `time_macros` settings, and every WebCore file uses the precompiled
header: ccache gave up on all of them ("could not use precompiled header")
and compiled each one again. `build-webcore.sh` now runs the compiler
through a launcher in the build folder (`ob-ccache`) with those settings
and its own 20 GB cache, so an unchanged file comes from the cache (6.5 s
of compiling becomes 0.02 s), and a rebuild that touches files without
changing them costs little.

**Engine and browser builds.** OpenBrowser's own code (src/webcore) builds
as the library OBCore and the programs, separately from WebKit's
libraries: `build-webcore.sh build-browser` compiles only OpenBrowser's
files and relinks, and `build-engine` does nothing unless the engine's
fingerprint (the WebKit tree, compiler, libraries' headers and options) has
changed. Measured on the build PC with the Makefile generator, 5 October
2026: a build with nothing to do takes under a second; one changed
OpenBrowser file 13 to 14 s (one compile, then OBCore and OpenBrowser
linked); one changed WebCore file 13 s for the engine and 13 s to relink
OpenBrowser. Linking the 116 MB program is most of that time.

With Ninja (5 October 2026, same machine): nothing to do, 3 s for
`build-webcore.sh all`; one changed OpenBrowser file 39 s (17 s when ccache
has it); one changed WebCore file 10 s, then 15 s to relink; a new file in
`PlatformAmiga.cmake` 10 s, compiling only that file; and with every object
removed (`clean-engine --yes`), all of WebKit and the programs again in
154 s, 1023 of WebKit's 1028 files coming from ccache. A full build with an
empty cache is still most of an hour on one PC.

**A second PC.** `OB_DISTCC_HOSTS` sends compiles to other PCs with distcc
over SSH (the same compiler at the same path there). ccache adds
`-fpch-preprocess` for files that use the precompiled header, which makes
distcc's preprocessed copy name the `.gch` file that the other PC does not
have, so every remote compile failed and fell back to this PC; the build
folder's `ob-distcc` drops that option. Compiles here keep the precompiled
header; the other PC compiles the headers in full. Jobs share one SSH
connection per PC, since a burst of new logins trips the other PC's
`MaxStartups` limit and distcc then leaves it alone for a minute.

**Duplicate template copies (`OB_FOLD`, 22 October 2026).** With
`OB_GC_SECTIONS=1` every template instance and inline function is a hunk of
its own with one weak definition, and GCC writes the calls from its own
object as hunk relocations against that hunk. Our ld (binutils 2.46, amiga
backend) keeps one copy per object: `bfd/amigaos.c` makes a hunk link-once
only when its `HUNK_EXT` holds weak definitions alone, and a hunk that calls
anything else also holds references; and `amiga_collect()` in its section
GC keeps every hunk that defines a referenced name, not only the copy the
name resolved to. The full engine linked 7,221 `.text` names more than once,
37,208 extra copies, about 6.2 MB. `scripts/hunkfold.py` folds them without
touching ld: from the plain link's map it takes, for each name, the copy ld
used (the winner); in copies of the archives it turns every relocation to
such a hunk into a reference to its symbol and makes each losing copy's
definition local; linked again, `--gc-sections` drops the losers. The same
archive members are linked, and the same copies run, as before.
`build-webcore.sh` does this after `build-browser` and `build-tests` link
(`OB_FOLD`, on by default when the folder links with `--gc-sections`), for
each program with the winners of its own map and its own folded archives
in `BUILD/lib-folded/`, so ninja's archives and build state stay as they
are; `build-webcore.sh fold` folds the programs ninja has linked since,
`WebBrowserEngine` among them. Stripped sizes, `OB_OPTIMIZE=js`:
OpenBrowser.engine 89,680,744 to 83,729,556 bytes (5.95 MB less),
obcore-view 89,672,156 to 83,720,968, obcore-dump 89,448,836 to 83,525,236,
WebBrowserEngine 89,683,216 to 83,732,028; about a minute per program on
the build PC. The real fix belongs in ld (`amiga_collect()` keeping only the
section a name resolved to); then no map would be needed.

**Pc-relative code (`OB_PCREL`, 23 October 2026).** `OB_PCREL=1` builds a
new folder with `-mpcrel -fPIC` and links with
`toolchain/amigaos3-onehunk.ld`. -mpcrel alone makes calls `bsr.l` but
reaches data as `(d16,pc)`, which cannot span a 70 MB program; with -fPIC
on the 68040 every reference is `(bd,pc)` with a 32-bit displacement. Our
ld resolves pc-relative references only inside one hunk: to another hunk it
prints "reloc ... is out of range" and still writes the program, with a
wrong displacement and exit status 0. So the one-hunk script moves `.data`
and `.bss` into the code hunk (the 170 KB of `.bss` becomes zeros in the
file), and every pc-relative reference is resolved at link time, leaving
no relocation. Absolute references stay: vtables and other pointer tables,
and the libraries built outside WebKit (ICU, cairo, curl, libnix, libstdc++).
`bsr.l` is as long as `jsr` to an absolute address; a data or constant
address is 2 bytes longer as `(bd,pc)`, and an address that went into a
register as an immediate now takes a `lea`. GCC 16.2.0b stops with an
internal compiler error (`change_address_1`, emit-rtl.cc:2315) on
JavaScriptCore's `bytecode/CodeBlock.cpp` at -O2 with -mpcrel; its unified
bundle is built with `-mno-pcrel -fno-PIC` (`scripts/cmake/ob-pcrel.cmake`),
the only file of the engine that is. Stripped and folded,
OpenBrowser.engine goes from 78,371,520 to 75,787,212 bytes (82,757,480 to
79,893,600 before folding): relocations 1,494,434 to 630,158, code
72,220,512 to 72,922,500 bytes, and three hunks to one. `OB_PCREL` is on by
default since 23 October 2026, when a clean build of main with it passed
the test pages and the window on lab copies of Instance-32 and Instance-14;
`OB_PCREL=0` gives the absolute, three-hunk build. It has been tested with
`OB_GC_SECTIONS=1` only.

**AmigaDOS names in fontconfig.** fontconfig joins its configuration
directory and file name with a slash, so an empty directory turned
`PROGDIR:fontconfig/fonts.conf` into `/PROGDIR:...`, and AmigaDOS asked for
a disk called `/PROGDIR`. The fontconfig patch keeps names with a volume as
they are.

**No volume requesters.** A program that asks AmigaDOS for a name that is
not mounted gets a "Please insert volume" requester, and waits until someone
answers it. OpenBrowser's programs set `pr_WindowPtr` to -1 at start, so
such a name just fails.

**TLS handshakes take seconds.** On the test instance's emulated 68k a TLS
handshake is 4 to 10 seconds of public-key arithmetic: an X25519 key exchange
takes about 1.2 s, P-256 about 2 s and P-384 about 7 s, and checking an ECDSA
certificate chain such as Cloudflare's about 7 s more. Servers hang up on a
client that takes too long (Cloudflare accepted 10 seconds and refused 20),
and curl then reports "Failed sending data to the peer" when it sends the
request. So the network thread runs one priority above the browser's, the
key exchange offers X25519 and P-256 only (login.live.com picks P-384 when it
is offered), curl keeps two connections per host (one, before patch 0007),
reused for the next requests, and four in all, so few handshakes run side by
side (with HTTP/2, one connection per server is enough), and the connect timeout,
which includes the handshake, is two minutes.

**UTF-16 in the CPU's byte order.** WebKit keeps 16-bit strings in the
CPU's byte order, but three places assumed little-endian: converting them to
UTF-8 (simdutf's `utf16le` functions in WTF), the HTML fast-path parser's SIMD
scan of 16-bit text, and JavaScriptCore's hex decoder. On a 68k the first
garbled every page holding a character beyond Latin-1, and the second turned
markup given to `innerHTML` or `insertAdjacentHTML` with such text into
literal text. WTF now uses simdutf's native-endian functions, and the two
scans take each character's low byte from the right half. The proper place
for that is `SIMD::findInterleaved()` itself, which is in WTF's prefix header,
so it waits for a change that rebuilds everything anyway.

**WebKit's network thread never ends.** Upstream keeps curl's thread for
the life of the program. An AmigaOS program does not end while one of its
threads runs, so the browser stops it (`stopAmigaNetwork()`) before it exits.

**`getenv()` sees Shell variables.** libnix's `getenv()` reads the
variables of the Shell that started the program (`Set`), not global ones
(`SetEnv`). WebKit's `WEBKIT_CURL_*` settings are given with `Set`.

## Speed on a 68k

Measured on the test instance (AmigaChrome's AC090, a 68040 with FPU, 256 MB),
4 October 2026:

| Step | Time | What it is |
| --- | --- | --- |
| Loading the program | about 40 s | 122 MB: 82 MB of code, 33 MB of ICU data, 1.6 million relocations, read through the emulated disk |
| WebCore starting | 2 to 3 s | after `main()` |
| The first text on a page | about 25 s | fontconfig scanning every font again, as its cache was never saved |
| A page with a script and no text | 4 to 9 s | local file |
| Each new https site | 4 to 10 s | the TLS handshake (see above) |

What is done about it:

- **The font cache works.** fontconfig's cache was never written under
  libnix (openamigafontconfig's cache patch), so every start scanned all the
  fonts. It now lives in `PROGDIR:fontconfig/cache`, which lasts across
  reboots, with `T:fontconfig` when the program's drawer cannot be written.
- **Less ICU data.** `scripts/build-icu.sh` keeps the items listed in
  `icu/data-keep.lst`: the root and English locales, every break rule and
  dictionary, normalisation, properties and converters. The data goes from
  33 MB to under 13 MB, and the program from 122 MB to 101 MB.
- **Fewer rendering updates while a page loads.** WebCore asks for a
  rendering update (style and layout of the whole page) after each piece of a
  page arrives. Until the page has loaded, OpenBrowser lets one through at
  most every second, or every twice as long as the last one took.
- **The window paints what is laid out, no faster than WebCore renders**
  (20 October 2026; the gain is still to be measured). Painting no longer
  lays the page out first: the window paints right after each rendering
  update, and between them only areas whose layout is not due, while a page
  loads at most once a second. Changed areas are kept as up to eight
  rectangles (beyond that, one around them all), each painted and copied by
  itself. Of the mouse moves waiting, only the last is handled; while a
  page loads the status line changes at most four times a second, and the
  gadgets change only when what they show does. After each load the window
  prints `OBVIEW_BENCH gui_legacy= gui_load_ms= gui_settled_ms= gui_paints=
  gui_rounds= paint_ms= paint_max_ms= blit_ms= blit_max_ms=` once the page
  has settled; with the environment variable `OB_PAINT_LEGACY=1` it paints
  as before (one area around all the changes, laid out first, after every
  run-loop turn), to compare both in one build.
- **Cookies stay off the disk until needed.** The cookie database keeps its
  journal in memory and does not sync, instead of writing, syncing and
  deleting a journal file for every cookie.
- **Scripts and pictures can be switched off** (Settings menu, or the tool
  types `JAVASCRIPT=NO` and `PICTURES=NO`), the quickest way through a heavy
  page.
- **Fonts come from memory, unhinted.** FreeType read each glyph from the
  disk through stdio and hinted every glyph as it was drawn; a window sat at
  31% while its first text was laid out. On AmigaOS FreeType now reads a font
  file into memory once (openamigafreetype), and `fonts.conf` turns hinting
  off.
- **Two connections per host** (`0007` in `webkit/`), and the C-loop
  interpreter built with `-O2` while the rest is built for size.

**Infinity on AmigaChrome's AC090 FPU (fixed in AmigaChrome 0.30.0).** On
older AC090 builds an infinity compared unequal to itself. WTF's hash tables
mark empty double keys with +infinity, so a table keyed by a double never
found an empty slot and looped: www.bbc.co.uk stopped in
`Style::Resolver::keyframeRulesForName()`, found by sampling the stuck
task's stack (`tests` programs `fpinf` and `fpedge`, in the build tree).
Real 68881, 68882 and 68040 FPUs were never affected.

**A title window while it starts (5 October 2026).** The browser is about
100 MB, and loading it from disk took 10 seconds or more with nothing on the
screen. `OpenBrowser` is now a small launcher (`ob_launcher.c`): it opens a
title window at once, loads `OpenBrowser.engine` with `InternalLoadSeg()`
and a progress bar that follows the bytes read, then runs it in its own
process with `RunCommand()`, so the browser keeps the launcher's icon, tool
types, Shell arguments and `PROGDIR:`. Started from Workbench, the launcher
puts a copy of its Workbench message on its own port for the engine's
start-up code. The engine reports its steps to the title window through the
public port `OPENBROWSER.SPLASH` (`ob_splash.h`) and closes it when its own
window is open; started directly, without the launcher, it simply finds no
port. Once the window is open, the status line names each file being
fetched, since one can take seconds (a secure connection to a new site,
about ten).

**One engine (0.8.2 test, 23 October 2026).** The package used to carry two
76 MB engines with the same window: `OpenBrowser.engine`, which the launcher
loaded itself, and `Libs/WebBrowser/WebBrowserEngine`, which
webbrowser.library starts (`ob_browser_engine.c` is `ob_browser.c` built
with `OB_BROWSER_IN_ENGINE`), with the fonts, fontconfig, blocklist and ICU
data twice. The launcher now only asks webbrowser.library for the window
(`WB_OpenBrowser`), from `LIBS:` or, before Install, from the drawer's
`PROGDIR:Libs/`, and the library starts `LIBS:WebBrowser/WebBrowserEngine`
or else the drawer's `PROGDIR:Libs/WebBrowser/WebBrowserEngine`. The
engine's files go once, in `Libs/WebBrowser/`, its `PROGDIR:`. The title
window is unchanged; the engine reports to it as before.
`OpenBrowser.engine` is still a target (`build-webcore.sh
build-window-engine`) for tests that run the window directly.

## Experiments, 5 October 2026

**Drawing commands instead of pixels.** `obcore-view -dl` paints a page
twice: directly with cairo, and through WebKit's display-list recorder
(which keeps the drawing commands), replaying the list with cairo. On the
test instance, 800x600, caches warm:

| Page | Commands | Glyphs | Bytes of commands | Paint (cairo) | Recording | Replay |
| --- | --- | --- | --- | --- | --- | --- |
| `test1.html` | 148 | 334 | 4.4 KB | 440 ms | 80 ms | 360 ms |
| example.com | 215 | 714 | 7.7 KB | 1,660 ms | 60 ms | 1,780 ms |
| 300 paragraphs and a list | 316 | 221 | 6.4 KB | 760 ms | 180 ms | 520 ms |

The replayed pictures match the direct ones pixel for pixel. 75 to 96% of
painting is cairo and pixman turning commands into pixels; WebCore deciding
what to draw is the rest. A screen's commands are a few kilobytes against
1.9 MB of pixels, so handing them to something faster to draw (a GPU, the
host) would cut painting by 4 to 25 times.

**Fetching through the PC.** With `OB_FETCH_PROXY` set (patch 0010,
`scripts/ob-fetch-proxy.py`; both gone with curl in patch 0075, where
openweb.fetch's card does this job), another PC makes the connections and
the TLS handshakes. AmigaChrome does not let an instance reach the PC it
runs on, so the proxy ran on a second PC on the LAN. example.com took 38 s from the
Shell command with TLS on the 68k and 27 s through the proxy: each new https
site costs the 68k about 10 seconds of TLS. Wikipedia's Amiga article
fetched all its files quickly either way and was still loading after 7
minutes: on heavy pages the 68k's own work on the page (style, layout,
scripts) is the bottleneck, not the network.

**Pictures decode on the main task.** WebKit decoded big and animated
pictures on work-queue threads: each a 2 MB stack, no gain on one CPU, and
threads that kept the program from exiting. Both settings are off.

## Open problems

**login.live.com stalls after its page arrives (4 October 2026).** The page
comes back (HTTP 200, 33 KB), and then WebCore's main thread makes no more
progress: the page never reports its document finished, asks for none of its
scripts or preloads (all on logincdn.msauth.net), and the run loop's 15-second
heartbeat in `obcore-view` stops, so the thread is stuck inside one run-loop
turn, in parsing or script. The network thread is not the cause: polling
idle at its higher priority leaves the main task its time. example.com, whose
script adds paragraphs in several languages, loads and draws. Next steps:
sample the main task's program counter while it is stuck, or load a saved
copy of the page from disk and remove its inline scripts one at a time.
A likely cause, not yet re-tested: until WebKit patch 0008 (5 October 2026)
every `for (k in o)` loop ran forever on the 68k, which would leave the main
thread inside one script exactly like this.

