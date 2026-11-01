# unicode.library

One shared copy of Unicode for every AmigaOS 3.2 program on a 68040:
ICU 78.3's code and its data, loaded once, in place of a copy of ICU in
each program. GFOpenBrowser's engine carried 3.4 MB of ICU code and read
4.6 MB of ICU data at each start; JavaScriptEngine carried another copy of
the code and 5.5 MB of data. With the library, each carries 6.8 KB of
stubs. PLAN.md step 7, first half: ICU leaves the programs. The second
half replaces ICU's code inside the library with our own.

Lives here for now; it moves to its own repository, openamigaunicode,
later.

## How a program uses it

A program is built with ICU's own headers and calls ICU's C functions by
their own names, as before. It links `libicuuc.a` from the folder
`scripts/build-unicode-library.sh` makes (`OUT/icu`) in place of ICU's
three libraries (`libicui18n.a` and `libicudata.a` there are empty). Each
ICU function in it is a three-instruction stub that jumps through a table:

```
_ucol_open_78:  move.l  ___ul_table,a0
                move.l  (304,a0),a0
                jmp     (a0)
```

The table starts as a boot table: the first ICU call from any task opens
the library, fetches its table with `UnicodeTable()`, and goes round
again, so constructors that call ICU early still work. A program may call
`ul_client_open()` (`ul_client.h`) first to report a missing library or
data file its own way; GFOpenBrowser does, in `ob_icudata.c`.

Only ICU's C interface goes through the library: the programs use no
`icu::` C++ class (checked against the 0.8.1 engine's archives). The calls
are ordinary C calls, arguments on the stack, the way they were.

## The library

| File | What it is |
| --- | --- |
| `calls.txt` | The 261 calls in table order: what the engine, JavaScriptEngine, harfbuzz-icu and libpsl call (`nm -u` of the 0.8.1 build). Then the two converter callbacks WebCore hands to ICU, and what the GFOpenFont text path adds. Append only. |
| `gen_calls.py` | Makes the library's table (`ul_table.c`) and the programs' stubs (`ul_stubs.s`) from it. |
| `unicode_lib.c` | The RomTag, the one call `UnicodeTable(count)` (LVO -30, `sfd/unicode_lib.sfd`), and the data: the first call reads `icudt78b.dat` from `LOCALE:Unicode/`, `LIBS:Unicode/` or the caller's `PROGDIR:Unicode/` and gives it to ICU. |
| `ul_runtime.c` | What ICU needs from a C library, made safe to share between programs: one memory pool under a semaphore for `malloc` (and ICU's `new`), pthread mutexes, conditions, once and keys on Exec semaphores and task signals for libstdc++'s `std::mutex`, `std::condition_variable` and `std::call_once`, the 4-byte atomics under Forbid, `getenv` from DOS variables, and `fopen` that fails at once (all of ICU's data is in the loaded block, so ICU never needs a file). libnix's start-up is not linked: the libraries its maths and locale code call are opened here and libstdc++'s constructors run here. |
| `ul_start.s` | The file's first code: run as a program, the library returns. |
| `ul_client.c`, `ul_client.h` | The program side: the boot table's opener, `ul_client_open()`, and closing the library at exit. |
| `sdk/` | `libraries/unicode.h` by hand, and what GFOpenNDK's `ndkgen.py` makes from the SFD (`scripts/gen-unicodelib-sdk.sh`). |
| `tests/UnicodeTest.c` | The self-test, built as a program on the library: NFC, case mapping, a converter calling back through the stubs, IDNA, line and word breaking, collation, number and date formatting, the host time zone; timings; `UnicodeTest TASKS` also breaks text in three tasks at once. |

ICU keeps one state for all programs, as it does for the threads of one
program. Its heap is the library's pool, so expunging the library frees
all of it in one call, even what a crashed program never closed.

## Building

1. ICU 78.3 for the 68040: `scripts/build-icu.sh` (as for the engine);
   its `share/icudt78b.dat` is the trimmed data (`icu/data-keep.lst`).
2. `scripts/build-unicode-library.sh [OUT]` with `OAB_DEPS`, `NDK_INCLUDE`
   and `TOOLCHAIN_DIR` (the GFOpenAmigaGCC release) or `STOVE` (os32-gcc16).
   It checks that nothing is left undefined and that none of libnix's
   per-program code (start-up, stdio, environment, malloc) is linked.
3. GFOpenBrowser builds on the library by default: `scripts/build-webcore.sh`
   (`OB_ICU_DATA=library`) makes it in `BUILD/unicode-library` with the
   folder's stove and `NDK_INCLUDE`, again only when this folder or ICU's
   libraries change, and the engine and JavaScriptEngine link its stubs.
   `OB_ICU_DIR` points a build at one made elsewhere; `OB_ICU_DATA=file`
   goes back to ICU's code linked in. An existing folder switches either
   way at its next `configure-engine`, with a relink (the ICU headers are
   the same). `test-browser` stages the library in `Libs/` and
   `package-browser.sh` puts it in the drawer's `Libs/`.

Install: `unicode.library` in `LIBS:` (GFOpenBrowser's Installer and
`Install` do it), `icudt78b.dat` in `LOCALE:Unicode/` or `LIBS:Unicode/`.
A program also finds the library in its own `Libs/` or the folder above
(`PROGDIR:Libs/`, `PROGDIR:/`, as GFOpenBrowser's engine in
`Libs/WebBrowser/` does), and the data in its own `Unicode/` drawer when
that program is the first to call the library.

CI: `.github/workflows/unicodelib.yml` builds ICU, the library and the
test, and runs the test in amitools' vamos (68040).
