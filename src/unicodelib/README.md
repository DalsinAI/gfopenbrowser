# unicode.library

One shared copy of Unicode for every AmigaOS 3.2 program on a 68040:
ICU 78.3's code and its data, loaded once, in place of a copy of ICU in
each program. OpenBrowser's engine carried 3.4 MB of ICU code and read
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
data file its own way; OpenBrowser does, in `ob_icudata.c`.

Only ICU's C interface goes through the library: the programs use no
`icu::` C++ class (checked against the 0.8.1 engine's archives). The calls
are ordinary C calls, arguments on the stack, the way they were.

## The library

| File | What it is |
| --- | --- |
| `calls.txt` | The 261 calls in table order: what the engine, JavaScriptEngine, harfbuzz-icu and libpsl call (`nm -u` of the 0.8.1 build). Then the two converter callbacks WebCore hands to ICU, and what the OpenFont text path adds. Append only. |
| `gen_calls.py` | Makes the library's table (`ul_table.c`) and the programs' stubs (`ul_stubs.s`) from it. |
| `unicode_lib.c` | The RomTag, the one call `UnicodeTable(count)` (LVO -30, `sfd/unicode_lib.sfd`), and the data: the first call reads `icudt78b.dat` from `LOCALE:Unicode/`, `LIBS:Unicode/` or the caller's `PROGDIR:Unicode/` and gives it to ICU. |
| `ul_runtime.c` | What ICU needs from a C library, made safe to share between programs: one memory pool under a semaphore for `malloc` (and ICU's `new`), pthread mutexes, conditions, once and keys on Exec semaphores and task signals for libstdc++'s `std::mutex`, `std::condition_variable` and `std::call_once`, the 4-byte atomics under Forbid, `getenv` from DOS variables, and `fopen` that fails at once (all of ICU's data is in the loaded block, so ICU never needs a file). libnix's start-up is not linked: the libraries its maths and locale code call are opened here and libstdc++'s constructors run here. |
| `ul_start.s` | The file's first code: run as a program, the library returns. |
| `ul_client.c`, `ul_client.h` | The program side: the boot table's opener, `ul_client_open()`, and closing the library at exit. |
| `sdk/` | `libraries/unicode.h` by hand, and what OpenNDK's `ndkgen.py` makes from the SFD (`scripts/gen-unicodelib-sdk.sh`). |
| `tests/UnicodeProps.c` | Checksums of the property and case calls over every code point (`UPROPS` lines), to compare with ICU's on the host (`own/tests/run-host.sh` writes the expected lines). |
| `tests/UnicodeTest.c` | The self-test, built as a program on the library: NFC, case mapping, a converter calling back through the stubs, IDNA, line and word breaking, collation, number and date formatting, the host time zone; timings; `UnicodeTest TASKS` also breaks text in three tasks at once. |

ICU keeps one state for all programs, as it does for the threads of one
program. Its heap is the library's pool, so expunging the library frees
all of it in one call, even what a crashed program never closed.

## Our own code (PLAN.md step 7, second half)

ICU's code leaves the library one piece at a time, keeping the 261 calls
and their table. `own.txt` names the calls our own code answers;
`gen_calls.py` points their table entries at `uo_<call>`, and the rest stay
ICU's. Our code's data is made at build time from the Unicode Character
Database of ICU 78's Unicode version, 17.0.0 (`scripts/fetch-ucd.sh`,
pinned by sha256; `own/ucd.sha256` checks every file read).

| File | What it is |
| --- | --- |
| `own.txt` | The calls that are ours. |
| `own/gen_props.py` | Makes `uo_data.c` from the UCD: three tries (the property record, the binary properties, the case record), each the smallest of a few shapes, plus blocks, script extensions, mirroring, full case mappings, script names and error names. ICU's enum numbers come from its own headers, since the programs use ICU's interface. It stops the build when SpecialCasing's conditional mappings or CaseFolding's Turkic ones change, as `uo_case.c` writes them out. |
| `own/uo_props.c` | General category, bidi class, combining class, mirroring, the `u_is*` calls, the 58 binary properties in the UCD files and ICU's POSIX ones, the integer properties WebKit asks for, blocks, scripts and script extensions, script names, error names. |
| `own/uo_case.c` | Simple and full case mapping and folding, with SpecialCasing's conditions (final sigma; Lithuanian, Turkish and Azerbaijani i and dot above). |
| `own/tests/run-host.sh` | On the host: `host_props.c` compares every call with ICU 78.3's for every code point (and -2, -1, 0x110000, 0x110001), every property, and the case calls on every code point, on strings of up to three of the characters the conditions look at, and on 20,000 random strings, in 18 locales, with buffers exact, short and long. Then it builds `tests/UnicodeProps.c` against ICU and writes its lines: the Amiga build of the same program, on the library, must print the same. |

Piece 1, properties and case (1 November 2026), is the first: 28 calls,
those that ran on real pages and in JavaScriptCore (most of all
`u_charType`, `u_hasBinaryProperty`, `u_toupper`, `ublock_getCode` and
`u_charDirection`). Inside them, a property no WebKit code asks for (the
binary properties of ICU's own, such as `UCHAR_SEGMENT_STARTER`, and the
integer properties other than those listed in `uo_props.c`) and Greek
uppercasing still go to ICU, as do `u_strToTitle` and the `uset_*` calls.
The tables are 108,906 bytes.

## Building

1. ICU 78.3 for the 68040: `scripts/build-icu.sh` (as for the engine);
   its `share/icudt78b.dat` is the trimmed data (`icu/data-keep.lst`).
2. `scripts/build-unicode-library.sh [OUT]` with `OAB_DEPS`, `NDK_INCLUDE`
   and `TOOLCHAIN_DIR` (the OpenAmigaGCC release) or `STOVE` (os32-gcc16),
   and the UCD 17.0.0 in `$OAB_DEPS/ucd-17.0.0` (`scripts/fetch-ucd.sh`)
   or `UCD_DIR`.
   It checks that nothing is left undefined and that none of libnix's
   per-program code (start-up, stdio, environment, malloc) is linked.
3. OpenBrowser builds on the library by default: `scripts/build-webcore.sh`
   (`OB_ICU_DATA=library`) makes it in `BUILD/unicode-library` with the
   folder's stove and `NDK_INCLUDE`, again only when this folder or ICU's
   libraries change, and the engine and JavaScriptEngine link its stubs.
   `OB_ICU_DIR` points a build at one made elsewhere; `OB_ICU_DATA=file`
   goes back to ICU's code linked in. An existing folder switches either
   way at its next `configure-engine`, with a relink (the ICU headers are
   the same). `test-browser` stages the library in `Libs/` and
   `package-browser.sh` puts it in the drawer's `Libs/`.

Install: `unicode.library` in `LIBS:` (OpenBrowser's Installer and
`Install` do it), `icudt78b.dat` in `LOCALE:Unicode/` or `LIBS:Unicode/`.
A program also finds the library in its own `Libs/` or the folder above
(`PROGDIR:Libs/`, `PROGDIR:/`, as OpenBrowser's engine in
`Libs/WebBrowser/` does), and the data in its own `Unicode/` drawer when
that program is the first to call the library.

CI: `.github/workflows/unicodelib.yml` builds ICU, the library and the
test, and runs the test in amitools' vamos (68040).
