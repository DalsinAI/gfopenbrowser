# GFOpenBrowser Lite 0.4.2

Date: 3 November 2026

0.4.2 is 0.4.1 rebuilt with a fixed compiler. Dale, 3 November: "rebuild everything on open5". The source is the
same apart from the version.

## Why

0.4.1 was built with GFOpenAmigaGCC 16.2.0-open3. GCC 15 and 16's late-combine pass can compile a copy loop into
`move (An)+,(d,An,Xn)`. The 68k steps An before it works out the destination, so each element lands one place on
(GCC PR 123853). GFOpenAmigaGCC has carried the fix since open4 (gfopengcc `patches/gcc/0013`). The autoinc scanner
(amicradle-guest `tools/scan_autoinc_overlap.py`) finds no such store in 0.4.1's GFOpenBrowserLite, so this is a
precaution, not a bug fix you would see.

## What changed

- `$VER: GFOpenBrowserLite 0.4.2 (3.11.2026)`; the readme's Version is 0.4.2.
- Built on daletop's os32-gcc16 stove, GFOpenAmigaGCC **16.2.0-open5**, with GFOpenLayout at the commit CI pins
  (ce9f92d). The scanner finds no store (2 programs, 0 failing).
- The cloud CI (lite, jslib, jsonlib, unicodelib, bench) now downloads the **16.2.0-open4** release (sha256
  `495319a2...`) instead of open3. open5 isn't a published release yet; moving to it means changing the release
  name and sha256 in the workflows. A local build with the open4 release and GFOpenNDK (`TESTS=1`) passes.

## The archive

`scripts/package-lite.sh` gives `build/package/GFOpenBrowserLite-0.4.2.lha` (72,447 bytes, sha256
`d1331e662421ae04d80f8cb11aec986f3ef350c0dbfe06572e0027ecbe095ffc` from daletop's open5 build). It is in GFOpenUp
0.8.13 as the GFOpenBrowserLite part. A draft release v0.4.2 carries it; Team 5 publishes it.
