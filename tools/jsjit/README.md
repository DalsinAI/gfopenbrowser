# The 68k template JIT (J2): tools

JavaScriptCore on the 68k runs our own interpreter: offlineasm's M68K back end
(patches 0015, 0017, 0020) turns each bytecode handler into 68k code. Upstream
JavaScriptCore has no JIT for any CPU with 32-bit pointers, so J2 builds its
JIT from those same handlers: each one becomes a **template** that is copied
once per bytecode instruction into a code buffer, with the instruction's
operands written into the copy. The copies run one after another without the
interpreter's dispatch or operand decoding, on the interpreter's own frames
and registers, so compiled code and the interpreter can hand over at any
bytecode instruction.

| File | What |
| --- | --- |
| `m68k_templates.py` | Writes `LLIntTemplates.h` (templates and their hole tables) from the generated `LLIntAssembly.h` |
| `stitch.h` | The stitcher's core: copy a template, fill its holes |
| `check_templates.py` | Checks an assembled set: no PC-relative reference leaves a template, every hole is where its table says |
| `test/run_qemu_test.sh` | Stitches real `op_mov` templates and runs them under `qemu-m68k -cpu m68040` |
| `test/bitops/run.sh` | Patch 0101's bit operation code (int32, float path, slow path) under `qemu-m68k -cpu m68040`, checked against ToInt32 |

## How a template differs from its handler

- **Operand reads become holes.** A load from `(K,a3,a2.l*1)` (PB + PC + K)
  that is sign- or zero-extended becomes `move.l #imm,dN`; the stitcher reads
  the operand from the bytecode (size and sign from the hole's kind) and writes
  it into the immediate.
- **Dispatch becomes a check.** Fetching the next opcode and jumping through
  the opcode map becomes `cmpa.l #next,a2; jeq end; jmp redispatch`. The
  stitcher writes this instruction's PC plus its length as `next`: when PC
  moved by exactly one instruction the copy falls through into the next one,
  and otherwise (a jump, a slow path that changed PC, an exception) it leaves
  through `jsc_m68k_tjit_redispatch`.
- **Code outside the template is reached absolutely, labels inside it
  PC-relatively**, so a copy runs at any address.
- PB (a3) and PC (a2) stay valid, so anything a template still reads from
  the bytecode, and every slow path, works as in the interpreter.

All 585 JavaScript handlers (narrow, wide16, wide32) convert, with 2,745 holes.

## Running the checks (cloud container or any Linux)

```sh
apt-get install -y binutils-m68k-linux-gnu g++-m68k-linux-gnu qemu-user
# LLIntAssembly.h from a 68k build; it can also be generated on Linux, see
# the JS JIT handover notes
tools/jsjit/test/run_qemu_test.sh path/to/LLIntAssembly.h
```

The host test uses m68k Linux's calling convention; the templates themselves
only use the interpreter's registers, so they are the same on AmigaOS.

## The lab build (jsc for m68k Linux)

The numbers in `lab/RESULTS.md` come from `jsc` cross-built for m68k Linux
and run under `qemu-m68k -cpu m68040`, counting 68k instructions
(`lab/count.sh`), never wall time.

1. `apt-get install g++-14-m68k-linux-gnu qemu-user nodejs`. Build ICU 78.3
   twice: for the host (`runConfigureICU Linux`), then for m68k
   (`configure --host=m68k-linux-gnu --with-cross-build=<host build>
   --enable-static --disable-shared --with-data-packaging=static`).
2. Check out and patch WebKit as `webkit/README.md` says.
3. Changes for this build only, never part of a patch: let the build run
   the Python generators on m68k Linux too (`if (WTF_OS_AMIGA OR
   WTF_CPU_M68K)` where `Source/cmake/WebKitCommon.cmake` picks the Ruby
   interpreter and where `Source/JavaScriptCore/CMakeLists.txt` picks
   offlineasm-py); no ELF `.size` directives in `offlineasm-py/asm.py`
   (the 68k labels carry an underscore); and skip the `-quiet_NaN`
   static_assert in `PureNaN.h` for `CPU(M68K)`, as on the Amiga.
4. CMake with `-G Ninja -DPORT=JSCOnly`, a toolchain file for
   `m68k-linux-gnu-gcc-14` with `CMAKE_SYSTEM_PROCESSOR m68k`, `-m68040`,
   `-static`, Release at `-O3`, `-DENABLE_JIT=OFF -DENABLE_DFG_JIT=OFF
   -DENABLE_FTL_JIT=OFF -DENABLE_WEBASSEMBLY=OFF -DUSE_SYSTEM_MALLOC=ON
   -DEVENT_LOOP_TYPE=Generic -DENABLE_STATIC_JSC=ON`.
5. Build `LowLevelInterpreter.cpp.o`, make `defsyms.txt` from it (see
   `lab/fix_ninja_m68k_linux.py`), run that script, and build `jsc`.
   Ubuntu's static m68k libm has no `fmod`: link a one-line `fmod.x` shim
   before `-lm`.
6. `lab/check.sh path/to/jsc` runs the correctness tests against node;
   `lab/count.sh` counts a test, and `lab/page/` has the page tests.
