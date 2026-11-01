# 32-bit JavaScript values for the 68k: design 0.1

The AmiCradle Team, 20 Oct 2026.
Status: design only. Stage J3 (32-bit integer fast paths in the JIT) comes first and is what
we measure against; this is the follow-on that changes the engine's value layout.

## Why
JavaScriptCore keeps every value in 64 bits (NaN boxing). On the 68k every value move, compare
and tag check is two words, and the interpreter spends most of its time on that. A 32-bit value
makes integers and pointers single words and halves the memory for values (frames, properties,
arrays of objects).

## Rule: numbers are exact, always
Numbers keep full double precision. The 32-bit form is used only when it holds the number
exactly; anything else is a heap number (16 bytes, garbage collected). No approximation anywhere,
so pages behave exactly as on any browser.

## Encoding (one 32-bit word)
| low bits | meaning | work on the 68k |
|---|---|---|
| `...0` | small integer, value << 1 (31 bits, about ±1.07 billion) | add/sub/compare as is, overflow is the V flag; mul and div shift once |
| `..01` | pointer to a heap cell (cells are 4-aligned) | load with displacement - 1, no untag |
| `..11` | short float: a 32-bit float whose two lowest mantissa bits are zero, or a special (undefined, null, true, false, empty, deleted) from reserved NaN patterns | fmove.s after clearing the tag |

- A double result is stored as a short integer if it is one, else as a short float if
  `(double)(float)d == d` and the two low mantissa bits are zero, else as a heap number.
- -0 is never a small integer (it is a short float), as JavaScript needs.
- Small integers use tag 0 (as V8 does) so integer adds need no untagging; pointers pay a
  displacement of -1, which the 68k's addressing modes do for free.

## What changes in the engine
- `JSValue` (JSCJSValue.h, JSCJSValueInlines.h): encoding, `isInt32`/`asInt32`, `isCell`,
  number boxing (needs the VM to allocate heap numbers).
- `Register` and the call frame: 4-byte slots; the frame header layout changes.
- The interpreter (`LowLevelInterpreter64.asm`) and our 68k back end (`m68k.py`): every tag
  check and boxing macro; the register file's 64-bit pairs become single words.
- Garbage collector: `isCell` checks and conservative stack scanning (tag 01 words).
- Property storage and `WriteBarrier<Unknown>`: 4 bytes. Double arrays keep raw 8-byte doubles.
- The JIT templates regenerate from the interpreter, so J2/J3 carry over.

## Costs and risks
- A fork of JavaScriptCore's core: every WebKit update conflicts there.
- Float-heavy code allocates more (heap numbers). Mitigation: short floats, and the JIT keeping
  doubles in FPU registers inside a loop.
- Full re-validation: test262, the bench pages, long GC soak runs (bugs here show as rare
  crashes).

## Stages
1. J3 (now): 32-bit integer fast paths in the JIT on the 64-bit layout. Measure.
2. V0: prototype the encoding in `JSValue` on the cloud lab (jsc for m68k Linux), interpreter only,
   behind a build flag; run jsbench + test262 slice; count instructions vs J3.
3. V1: interpreter + JIT on 32-bit values, GC soak, browser build.
4. Decide on the V0 numbers whether the fork is worth carrying.
