# javascript.library DS: JavaScript as a demo coder's source language

Team 5, 23 November 2026, for Dale ("lets write up the ideas for javascript.library (ds variant)").
Ideas and a plan, not yet built. Nothing here changes how web pages run: the browser's page scripts keep
JavaScriptCore's full semantics. DS is an opt-in mode of javascript.library for programs written for it:
demos, effects, games, tools.

## The idea

javascript.library today runs any script on JavaScriptCore with our 68k template JIT (J2). That is a general
engine: every `x + y` may be an int, a double, a string or an object, so every operation checks types, and much
of the time goes into the engine rather than the program's work.

DS treats JavaScript as a **source language that is compiled to 68k code**, the way demo groups have always
generated exactly the code an effect needs. The program says what it needs (`"use ds"` at the top of a file or
function, or `JSA_Mode, JSM_DS` when the program is compiled) and gets, in return, code close to hand-written
68040 assembly for its hot paths.

## The levels

| Level | What | Gain we expect over J2 | Keeps full JS meaning? |
|---|---|---|---|
| 1 | **Compile ahead (AOT):** parse, an SSA IR, an optimiser, 68k code | 3–10× on most code | yes |
| 2 | **Types fixed by the program:** int32, double and fixed 16.16, inferred from the code and from hints | another 2–5× on maths | within the DS subset |
| 3 | **Hot loops compiled again with what they actually saw** (a trace or second tier), guarded | 2–3× on loops | yes, with guards |
| 4 | **Maths by table:** `Math.sin`/`cos` from tables at the precision asked for | 10–50× on those calls | no: opt-in, `ds.fastMath` |
| 5 | **Fixed point:** `fx` numbers (16.16) with integer add, multiply and shift | 3–10× against the FPU on a 68030; less on a 68040 or 68060 | no: opt-in, a type of its own |
| 6 | **Effects generated per use:** `createParticleRenderer(50000)` builds a renderer for exactly that | as hand-written | yes |
| 7 | **Compiled on the host:** levels 1–3 run on the Pi's ARM through the services card, and the 68k gets the finished code | compile time off the 68k entirely | yes |

## The DS subset

**Kept:** functions and closures, `let`/`const`, arrays and typed arrays, plain objects with a fixed shape
(classes), numbers, strings, `Math`, modules.

**Left out in DS code:**
- `eval`, `with`, `Proxy`, `Reflect`, changing a prototype after creation, `arguments`;
- getters and setters on hot objects, exceptions in hot loops.

A DS function that meets something outside the subset at run time does **not** carry on with a wrong answer:
- **Strict (the default):** it stops with a TypeError naming the line.
- **`"use ds loose"`:** that call goes to J2, slower but right.

Code without `"use ds"` in the same program runs on J2 as today, so a program can be DS only where it matters.

## Types

- **int32 by default** for numbers that only ever hold integers in range (proven by the compiler, or declared:
  `let i = ds.int(0)`), with overflow checked (a check per add, as `addx`/`bvs` costs nothing much) or wrapping
  when the function says `"use ds wrap"`.
- **double** for anything with a fraction, on the 68040/060 FPU.
- **fx** (16.16 fixed point) only when asked: `let a = ds.fx(0.05)`. `fx * fx` is `muls.l` and a shift;
  `ds.fx.sin(a)` is a table lookup. Mixing `fx` and `double` without a conversion is a compile error.
- Typed arrays (`Int16Array`, `Float32Array`, `Uint8Array`) are raw memory, with bounds checked once per loop
  where the compiler can prove the range.

## The intrinsics (`ds.*`)

- `ds.fx`, `ds.int`, `ds.fx.sin/cos/sqrt/recip` (tables, 1024 or 4096 entries, chosen per program).
- `ds.chunky(width, height, depth)`: a pixel buffer GFOpenRTG shows; `ds.present()`.
- `ds.audio.*` through AHI, `ds.time()` on the beam or the host clock.
- `ds.gen(fn, constants)`: compile `fn` again with the given values fixed (level 6). For example, a particle
  update for exactly 50,000 particles, unrolled four times.
- `ds.hot(fn)`: compile now rather than on first use, for no stutter in an effect.

## How it runs

```
 script ──► parser ──► IR (SSA, typed) ──► optimiser ──► 68k emitter ──► code in the program's memory
              ▲                                                            │
              └──────────── the cache: by a hash of the source and the CPU (68030/040/060) ◄─┘
```

- **On the Amiga:** the compiler is part of javascript.library DS (built from C, like the rest of the library).
  It compiles a function the first time it runs, or at load with `ds.hot`.
- **With the services card (level 7):** the library sends the source; the card compiles it on the host's ARM
  (the same compiler, built for Linux) and sends back the code and its relocations; the library loads it like a
  hunk. Without the card, it compiles on the Amiga as above. The result is the same code either way, so a
  program never depends on the card.
- **Ahead of shipping:** `JS -compile effect.js` writes `effect.jsds`, the compiled code for each CPU; a demo ships
  that and never compiles at all.

## On our machines: what this does and doesn't fix

Measured on the PiMiga on 23 Nov, a GitHub page load (about 28 s): the emulation thread spends 45–63% of its time
in the **emulator's own JIT translating 68k code** (compile, decode, cache flushes), and only 14–51% running the
translated code. A page load runs a great deal of code once. DS doesn't change that: it is for programs whose hot
code runs many times, where it matters most. The page-load cost is a separate fix in AC090's JIT (translate a
block only after it has run a few times; batch the cache flushes), which helps every program.

## A plan

1. **The benchmark first:** four effects in plain JS (plasma, tunnel, particles, an SDF raymarch), timed on J2
   today on the PiMiga and the CM4, and counted in 68k instructions in the lab (`tools/jsjit/lab/count.sh`).
2. **Levels 1–2 for a first subset** (int32 and double, functions, typed arrays, loops): the compiler on the
   host first (fast to iterate), its output run under qemu-m68k against J2's results.
3. **The library side:** `"use ds"`, the cache, loading compiled code, strict and loose fallback.
4. **Levels 4–6:** `ds.fx`, tables, `ds.gen`, `ds.chunky`.
5. **Level 7:** the compile service on the card.
6. **Level 3:** a guarded second tier for hot loops.

Each step is measured against step 1's numbers.

## Open questions

1. Is the compiler ours from scratch (small, 68k-only, a few thousand lines) or built on an existing JS front end
   (for example QuickJS's parser) with our own IR and 68k back end?
2. Should DS be a separate library (`jsds.library`) or a mode of javascript.library?
3. Is the first user a demo of our own, or a port (an existing JS demo or game)?
