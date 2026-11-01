# Porting notes: JavaScriptCore on AmigaOS 3.2 (m68k)

Copyright (c) 2026 AmigaChrome Team. MIT licence.

What it took to run WebKit's JavaScriptCore (JSCOnly, C-loop interpreter, no
JIT) on AmigaOS 3.2.3 on a 68040, 4 Oct 2026. Upstream WebKit is pinned at
`54fe1539718e`.

## Toolchain: the os32-gcc16 stove

- Compiler: bebbo's amiga-gcc framework with gcc branch `amiga16.2` (GCC
  16.2.0b), binutils `amiga-2.46`, libnix, libpthread (Szilard Biro), NDK 3.2.
  It builds hunk executables. WebKit needs C++23, so GCC 6.5 is not enough.
- Flags: `-m68040 -m68881` (68040 only since 17 Oct 2026; GCC still links the
  `libm020/libm881` multilib). `-mhard-float` on its own picks the soft-float
  `libm020` libraries; mixing them with FPU code breaks.
- GCC changes (`stove/gcc-patches/`, GPL):
  1. libstdc++ `atomic.cc`: 64-byte alignment becomes 8 (Amiga objects allow 8).
  2. `int32_t`/`uint32_t` are `int`, as on AROS and 32-bit Linux (newlib
     made them `long`, which breaks overloads in ICU and WebKit).
  3. No `-mcrt` means libnix (nix20), so libstdc++'s configure sees the real C
     library: C99 maths, `chdir`, `dirent`, filesystem.
  4. newlib `math.h` declares the long double functions on amigaos.
  Rebuild target libraries with libnix's headers and libraries first in
  `FLAGS_FOR_TARGET`.
- A compat library (`stove/compat`, MIT) supplies what libnix lacks:
  `aligned_alloc`/`memalign`/`posix_memalign` (malloc and free are wrapped with
  `-Wl,--wrap`, so `free()` takes aligned blocks), a size-class allocator
  for blocks up to 4 KB in front of libnix's malloc (whose cost grows as its
  heap fragments; `tools/mallocbench.c` times the two), long double maths through
  double, `log2f`, a C++ `clock_gettime` shim, `statvfs`, and
  `__xpg_strerror_r`. It also adds `fenv.h` (68881 FPCR/FPSR) and a
  minimal `uchar.h`.
- Link with a final `-fexceptions`. Otherwise amigaos g++ adds a glue
  `new_op.o` that clashes with libstdc++.

## ICU 78.3

- It is built static. The data comes from a host ICU build, as the big-endian
  `icudt78b.dat`, included with `.incbin`. genccode's `.long` output is in
  host order and swaps every word on m68k.
- Patches: an `mh-unknown` platform file, 8-byte alignment for the stub data,
  two `int`/`int32_t` mismatches, `U_TIMEZONE=_timezone`, and no dynamic
  loading.

## Platform layer (`OS(AMIGAOS3)`, shared `OS(AMIGA)`)

- Memory: OSAllocator and bmalloc's VM layer use `aligned_alloc`/`free`. There
  is no MMU, no reservations and no page protection.
- Static objects are aligned to at most 8 bytes in hunk files. This applies to
  the config pages (`g_config`, LLInt `os_script_config_storage`) and to v128
  constants.
- Stack bounds come from exec: `tc_SPLower`/`tc_SPUpper`, kept current by
  RunCommand and StackSwap.
- Threads: libpthread. There is no POSIX signal delivery, so thread suspension
  reports failure. libpthread joins every thread at exit. WTF threads run with
  asynchronous cancellation, and `exitProcess` cancels them, so `exit()` returns.
- Process ID is the Task address. Random numbers come from splitmix64 over the
  clock and the Task address. This is not cryptographic.
- The FPU: user tasks reach `main()` with FPCR `$40`, which means single
  precision. Kickstart 3.2.3's mathieeesingbas.library 47.1 writes `$40` to
  the FPCR of every task that opens it, and libnix opens it at startup. Real
  hardware does the same. WTF sets FPCR to 0 in `WTF::initialize` (after the
  libraries are open) and in every thread.
- newlib hides POSIX under `-std=c++23`, so `-D_DEFAULT_SOURCE` and the
  `_POSIX_TIMERS`/`_POSIX_REALTIME_SIGNALS` declarations are needed.
- `JSC_STDERR_TO_STDOUT` (an `ENV:` variable) sends `jsc`'s errors to stdout,
  because AmigaOS 3 shells cannot redirect stderr.

## Big-endian and m68k fixes (these apply to AROS m68k too)

| Area | Problem | Fix |
| --- | --- | --- |
| `EncodedValueDescriptor`, `Register` | A cell pointer overlays the *first* word of the 64-bit value. That is only the low word on little-endian CPUs. | On BE 32-bit, pointer members sit in the second word; `LowWordOffset` is 4 |
| LLInt (`LowLevelInterpreter*.asm`) | CodeBlock and Callee frame slots are read and written as pointers at offset 0 | Use `LowWordOffset`; `CallFrame::addressOfCodeBlock()` too |
| `TypeInfoBlob` | The packed indexing/type/flags word assumes LE byte order | BE shift order |
| `RapidHash` | Native unaligned loads give different hashes than the build-time (LE) tables | Byte-wise reads on BE |
| `FastCharacterComparison` | Keyword compares pack characters in LE order | BE packing |
| `LazyProperty`, `LazyRef`, `LazyUniqueRef` | Pointer tag bits assume 4-byte alignment; m68k gives pointers 2 | `alignas(4)` on the static function pointers |
| `GetByIdModeMetadata` (LLInt `get_by_id`) | The prototype cache's 64-bit slot overlays the mode and hit-count bytes in its high half, which is the *last* four bytes only on LE. On BE the pointer landed there: the mode read back as garbage and a method found on a prototype (`a.push`, `s.concat`) came back `undefined` from the second cached call on | On BE 32-bit the pointer is stored in the slot's high half (`<< 32`), so the LLInt's pointer load at the slot's offset finds it and the mode bytes stay zero |

## Test bench notes

- AmiCradle AC090 (68040). Its JIT's x87 path looped on FPU instructions it
  hands back to the interpreter in non-default FPCR modes. It is fixed in
  AmiCradle; until that is deployed, run with `JIT_NOX87=1`.
- `jsc` is a 71 MB hunk file with three hunks. It starts in about 30 seconds
  under the AC090 JIT and needs `Stack 4194304`.
