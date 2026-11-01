# Benchmarks: speed and compatibility

How fast GFOpenBrowser (WebKit's WebCore on AmigaOS 3.2, 68040) and
GFOpenBrowser Lite load and draw pages, how much memory they use, and how
much of the web they understand. The tools are in `tools/bench/`, the pages
in `tests/bench/`.

**Emulator numbers are not 68040 numbers.** The lab runs an emulated Amiga
(AmiCradle instances with a JIT and hundreds of MB of Fast RAM). Its
times depend on the host PC, the JIT settings (`PROBE_ENV`), what else the
host is doing and the network. Use them to compare one build with another
on the same instance, never as what a real A4000/040 or A1200/040 would
do (which is slower, often many times slower, and has far less memory).

## What runs

For every page in `tests/bench/pages.txt` (sets `local`, `compat`, `live`):

1. **obcore-view** (the engine without a window) loads the page, runs it
   until it has loaded, paints it at 800x600 and saves `png/<id>.png`.
2. **obfetch** (GFOpenBrowser Lite's network layer and its HTML-to-text, no
   layout), for `live` pages, when it is staged.
3. Optionally (`--lite-gui SECONDS`, experimental) **GFOpenBrowserLite**'s
   window on `live` pages: opened, given that many seconds, then stopped
   with Ctrl-C. This gives Lite's memory with a page open, not a time: Lite
   says nothing when it has drawn a page.

Each program run sits between two `obstamp` lines (time and free memory).

### The pages

| Set | Pages | Why |
| --- | --- | --- |
| `local` | `text` (long text), `table` (300 rows), `grid` (CSS grid and flexbox cards), `images` (120 small `data:` PNG and SVG pictures), `dom` (JavaScript building, sorting and changing the DOM), `jsbench`, `jsops`, `test1` | Fixed pages from this repository, no network: the numbers move only when GFOpenBrowser does |
| `compat` | `features` (our CSS/HTML/JS feature probe), `acid3` (http://acid3.acidtests.org/) | What the engine supports |
| `live` | about 20 common sites: Wikipedia, BBC News, the Guardian, GitHub, Google, YouTube, Amazon, Reddit, Hacker News, MDN, Stack Overflow, eBay, GOV.UK, W3C, DuckDuckGo (HTML), amiga.org, EAB, Aminet, example.com | Real pages over https://, fetched read-only, one request for the page and whatever it loads |

The `local` and `compat` pages are our own (MIT). No site's pages, fonts
or test suites are kept in this repository: live pages are fetched when
the benchmark runs, Acid3 is loaded from its own site, and test262 is
fetched at run time from a pinned commit with a SHA-256 for every file.

## The numbers

obcore-view prints one `OBVIEW_BENCH` line at its end; `report.py` turns it
into these columns (times in ms from the moment obcore-view asked for the
page, unless said otherwise):

| Column | Meaning |
| --- | --- |
| `startup_ms` | Program start to the engine being ready (WebCore, JavaScriptCore, fonts, network set up). Counted from the program's start, not the page's |
| `first_response_ms` | The page's first response committed: the server answered and WebCore started on the document (`OBVIEW_URL`) |
| `first_paint_ms` | The first time WebCore had something to draw (its first invalidation). The nearest thing to "first paint" without a window |
| `load_done_ms` | WebCore said the load was over (the page and everything it waits for) |
| `last_resource_ms` | The last request ended |
| `painted_ms` | obcore-view's picture of the page was finished. obcore-view paints once, after the load, when no timer is due within 50 ms or, for a page whose animations or timers never stop, once the network has been quiet for 2 s (`-settle`, the page option `settle=N`; `OBVIEW_SETTLED` in the log): about when a window shows the finished page |
| `wall_ms` | The whole run of obcore-view, program loading and start-up included, from `obstamp` (20 ms steps) |
| `requests`, `failed`, `bytes` | Requests made, requests that failed (blocked hosts count), bytes received as WebCore saw them arrive |
| `mem_image` | Free memory lost between `obstamp` and obcore-view's own first line: mostly the program itself (the 104 MB engine) |
| `mem_engine` | Memory the engine took to start (before any page) |
| `mem_peak` | Most memory in use while the page ran, from the lowest `AvailMem(MEMF_ANY)` seen (sampled at most every 100 ms; can miss short peaks) |
| `mem_painted` | Memory in use once the picture was made |
| `mem_kept` | Free memory before the program minus after it ended: memory it did not give back (fragmentation and leaks show here) |
| `lite_fetch_ms`, `lite_fetch_bytes`, `lite_fetch_status`, `lite_fetch_rc` | obfetch: the page fetched over HTTP(S) and turned into text, wall time from `obstamp`; body bytes; HTTP status |
| `lite_gui_mem_open` | With `--lite-gui`: memory in use while Lite's window showed the page |
| `features_pass`/`features_total` | The feature probe: features the engine reports (it detects support; it does not check drawing) |
| `acid3` | Acid3's score out of 100, read from the page's text |
| `js_total_ms` | Sum of the `JSBENCH` times (`jsbench`, `jsops`) or the `dom` page's total |
| `similarity`, `pixel_match` | From `compare.py`: SSIM of the Amiga picture against the Chromium reference, in %, and the share of pixels within 24 of it |
| `rc`, `timeout` | obcore-view's return code (the worst of the runs); 1 in `timeout` when the host gave up on the page |

With `--repeat N` each number is the median of the N runs. An obcore-view
built before `OBVIEW_BENCH` still works: times then come from its
`OBVIEW_LOADED [ms]` lines, and there are no memory numbers.

## Running it

On the lab machine, from a checkout of this branch, with the engine and
`obcore-view` built (`scripts/build-webcore.sh build-tests`: only
GFOpenBrowser's own files recompile) and an instance whose
`S:User-Startup` runs `DH1:Probe/runner.script` (`tools/runner.script`):

```sh
export INSTANCE_ROOT=/path/to/instance RUNTIME_PORT=NNNN
export WEBKIT_BUILD=$PWD/build/Amiga OB_FONTS=/path/to/ttf
B=$INSTANCE_ROOT/devices/harddisks/DH1/Bench

# 1. stage: programs, fonts, pages, AmigaDOS scripts (OB_LITE_DIR: Lite's build/os3)
python3 tools/bench/stage.py --out "$B" --lite "$OB_LITE_DIR"
#    one set or a few pages:  --sets local   --pages example,wiki-amiga   --repeat 3

# 2. run: one page per job, the instance restarted if a page hangs
R=bench-results/$(date -u +%Y%m%dT%H%M%SZ)
tools/bench/run-bench.sh "$R"            # or: tools/bench/run-bench.sh "$R" example wiki-amiga

# 3. references and comparison (any Linux PC with Chromium, e.g. Playwright's)
python3 tools/bench/make-refs.py --out "$R/ref" --sets local,compat,live
python3 tools/bench/compare.py "$R/png" "$R/ref" --csv "$R/compare.csv" --diff "$R/diff"

# 4. JavaScript conformance: the test262 slice, one jsc run
python3 tools/bench/test262_slice.py fetch --out "$INSTANCE_ROOT/devices/harddisks/DH1/Probe"
tools/run-js-suite.sh /path/to/jsc 3600 t262_all > "$R/t262.txt"

# 5. report, against the last run
python3 tools/bench/report.py "$R" --compare "$R/compare.csv" --js "$R/t262.txt" \
    --baseline bench-results/previous/results.csv
```

`stage.py` needs `obstamp` for the Amiga: it builds it with the stove's
`m68k-amigaos-gcc` (`OS32_GCC16`, or `OBSTAMP_CC`), or takes one with
`--obstamp` (CI builds one: the `obstamp-68040` artifact of the Benchmark
tools workflow). Without the host scripts, from an Amiga Shell in the
staged drawer: `Execute OBBench`; the logs are then in `logs/` and
`report.py` reads the drawer itself. (An obcore-view that stops with `OBVIEW_ABORT` waits
for Ctrl-C so that its stack can be read: `Break` it and OBBench goes on
to the next page. `run-bench.sh` restarts the instance instead.)

Steps 3 and 5 can run anywhere with Python 3, Pillow and NumPy
(`python3 -m pip install pillow numpy`). Make live references close in time
to the Amiga run: live sites change, and differ by country and cookie
banner. Prefer `headless_shell` (Playwright's `chromium_headless_shell`):
full Chromium's new headless mode keeps part of the window for its top bar.

### Output

- `results.csv`: one row per page, the columns above.
- `summary.md`: the table (engine and Lite times, memory, compatibility),
  the test262 result, the features the probe did not find and, with
  `--baseline`, what got worse or better: a time more than 20% and more
  than 0.5 to 1 s slower, peak memory 20% and 2 MB higher, a lower
  similarity (5 points), feature or Acid3 score, a return code that was 0
  and is not, a new time-out. `report.py` exits with 3 when something got
  worse.

### test262

`tests/bench/test262-slice.txt` pins a test262 commit and about 100 tests
(built-ins and language, no negative, async or module tests) with the
SHA-256 of each file and of the harness. `test262_slice.py fetch` downloads
exactly those from raw.githubusercontent.com and refuses all of them if
one hash differs; it writes `t262_all.js`, which loads the harness once and
each test inside try/catch in one jsc run (jsc takes about 30 s to start
on the emulator) and prints `T262_PASS`, `T262_FAIL` and a `T262_SUMMARY`
line. Tests share one global object there, so a test that breaks a
built-in for the others would show as a run of failures; `--standalone`
also writes one file per test for `run-js-suite.sh`. test262 is
BSD-3-Clause (Ecma International); its licence is written beside the files
as `LICENSE-test262`. To move the pin: check test262 out at the new
commit and run `test262_slice.py select --checkout DIR --commit SHA`.

The existing jsc tests (`tests/js/a*.js`) run the same way:
`tools/run-js-suite.sh jsc 600 a1 a2 ...` after copying them to DH1:Probe.

## Self-tests

```sh
python3 -m unittest discover -s tools/bench/tests -v
```

They run in CI (`.github/workflows/bench.yml`) with the test262 slice's
hashes checked against the network, `obstamp` built and `obcore_view.c`
compiled for syntax with GFOpenAmigaGCC.

MIT, Copyright (c) 2026 The AmiCradle Team.
