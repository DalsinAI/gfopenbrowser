# GFOpenBrowser Lite 0.4.3

Date: 4 November 2026

0.4.3 opens GFOpenTLS by its GF name, gfopentls.library. 0.4.2 (and 0.4.1) asked for opentls.library, its name
before the GF rename, through the vendored `third_party/opentls` headers that openmail's `oam_net_amiga.c` is built
on. On an Amiga with GFOpenUp 0.8.13, whose GFOpenTLS part installs gfopentls.library, every page failed with "No
network: OpenTLS (opentls.library) is not installed" (lab t5-up0813-fresh, 4 Nov 2026). They only worked where an
OpenUp 0.8.11 had left opentls.library in LIBS:.

## What changed

- `third_party/opentls/include/libraries/opentls.h`: `OPENTLSLIB_NAME` is `"gfopentls.library"`. The API is the
  same (`third_party/gfopentls` has the renamed headers).
- `third_party/openmail/oam_net_amiga.c`: its step and its message name gfopentls.library and GFOpenUp's GFOpenTLS
  part.
- `$VER: GFOpenBrowserLite 0.4.3 (4.11.2026)`; the readme's Version is 0.4.3.

Built on daletop's os32-gcc16 stove, GFOpenAmigaGCC 16.2.0-open5, with GFOpenLayout at the commit CI pins (ce9f92d).

## Tested

In lab t5-up0813-fresh (a copy of Instance-32, OpenUp 0.8.11 taken out, GFOpenUp 0.8.13 installed): 0.4.2 says "No
network"; 0.4.3 loads https://example.com/ ("text/html: 577 bytes").

## The archive

`scripts/package-lite.sh` gives `build/package/GFOpenBrowserLite-0.4.3.lha` (sha256
`dacfcb8324bfb2eb40cffde651f94b4d0c1663484f90352efe5e037d35dc9a8c` from daletop's open5 build). GFOpenUp 0.8.13 takes
it as its GFOpenBrowserLite part.
