# Third-party notices

GFOpenBrowser's own code is MIT (see LICENSE). These parts are not ours and keep
their own licences:

| Path | What | Licence |
| --- | --- | --- |
| `webkit/patches/` | Changes to WebKit / JavaScriptCore source files | The licence of each patched file: LGPL-2 or BSD-style, as stated in its header in WebKit (https://github.com/WebKit/WebKit) |
| `icu/patches/` | Changes to ICU 78.3 source files | Unicode License v3 (ICU) |
| `stove/gcc-patches/` | Changes to GCC 16 (bebbo amiga-gcc, branch amiga16.2) and its newlib headers | GPL-3.0-or-later with the GCC Runtime Library Exception (GCC); newlib's own licence for `sys-include` |

Patch 0019 (`webkit/patches/0019-jsc-generators-in-python.patch`) adds Python
ports of WebKit's Ruby build scripts so that JavaScriptCore builds without
Ruby: `Source/JavaScriptCore/offlineasm-py/` (offlineasm's front end, from
`offlineasm/*.rb`), `Source/JavaScriptCore/generator-py/` (the bytecode
generator, from `generator/*.rb`) and `Source/WTF/Scripts/GeneratePreferences.py`
(from `GeneratePreferences.rb`). They are derived works of Apple Inc.'s
BSD-licensed originals (Copyright (C) 2011-2023 Apple Inc.): each file keeps
Apple's copyright notice and licence text, with a line for the AmiCradle
Team's port, and stays under that BSD licence. The M68K back end
(`offlineasm-py/offlineasm/m68k.py`, a port of our own `m68k.rb`), the
BytecodeList reader (`generator-py/RubyListReader.py`) and `Source/cmake/no-ruby.py`
are ours (MIT, AmiCradle Team).

Patch 0021 (`webkit/patches/0021-webcore-generators-in-python.patch`) does the
same for WebCore: `Source/WebCore/domjit/generate-abstract-heap.py`,
`Source/WebCore/Scripts/GenerateSettings.py` and
`Source/WebCore/Scripts/GenerateQuirkBehaviors.py`, ports of Apple Inc.'s
BSD-licensed Ruby scripts (Copyright (C) 2016-2023 Apple Inc.), and the
Python forms of their templates (`*.pyerb`, from Apple's `*.erb`). They keep
Apple's notices and licence, with a line for the AmiCradle Team's port.

`data/public-suffixes` is Mozilla's Public Suffix List
(https://github.com/publicsuffix/list, taken 23 October 2026), rewritten by
`tools/psl-compile.py` (Punycode, sorted, comments left out). It is data
under the Mozilla Public License 2.0 (https://mozilla.org/MPL/2.0/); the
list as published is its source form. `tests/public-suffix-cases.txt` is
the list's own test file (public domain, CC0) in the same form.

`third_party/openmail/` comes from GFOpenMail (AmiCradle Team's mail client,
MIT, same copyright); see its `FROM` and `LICENSE`.

GFOpenBrowser Lite links, at build time, libnix and libamiga from bebbo's
amiga-gcc and uses GFOpenTLS (gfopentls.library) when it runs; until 19 October
2026 it used AmiSSL 5 (OpenSSL 3, Apache-2.0).

The package (`scripts/package-browser.sh`) carries, for compressed pages,
z.library (GFOpenZlib, AmiCradle Team, MIT, with zlib inside under the zlib
licence) and gfopendatatypes' stream.datatype, gzip.datatype and
brotli.datatype (AmiCradle Team, MIT; brotli.datatype is our own decoder,
written from RFC 7932, with the static dictionary taken from the RFC's
text), built from their own repositories.

The package carries fonts that are not in this repository: in
`Libs/WebBrowser/Fonts/` the Liberation and DejaVu faces from the build
machine's `OB_FONTS`, and, for Chinese, Japanese and Korean text, Droid Sans Fallback
Full from the Android Open Source Project (Apache License 2.0), which is
gfopenfont's CJK face: gfopenfont's `fonts/cjk/fetch-cjk-font.sh` fetches it at
build time at a pinned sha256, and the package carries it with its
`DroidSansFallback.NOTICE` in `Fonts/` for the Installer to put in `FONTS:`.

Built programs also link, at build time on your machine (none of it is in this
repository):

- WebKit's WTF, bmalloc and JavaScriptCore (LGPL-2 / BSD);
- ICU 78.3 (Unicode License v3);
- libnix and libpthread from bebbo's amiga-gcc (libnix: public domain/its own
  terms; libpthread by Szilard Biro: zlib-style);
- libstdc++, libgcc and libatomic (GPL-3.0 with the GCC Runtime Library Exception).

A `jsc` binary built from this repository is a combined work that includes
LGPL-licensed JavaScriptCore; distribute it under the LGPL's terms (source and
relinking information available). JavaScriptCore stays a separate program; it
is not part of AmiCradle's own core.
