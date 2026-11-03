# Test pages for GFOpenBrowser's page layer

Pages for `obcore-view`, which runs GFOpenBrowser's page layer (WebCore with
JavaScript, the DOM and timers) without a window and prints what the page
does: `OBVIEW_TITLE`, `OBVIEW_ALERT`, `OBVIEW_CONSOLE` and so on. Copy the
pages and `obcore-view` to the Amiga and run it from a Shell, with the
fonts in `PROGDIR:Fonts` and gfopenfont.library 1.4 in `LIBS:` (or
`PROGDIR:Libs`) as for GFOpenBrowser itself.

| Page | Command | What it shows | Result on AmigaOS 3.2.3, 4 October 2026 |
| --- | --- | --- | --- |
| `test1.html` | `obcore-view test1.html 800 600 test1.png` | Text, a table, colours and the three font families, painted to a PNG | Draws the page; compare `test1.png` with a desktop browser |
| `test3.html` | `obcore-view test3.html` | A script builds a list, sets the title and fires a timer | `OBVIEW_TITLE After script: {"ok":true,"n":5}` and `OBVIEW_ALERT Timer fired` |
| `test4.html` | `obcore-view test4.html` | JavaScript probes, one console line each | Every line has a value; none says `THREW` |
| `test5.html` | `obcore-view test5.html` | `insertAdjacentHTML` and `innerHTML` with Latin-1, Arabic and Cyrillic text (UTF-8 on big-endian CPUs) | `OBVIEW_TITLE t5 ascii ps=3; wide ps=5; b=2 i=2` |
| `input.html` | `obcore-view -input input.html` | A click into a text box, typing an address key by key, then a click on a link | `OBVIEW_ALERT clicked with someone@example.com keys=19 inputs=19` |
| `cjk-decoders.html` | `obcore-view cjk-decoders.html` | `TextDecoder` on 46 inputs in Shift_JIS, EUC-JP, ISO-2022-JP, GBK, gb18030, Big5 and EUC-KR, against the Encoding Standard's indexes (WebKit patch 0064) | added 1 November 2026; see the PR |
| `cjk-text.html` | `obcore-view cjk-text.html 800 600 cjk-text.png` | Japanese, Chinese and Korean lines drawn with the CJK font (`scripts/fetch-cjk-font.sh`), not as boxes: six runs measured against three missing glyphs | `OBVIEW_TITLE cjk text pass=6 fail=0`; added 3 November 2026 |
| `forin.html` | `obcore-view forin.html` | `for (k in o)` over a small object, an array and a 1000-key object | `OBVIEW_TITLE forin names=abcd sum=10 array=012 big=1000/499500` (before WebKit patch 0008 the loops never ended, and the program crashed) |

The XML pages (`xml.html`, `xml-xhtml.xhtml`, `xml-svg.svg`) test WebKit
patch 0030, XML through OXP. Their commands and expected results are in
`../xml-oxp/README.md`.

`obcore-view -input` clicks at (30, 35), types `someone@example.com` with
Intuition raw key codes, as GFOpenBrowser's window does, then clicks at
(30, 110). `-wait seconds` changes how long it lets a page load (120 by
default), and `-url` loads an address from the network instead of a file.
`-dl` also paints through WebKit's display list and prints `OBVIEW_DL`
lines: the drawing commands, their size and how long painting, recording and
replaying took (see docs/WEBCORE_PORT.md).
`-text` prints the page's text after the load (`OBVIEW_TEXT`, up to 2000
bytes). Every run ends with an `OBVIEW_BENCH` line of timings and memory,
which the benchmark reads (docs/BENCHMARKS.md, tools/bench).

MIT, Copyright (c) 2026 AmiCradle Team.
