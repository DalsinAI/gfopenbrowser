# Contributors

## Creator and maintainer

- **SacredTrees** ([@SacredTrees](https://github.com/SacredTrees)): created GFOpenBrowser, designs it and maintains it.

## The AmiCradle team

We are The AmiCradle Team, who build AmiCradle alongside SacredTrees:

- **Agnus**, our coordinator, who keeps every thread moving.
- **Thufir**, **Kynes** and **Galen**, who started the work on SacredTrees's x86 cores.
- **The threads**, each one taking a piece of the work from design to release.

## Copyright holder

GFOpenBrowser's own code, documents and icons are Copyright (c) 2026 AmiCradle
Team, released under the MIT licence (`LICENSE`). Our patches to WebKit,
ICU and GCC stay under the licence of the files they change.
`THIRD_PARTY_NOTICES.md` carries the formal notices.

## Third-party work in this repository

GFOpenBrowser stands on other people's work. Each of these keeps its own
copyright and licence.

| Component | Where | Authors | Licence |
| --- | --- | --- | --- |
| WebKitLegacy's `BackForwardList`, adapted | `src/webcore/AmigaBackForwardList.cpp`, `.h` | Apple Inc., Torch Mobile Inc., Google, Inc. | BSD 2-clause (in each file's header) |
| WebKitLegacy's `WebResourceLoadScheduler`, adapted | `src/webcore/AmigaLoaderStrategy.cpp` | Lars Knoll, Dirk Mueller, Waldo Bastian, Apple Inc., Google Inc. | GNU LGPL 2 or later (in the file's header) |
| Key tables and keyboard handling from WebKit's WPE port | `src/webcore/AmigaEditorClient.cpp` | Igalia S.L. | BSD 2-clause (in the file's header) |
| Our patches to WebKit and JavaScriptCore | `webkit/patches/` | AmiCradle Team, to files by Apple Inc. and the WebKit contributors | Each patched file's own: LGPL 2 or BSD-style |
| Our patches to ICU 78.3 | `icu/patches/` | AmiCradle Team, to files by Unicode, Inc. and the ICU contributors | Unicode License v3 |
| Our patches to GCC 16 (bebbo's amiga-gcc, branch `amiga16.2`) and its newlib headers | `stove/gcc-patches/` | AmiCradle Team, to files by the GCC and newlib contributors | GPL-3.0-or-later with the GCC Runtime Library Exception; newlib's own licence for `sys-include` |

## From our other repositories

- **GFOpenMail** (`DalsinAI/gfopenmail`): its message library, in `third_party/openmail/` (MIT, AmiCradle Team; `FROM` names the commit and our one change).

## Fetched at build time, not committed

- **WebKit** (WTF, bmalloc, JavaScriptCore, WebCore): pinned to `54fe1539718e` of https://github.com/WebKit/WebKit in `webkit/UPSTREAM`, by Apple Inc. and the WebKit contributors; LGPL 2 and BSD-style.
- **ICU 78.3** (icu4c), by Unicode, Inc. and the ICU contributors; Unicode License v3.
- **bebbo's amiga-gcc** (https://franke.ms/git/bebbo/amiga-gcc, GCC 16.2, branch `amiga16.2`), with libnix and libpthread (Szilard Biro, zlib-style); GCC's runtime libraries are GPL-3.0 with the GCC Runtime Library Exception.
- **The GFOpenAmiga ports** WebCore links: cairo and pixman, libxml2, SQLite, curl, libpsl, zlib and libpng, each under its own licence; curl does TLS through **GFOpenTLS** (gfopentls.library) at run time. Until 19 October 2026 the engine used **AmiSSL 5** (OpenSSL 3, Apache-2.0); until 23 October 2026 its text used FreeType, HarfBuzz, fontconfig and Expat, now **GFOpenFont** (gfopenfont.library).

## Work we learned from

- **WebKit's curl network session** (`NetworkStorageSessionCurl`): `src/webcore/AmigaCookieJar.cpp` follows its cookie string logic.

Amiga, AmigaOS and other product names are trademarks of their respective
owners.
