# GFOpenBrowser's start-up and size: what to cut, what to make Amiga-centric

Measured 6 October 2026 on GFOpenBrowser 0.4 (68040 build), Instance-24
(AmigaOS 3.2.3, the current AC090 emulator).

## Where the bytes are

`GFOpenBrowser.engine` is 102 MB stripped (116 MB with symbols): 64 MB of code,
13 MB of data, nearly all of it read and relocated by LoadSeg at every start.

| Part | Size | What it is |
| --- | --- | --- |
| WebCore | 41.9 MB | HTML, CSS, layout, painting, the DOM, and 14.7 MB of generated JavaScript bindings |
| ICU data | 12.7 MB | Unicode tables: 5.2 MB of text converters (190 of them), 3.0 MB of word-break dictionaries (Chinese/Japanese, Thai, Khmer, Burmese, Lao), 0.9 MB collation, 3.6 MB the rest |
| JavaScriptCore | 12.3 MB | The JavaScript engine (C-loop interpreter) and its built-ins |
| ICU code | 3.4 MB | |
| SQLite, libxml2, HarfBuzz, FreeType, cairo, curl, fontconfig | 4.4 MB | |
| pixman | 0.5 MB | |

Inside WebCore, by area: rendering 4.6 MB, CSS 3.9, platform 3.4, page 3.0,
style 2.9, DOM 2.7, SVG 2.6, HTML 2.5, Web Inspector 1.8, loader 1.5, workers
1.5, editing 1.4, layout 1.2, accessibility 1.0.

## Start-up today

On the bench, starting `obcore-view`, loading a small local page, painting it
and quitting takes 7 to 8 s in all; fonts are ready at once (fontconfig's
cache is kept in `PROGDIR:fontconfig/cache`). On the older emulator it was
about 40 s. On a real 68040 the program read dominates: 102 MB at a few MB/s
from an IDE or CF disk is 20 to 50 s, and the program needs that much RAM
before it does anything.

## What we can cut

| Cut | Saves | Cost |
| --- | --- | --- |
| ICU converters: keep the ~40 encodings the web uses (WHATWG list), drop EBCDIC and the other legacy IBM tables | about 4 MB | none for web pages |
| ICU word-break dictionaries: move to a separate file, read only when such text appears | 3 MB from the program | Thai/CJK pages pay a one-off read |
| Web Inspector backend | 1.8 MB | no remote debugging (we have none) |
| Accessibility tree | up to 1 MB | no screen readers on the Amiga today |
| Unused web APIs in the bindings (payments, WebAuthn, WebXR, Bluetooth, gamepads and similar) | a few MB, to be measured per API | those APIs report "not supported", which sites expect |

Together roughly 102 MB → 85–90 MB. The program stays big: the engine itself
(layout, CSS, JavaScript) is most of it and is what makes real pages work.

## Making it Amiga-centric

- **Resident engine.** The launcher keeps `GFOpenBrowser.engine` loaded after
  the first start (a resident segment, as `Resident` does for commands), so
  the second start skips the 102 MB read entirely: near instant on any Amiga.
  This is the biggest start-up win, larger than any cut.
- **pixman.** It is small (0.5 MB); its cost is per-pixel compositing at run
  time, 75–96 % of paint time. Keep it for anti-aliased shapes and text, and
  hand the bulk work to what the Amiga has: on AmiCradle AC090's magic
  pixman (native code, planned in the bridge thread); on real Amigas solid
  fills, copies and scaled pictures through graphics.library and the RTG
  card (`FillPixelArray`, `BltBitMapRastPort`, `ScalePixelArray`), which use
  the card's blitter.
- **Fonts.** Keep TrueType through FreeType (scalable, every script, what pages
  expect); Amiga bitmap fonts would look wrong at most sizes and lack the
  characters. What to add is a glyph cache: rasterised glyphs kept on disk
  per font and size, beside the picture cache, so text on a revisited page
  needs no FreeType work.

## Order

1. Resident engine (start-up).
2. ICU trim: converters and dictionaries (size, RAM).
3. pixman fast paths through graphics.library/RTG on real Amigas; magic
   pixman with the bridge thread on AmiCradle.
4. Glyph cache with the picture cache.
5. WebCore trims (Inspector, accessibility, unused APIs), measured one by one.
