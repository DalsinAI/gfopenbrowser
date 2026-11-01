# The XML parser under WebCore: OXP

WebKit patch 0030 (`webkit/patches/0030-amiga-xml-parser-oxp.patch`)
parses XML with OXP, the Open XML Parser (DalsinAI/openamigaxml, `oxp/`), in
place of libxml2. XML here covers SVG, XHTML, `DOMParser`, `responseXML` and
`innerHTML` on XHTML.

- **`XMLDocumentParserOXP.cpp`** is WebKit's `XMLDocumentParserLibxml2.cpp`
  with the parser's creation, feeding, stopping, position, entities and
  errors moved onto OXP. It keeps WebKit's licence (LGPL-2).
- **`XMLDocumentParserOXPBridge.h`** hands OXP's events to
  `XMLDocumentParser` in libxml2's shapes. The bridge uses only the C++
  standard library, so it is tested here on its own.

## The bridge test (on the host)

`bridgetest.cpp` is a receiver with `XMLDocumentParser`'s SAX methods. It
prints the calls it gets in the form of openamigaxml's
`tests/ref/libxml2dump`, which prints libxml2's own SAX2 events. Then:

```sh
sh tests/xml-oxp/compare.sh /path/to/WebKit /path/to/openamigaxml/oxp files.txt
```

Result, 17 October 2026, on 4,780 readable files on a Linux system (3,000
SVG, 42 XHTML, 1,738 other XML):

- **4,772 the same.**
- **6 refused by both.**
- **2 differ, by design.** `&nbsp;` (and one other entity) appear with an
  external DTD whose public ID is not XHTML's. libxml2 reports an error and
  goes on; OXP skips the entity, as XML 1.0 says.

In an XHTML document, the backend's `resolveEntity` gives `&nbsp;` as
U+00A0. `bridgetest -x` shows it. A fragment with an undeclared prefix gives
a non-fatal error and an element with no namespace, as libxml2 does
(`bridgetest -f`), and WebCore's fragment parser fills the namespace in.

## Pages (on the Amiga, with `obcore-view`)

| Page | Command | What it shows | Expected |
| --- | --- | --- | --- |
| `../pages/xml.html` | `obcore-view xml.html` | `DOMParser` and `XMLSerializer`: namespaces, CDATA, a not-well-formed document, an internal entity, the entity-expansion limit | `OBVIEW_TITLE xml urn:a urn:b\|x\|1\|t&u 4 err ent limit ser` |
| `../pages/xml-xhtml.xhtml` | `obcore-view xml-xhtml.xhtml` | An XHTML 1.0 Strict page: `&nbsp;` and `&copy;` from HTML's names, the XML version, `innerHTML` | `OBVIEW_TITLE xhtml a0 a9 1.0 i` |
| `../pages/xml-svg.svg` | `obcore-view xml-svg.svg 400 300 xml-svg.png` | An SVG with `xlink:href`, a gradient and text | Draws the picture; compare with a desktop browser |

Not run yet. The patch now applies last, on top of the Ruby-free WebKit
toolchain (0019, 0021) and the GFOpenTLS curl (0022); the next engine build
with OXP in the dependency prefix runs them.

## Building with it

OXP's library and headers go into the WebCore dependency prefix in place
of libxml2's:

```sh
OUT=$OAB_DEPS/m68k-amigaos sh /path/to/openamigaxml/oxp/build-amiga.sh
```

This writes `lib/liboxp.a` and `include/oxp.h` there. CMake finds them with
the patch's `Source/cmake/FindOXP.cmake`, and `src/webcore/CMakeLists.txt`
links `oxp` instead of `xml2`.

MIT, Copyright (c) 2026 AmigaChrome Team (this folder; the WebKit patch
keeps WebKit's licences).
