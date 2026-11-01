# javascript.library

JavaScript for any AmigaOS 3.2 program on a 68040: JavaScriptCore with our
68k JIT, shared by every program that opens the library. The program owns
its objects and hands them to scripts as **host classes** (tables of
methods, accessors and fields); the library owns the scripts.
Lives here for now; it moves to its own repository later.

```c
struct Library *JavaScriptBase = OpenLibrary("javascript.library", 1);
JSTask t = JS_NewTask(NULL);
JSValue v;
if (JS_Evaluate(t, "6 * 7", -1, "sum.js", &v) == JSERR_OK) {
    LONG n;
    JS_ToLong(t, v, &n);            /* 42 */
    JS_Free(t, v);
}
JS_EndTask(t);
CloseLibrary(JavaScriptBase);
```

From the Shell: `JS script.js arg1 arg2` or `JS EVAL "print(6 * 7)"`.

The engine's `JSON.parse` parses through json.library (WebKit patch 0058),
so `LIBS:json.library` (version 1) must be installed beside it.

## Translate once

A task made with `JSA_CacheDir` keeps what the engine makes from each
script in that folder: the script's own code when it first runs, and each
function's when that function is first called. The next run of the same
text loads them and makes nothing again; code never called is never made.
One file per script text (`<hash>-<length>.jsc`), written when the task
ends; a file from another engine version, or a spoilt one, is made again.
`JS` keeps its FILE scripts in `LIBS:JavaScript/Cache` (`CACHE` for
another folder, `NOCACHE` for none). In the tests a 1,500-function script
runs in about 950 ms the first time and 170 ms loaded.

What is kept today is JavaScriptCore's bytecode, as `jsc --diskCachePath`
keeps it. Keeping the 68k template JIT's code beside it is the next step.

## How it is put together

| Part | What it is |
| --- | --- |
| `javascript_lib.c` | The library (3.6 KB, C, no C library): RomTag, the 30 vectors in the SFD's order, each in its registers. It holds no engine: `JS_NewTask` starts `LIBS:JavaScript/JavaScriptEngine` the first time, then calls the engine's table through its public port; every other call goes straight to the table the task carries. |
| `jsl_cache.cpp` | Translate once: each script's translations kept in, and loaded from, the task's cache folder. On JavaScriptCore's private interface, so built with its own flags. |
| `jsl_core.cpp` | The engine core, on JavaScriptCore's C interface: script tasks, values, calls, host classes (one wrapper per program object, found again through a weak reference; the class's release function is told when the collector drops it), errors with file and line. |
| `jsl_engine.c` | JavaScriptEngine's main: the public port `JAVASCRIPT.ENGINE` with the table; starts and ends script tasks on its own process; quits after `ENV:JavaScriptIdleQuit` seconds (60) with none alive. |
| `jsl_os.h`, `jsl_os_exec.c`, `jsl_os_posix.c` | What the core needs from the system: a script thread (a pthread) and its mailbox (an Exec port on AmigaOS). The POSIX layer is for the tests. |
| `jsl_engine.h`, `jsl_engine_amiga.h` | The table, and the engine's port, shared by the library and the engine. |
| `JS.c` | The Shell command: `print`, `readFile`, `writeFile`, `arguments`. |
| `sfd/javascript_lib.sfd`, `sdk/` | The calls (LVO -30 to -204), and the SDK GFOpenNDK's `ndkgen.py` makes from them (`scripts/gen-jslib-sdk.sh`); `sdk/Include_H/libraries/javascript.h` by hand. |
| `check_vectors.py` | The library's vector table against the SFD. |

A **script task** is a thread with its own JavaScriptCore VM, heap and
global object. Everything about its scripts runs there, one job at a time.
Any task may call the library: a call from another task is handed to the
script task and waited for; a call from the script task itself (a host
class's function calling back) runs at once. Script tasks run side by
side; the 68k interpreter's register file is swapped at each task switch
(WebKit patch 0015's `tc_Switch`/`tc_Launch` hooks).

Host-class functions run on the script task. They must not use the C
library of the program that registered them (it is another task); Exec and
DOS calls are fine. A slot function returns a new value (`JS_New*`,
`JS_Dup`, `JS_Wrap`) or NULL for undefined, and throws with
`return JS_Throw(task, "message");`.

## Building and testing

- Library and JS: `scripts/build-javascript-library.sh` (os32-gcc16 or the
  GFOpenAmigaGCC release, GFOpenNDK). CI: `.github/workflows/jslib.yml`.
- Engine: `scripts/build-webcore.sh build-javascript` in the browser's
  build folder. JavaScriptEngine is then built on the browser's own
  JavaScriptCore, with the same patches (the 68k JIT included) and flags
  (MinSizeRel with -O2 for JavaScriptCore, pc-relative, one hunk, section
  GC), folded like the browser's programs, and it reads ICU's data from
  `LOCALE:Unicode/` or `LIBS:JavaScript/Unicode/` (OB_ICU_DATA=file). The
  older way, `scripts/build-javascript-engine.sh` after `scripts/build-jsc.sh`
  (a separate JSCOnly build, Release -O3, ICU's data linked in), still works.
- Core tests: `tests/jslib/run.sh` builds `tests/jslib/core_test.c` with the
  core against the 68k JIT lab's m68k-linux JavaScriptCore and runs it under
  qemu-m68k (68040): 69 checks of values, calls, errors, host classes,
  wrapper release by the collector, jobs, translate once (saved, loaded,
  added to, a spoilt file made again), and three tasks from three
  threads. They also pass with `JSC_slowPathAllocsBetweenGCs=50`.

Install: `javascript.library` in `LIBS:`, `JavaScriptEngine` in
`LIBS:JavaScript/`, `JS` in `C:`.

Next (the design's phases): the 68k code kept with the translation, then
the host classes proved on GFOpenBrowser Lite, then GFOpenBrowser itself on the
library.
