# 68k template JIT: lab results

The AmiCradle Team. jsc (WebKit pin 54fe1539, patches 0001-0024) cross-built
for m68k Linux with `-m68040`, run under `qemu-m68k -cpu m68040`. The numbers
are 68k instructions executed (`count.sh`), with jsc start-up (6.3 M) taken
off. They are not cycles: a real 68040 also pays for memory and branches.
Every test gives the same result with the JIT on and off.

## 22 Oct 2026, array writes one past the end (patch 0052)

A map of the JIT's code to bytecodes (a local build that prints each
instruction's address, matched against `count.sh`'s hot blocks) showed
page-events spending 48% in the Array.prototype.filter builtin, and
15% of the page in its `@putByValDirect(result, nextIndex, current)`:
put_by_val_direct had no fast path, and put_by_val's only stored in
bounds. Both now also store one past the end while the vector has room
(the interpreter's own append), and filter's loop now gets its
register-cached copy. `lab/correct22.js` (appends of int32s, doubles,
objects and mixed values past the vector, gaps, non-extensible, sealed
and frozen arrays and a read-only length part way through, strict mode,
copy-on-write literals, plain objects with indexed properties, filter,
map, slice, concat, flat, Array.from and Array.of, an indexed setter on
Array.prototype, arrays kept across collections) prints what node
prints with the JIT on and off, and under GC stress (N=3 and 50);
correct.js-correct21.js too (44/44). Counts with start-up left in:
page-events 60.44 M to 54.88 M (10% fewer with start-up taken off);
page-vdom 33.33 to 33.27, page-lexer 21.01 to 20.95, page-json 31.14
to 31.02, page-template 51.75 to 51.79.

## 22 Oct 2026, regex start positions skipped (patch 0051)

page-template spent half its time in the regular expression interpreter
(matchDisjunction, testCharacterClass): `/[&<>"']/g` was tried in full at
every position of every string. When the pattern's one alternative
starts with a character, a cased character or a character class, the
interpreter now works out once which Latin-1 characters can start a
match and moves past the other positions in 8-bit strings.
`lab/correct21.js` (36 patterns against 8-bit and 16-bit strings with
Latin-1, Kelvin, long s, dotted I, sigma and an emoji: classes, inverted
classes, ignore case, unicode, ranges, \d \w \s, alternatives,
optional and empty-able starts, ^ with m, \b, lookbehind, back
references, $, empty classes; replace, split, matchAll, search, sticky,
lastIndex, 10 KB input) prints what node prints, and what 0050 printed;
correct.js-correct20.js too (42/42). Counts with start-up left in:
page-template 83.08 M to 51.75 M (40% fewer with start-up taken off);
page-vdom 33.33, page-lexer 21.01, page-events 60.44, page-json 31.14
unchanged; micro-strings 10.71.

## 22 Oct 2026, arguments objects made in line (patch 0050)

page-vdom's `h()` reads `arguments` on every call; making the
DirectArguments (createByCopying, createUninitialized and the slow path)
was about 5% of the page. The JIT's code now takes a cell of the right
size from the cell space's allocator for that size and copies the
arguments in from the frame. `lab/correct20.js` (fewer, as many and
more arguments than parameters, none, 30-39 through apply, sloppy
mapping both ways, a changed length, deleted elements, callee, objects
kept across collections, spread, apply and Array.from of arguments,
strict mode, twelve parameters) prints what node prints with the JIT on
and off, and under GC stress (N=3 and 50); correct.js-correct19.js too
(40/40). page-vdom gives the same result under GC stress (N=3). Counts
with start-up left in: page-vdom 34.89 M to 33.33 M (5.4% fewer with
start-up taken off); micro-calls 13.90 M; the other pages unchanged
(lexer 21.00, events 60.44, json 31.14, template 83.08).

## 22 Oct 2026, arrays made in line (patch 0049)

page-vdom spent about 7% making arrays (`var kids = []` in every
node) in llint_slow_path_new_array, JSArray::tryCreateUninitializedRestricted
and constructArrayNegativeIndexed. Empty arrays, and contiguous ones of
up to eight elements, are now made by the JIT's code: the butterfly and
the cell from the free lists' current intervals, while the allocation
profile keeps its type and the global object is not having a bad time.
`lab/correct19.js` (empty arrays that take int32s, doubles and objects,
literals of every kind of value, eight and nine elements, holes, a site
whose arrays change shape, an indexed setter on Array.prototype part
way through, arrays kept across collections) prints what node prints
with the JIT on and off, and under GC stress (N=3 and 50);
correct.js-correct18.js too (38/38). page-vdom also gives the same
result under GC stress (N=3, 20). Counts with start-up left in:

| page | 0048 | 0049 |
| --- | ---: | ---: |
| page-vdom | 37.22 M | 34.89 M |
| page-lexer | 20.93 M | 21.01 M |
| page-events | 60.49 M | 60.44 M |
| page-json | 31.15 M | 31.14 M |
| page-template | 83.11 M | 83.08 M |

page-vdom runs 7.5% fewer instructions with start-up taken off.

## 22 Oct 2026, Math.min and Math.max in line (patch 0048)

page-vdom spent 2-3% in mathProtoFuncMin and its native call. Two
int32s now take the JIT's own compare. `lab/correct18.js` (int32 pairs
and limits, doubles, -0, NaN, strings, valueOf order, other argument
counts, a replaced Math.min, the function as a value) prints what node
prints with the JIT on and off; correct.js-correct17.js too (36/36).
page-vdom: 38.47 M to 37.22 M instructions (3.8% fewer with start-up
taken off); the other pages are unchanged.

## 22 Oct 2026, Array.prototype.push in line (patch 0047)

The page profiles had arrayProtoFuncPush and the native call trampoline
at 4-6% of page-vdom and page-lexer. Push of one value onto an Int32 or
Contiguous array with room now happens in the JIT's code.
`lab/correct17.js` (int32, object, double and mixed arrays, holes,
frozen, sealed and read-only-length arrays, subclasses, a generic
object, a replaced push, push with no or two values, cells into old
arrays across collections, copy-on-write literals) prints what node
prints with the JIT on and off, and under GC stress; correct.js-
correct16.js too (34/34). Counts with start-up left in, 0046 then 0047:

| test | 0046 (M) | 0047 (M) |
|---|---:|---:|
| page-vdom | 39.77 | 38.47 |
| page-lexer | 21.94 | 20.95 |
| page-events | 60.49 | 60.49 |
| page-json | 31.15 | 31.15 |
| page-template | 83.33 | 83.08 |
| micro-array | 69.00 | 66.45 |

With start-up taken off: vdom 3.8%, lexer 6.1% fewer instructions.

## 22 Oct 2026, scalar string scans (patch 0046)

A profile of page-json put half its instructions in LiteralParser's string
lexing, in emulated 16-byte vector code (SIMD::find: per-byte compares
through the stack, bfins). On M68K, SIMD::find and reverseFind now take
their scalar loop. Counts with start-up (5.5 M) left in, 0045 then 0046:

| test | 0045 (M) | 0046 (M) |
|---|---:|---:|
| page-json | 42.06 | 31.15 |
| page-vdom | 40.03 | 39.77 |
| page-lexer | 22.09 | 21.94 |
| page-events | 60.71 | 60.49 |
| page-template | 83.44 | 83.33 |

page-json: 26% fewer instructions, 30% with start-up taken off.
correct.js-correct16.js print what node prints with the JIT on and off
(32/32).

## 22 Oct 2026, GC stress (patch 0045)

Under GC stress (`JSC_slowPathAllocsBetweenGCs` 1-4, `JSC_useGenerationalGC=0`)
the qemu shell crashed in marking with the JIT on and off, for example on
`function d() { return new Error("trace").stack.split("\n").length; }`
called in a loop. The 68040 lab showed "Out of memory" and wrong values
on the long JSON page. The cause was the conservative scan of the machine
stack: it read only every fourth byte as a pointer start, and 68k code
keeps pointers 2-byte aligned. With 0045 it reads every halfword: every
page test and correct.js-correct16.js print what node prints at N=1, 2
and 3, with the JIT on and off (126/126).

## 21 Oct 2026, timing on the 68040 lab (patch 0043, K09 engine)

The GFOpenBrowser 0.7 engine (-Os, 0001-0043) in a lab copy of the
emulated 68040, three rounds, each test repeated for 2 s, JIT set on and
off with `Set JSC_useM68KTemplateJIT 1` / `0`. The engine reads only
local shell variables: `SetEnv` (ENV:) is not seen, and an earlier run
that used it had the JIT on in both modes. Mean ms per run:

| page | JIT on | JIT off | speed-up |
|---|---:|---:|---:|
| page-vdom | 42.6 | 116.9 | 2.7x |
| page-lexer | 24.5 | 54.3 | 2.2x |
| page-json | 40.8 | 45.9 | 1.13x |
| page-template | 110.9 | 123.4 | 1.11x |
| page-events | | | about 2x (first run) |

page-events builds up state as it repeats, so only its first runs
compare. json and template spend their time in C++ (the JSON parser,
string building). No crashes in 37 runs.

## 21 Oct 2026, objects made in line (patch 0044)

`new_object` and `create_this` now take a cell straight from the
allocator's free-list interval and fill in the header, as the baseline
JIT's emitAllocateJSObject does; an empty interval, a profile without an
allocator, a callee other than the one cached and a poly proto structure
go to the template. `lab/correct16.js` (object literals, points, classes
and subclasses, new.target, constructors that return objects, poly proto,
allocation churn through many GCs, big objects) prints what node prints
with the JIT on and off, as do correct.js-correct15.js (32/32).

Counts with start-up (5.5 M) left in, 0043 then 0044:

| test | 0043 (M) | 0044 (M) |
|---|---:|---:|
| page-vdom | 43.86 | 40.02 |
| page-lexer | 23.39 | 22.08 |
| page-events | 61.72 | 60.71 |
| page-json | 42.08 | 42.05 |
| page-template | 83.48 | 83.44 |

page-vdom (many small node objects) gains 9 per cent, 10 per cent with
start-up taken off; the micro tests are unchanged.

## 21 Oct 2026, virtual calls fixed (patch 0043)

0035's polymorphic call path crashed on a virtual call link: a call
site that had seen more callees than a polymorphic stub holds, or a
construct site whose constructors had never been called (no call
CodeBlock). `lab/correct15.js` (twenty callees at one site, closures,
natives and arrow functions mixed, seven constructors at one `new`,
exceptions through a megamorphic site) crashed with the JIT on before
the fix and prints what node prints with it now. Counts are unchanged.

## 21 Oct 2026, page hot spots (patch 0039)

A profile of the page tests by C++ function (qemu's executed blocks
mapped to `nm` symbols) showed where the time outside the JIT's code
went: `in` on arrays (the array builtins' hole checks) was a third of
page-events; for-in's next name, `arguments.length` and `arguments[i]`,
and `===` on strings were a quarter of page-vdom. Each now has fast
code. Every slow path's call-site bookkeeping (setCurrentVPC) also got
cheaper. `lab/correct14.js` (`in` on arrays of each shape, holes,
prototype elements, non-int32 keys, objects, strings, proxies, arrays
that grow, the array builtins; arguments objects with fewer and more
arguments, overridden length, deleted and redefined elements, sloppy and
strict mode, leaked arguments; for-in over one shape and many, empty
objects, indexed and prototype properties, properties added and deleted
during the loop, nested loops, arrays, strings, null, proxies,
dictionaries; `===` on atoms, strings built at run time, ropes, BigInts
and symbols) prints what node prints with the JIT on and off, as do
correct.js-correct13.js.

Counts with start-up (5.5 M) taken off:

| test | interpreter (M) | 0038 (M) | 0039 (M) | 0039 vs 0038 | vs interpreter |
|---|---:|---:|---:|---:|---:|
| page-vdom     | 154.3 | 61.3 | 38.4 | 1.60x | 4.0x |
| page-events   | 261.7 | 94.4 | 56.2 | 1.68x | 4.7x |
| page-json     |  50.8 | 38.8 | 36.6 | 1.06x | 1.4x |
| page-lexer    |  79.0 | 18.3 | 17.9 | 1.02x | 4.4x |
| page-template |  96.5 | 79.0 | 78.0 | 1.01x | 1.2x |

page-template is mostly regular expressions (the Yarr interpreter, in
C++); page-lexer's get_by_val with string keys made by `slice()` (not
atoms, so hashed in C++) and page-json's JSON.parse stay in C++.

## 21 Oct 2026, small functions inlined (patch 0038)

A call site whose call link holds a JavaScript function of at most 64
bytecode bytes, all of whose instructions have fast code, gets that
function's code written in line, behind a guard on the call link's
CodeBlock. The inlined function may itself call (its frame is finished
first), and a function that calls nothing is inlined into an inlined
one. Anything its fast code does not take finishes the frame and goes on
in the function's own code. `lab/correct13.js` (arithmetic, mixed types,
overflow, methods, primitive `this`, exceptions and stack traces through
inlined frames, getters, a callee replaced and collected, argument
counts, globals, writes, deep recursion and stack overflow, `.call`,
nested calls, garbage collection during an inner call) prints what node
prints with the JIT on and off, also with
`JSC_m68kTemplateJITMaximumInlineSize=600`, as do correct.js-correct12.js.

Counts with start-up (5.5 M) taken off:

| test | 0036 (M) | 0038 (M) | 0038 vs 0036 |
|---|---:|---:|---:|
| calls (small leaf functions) | 12.0 | 8.5 | 1.42x |
| nested (`f` calls `g`, 20000 times) | 5.0 | 3.8 | 1.31x |
| page-lexer    | 18.6 | 18.3 | 1.02x |
| page-vdom     | 61.6 | 61.3 | 1.00x |
| page-events, page-json, page-template, the other micro tests | | | 1.00x |

The page tests gain little: most of their call sites call natives or
several functions, and the functions they call are larger than 64 bytes
and use instructions the inliner does not take (arguments objects, new
objects, scopes, loops, to_object, to_string). Getters reached through
get_by_id are not inlined; the page tests barely use them.

## 21 Oct 2026, property shape tables, getters, strings, loop doubles (patch 0036)

get_by_id sites keep a table of the last four structures they saw, with
own values, getters (own, or on an unchanged prototype; called as op_call
calls) and missing properties; a garbage collection clears the entries
of structures that died. Strings get their own code for `s[i]`, `.length`
(flat strings and ropes) and `charCodeAt`. Loop register copies keep
doubles in fp2-fp6. `lab/correct9.js` (one site seeing six shapes, own
and prototype getters, prototypes changing and losing the property,
missing properties, throwing getters, native getters, proxies, shapes
dying across gc(), primitives, classes with super, dictionaries),
`lab/correct10.js` (characters and codes of 8- and 16-bit strings,
out-of-range indexes, ropes, `.length` of non-strings at the same site,
charCodeAt replaced, called on other receivers and through `.call`) and
`lab/correct11.js` (double loops with NaN, -0, Infinity, int32 overflow,
doubles that turn into ints and strings, read by closures, left by
exceptions, more doubles than FPU registers) print what node prints with
the JIT on and off, as do correct.js-correct8.js.

This lab build is a fresh one (GCC 14, jsc start-up 5.5 M, taken off
below), so its counts differ a little from the sections below.

| test | interpreter (M) | 0035 (M) | 0036 (M) | 0036 vs 0035 | vs interpreter |
|---|---:|---:|---:|---:|---:|
| page-lexer    |  79.6 |  27.5 |  18.6 | 1.47x | 4.3x |
| page-vdom     | 156.7 |  74.2 |  61.6 | 1.20x | 2.5x |
| page-events   | 263.9 |  95.7 |  94.4 | 1.01x | 2.8x |
| page-json     |  51.0 |  38.8 |  38.8 | 1.00x | 1.3x |
| page-template |  97.0 |  79.4 |  79.0 | 1.01x | 1.2x |
| double-loop   |  46.4 |   6.7 |   3.8 | 1.78x | 12.3x |
| closures      |  18.1 |   7.2 |   6.9 | 1.04x | 2.6x |
| array (sort)  | 102.2 |  64.8 |  63.5 | 1.02x | 1.6x |
| int-loop      |  91.2 |   6.0 |   6.1 | 1.00x | 15.1x |
| calls         |  96.1 |  12.0 |  12.0 | 1.00x | 8.0x |
| properties    |  94.2 |  12.5 |  12.5 | 1.00x | 7.5x |
| arrays        |  62.3 |   7.4 |   7.5 | 0.99x | 8.3x |
| strings       |   8.8 |   5.4 |   5.4 | 1.00x | 1.6x |

arrays is about 1% slower (the string path after the array path costs
a check). What the page tests still spend: for-in enumeration, array
builtins (push, `in`, put_by_val_direct), get_by_val with string keys
on objects, `arguments`, string `+=` (C++ ropes), and regular
expressions and JSON in C++.

## 21 Oct 2026, compares, new objects, polymorphic calls, scopes (patch 0035)

A real-page benchmark (`page/`: virtual DOM diff, HTML templating, JSON,
a tokenizer, an event store) showed where pages spend their time, by
opcode (a lab-only map from JIT code addresses to bytecodes): compares and
true/false tests on non-int32 values in the interpreter's copied code,
new properties on objects (cached transitions), calls whose site had gone
polymorphic or whose result is unused, and global and closure variables.
0035 gives all of those their own code. It also fixes a bug from 0031:
after a JIT call the interpreter's sp stayed at the callee's frame, so a
garbage collection cleared the caller's slots below it (`lab/correct8.js`
crashed). `lab/correct6.js` (every kind of value through ===, ==, <,
truthiness, == null), `lab/correct7.js` (polymorphic and closure calls,
.call, constructors, literals, setters on prototypes, late setters) and
`lab/correct8.js` (globals, let/const and TDZ, closures, eval injection,
with, read-only globals, gc()) print what node prints with the JIT on and
off; so do the page tests and correct3-5.

| test | interpreter (M) | 0034 (M) | 0035 (M) | 0035 vs 0034 | vs interpreter |
|---|---:|---:|---:|---:|---:|
| page-lexer    |  80.5 |  49.2 |  28.3 | 1.74x | 2.8x |
| page-events   | 264.4 | 134.6 |  95.6 | 1.41x | 2.8x |
| page-vdom     | 157.0 |  96.5 |  74.4 | 1.30x | 2.1x |
| page-json     |  48.8 |  40.0 |  36.5 | 1.09x | 1.3x |
| page-template |  97.3 |  83.0 |  79.7 | 1.04x | 1.2x |
| closures      |       |  10.2 |   7.2 | 1.42x | 2.6x |
| strings       |       |   5.8 |   5.4 | 1.08x |      |
| calls         |       |  11.7 |  11.9 | 0.98x |      |

calls pays two instructions per call for the sp fix. What the page tests
still spend: regular expressions (Yarr's interpreter) and JSON in C++,
array builtins written in JavaScript (`in` and put_by_val_direct),
for-in enumeration, get_by_id misses on objects of several shapes, and
the C++ slow paths' fixed cost.

## 21 Oct 2026, arrays and calls in loops (patch 0034)

get_by_val, put_by_val and get_length get their own code for int32 and
contiguous arrays; loop register copies also run array access and calls;
a nested entry from C++ saves only the sp and lr slots. `arrays` is a new
jsbench test (1000-element int array, read and written 50 times).
`lab/correct5.js` (holes, prototype holes, out-of-bounds, negative and
fractional indexes, copy-on-write literals, appends, doubles, objects,
mixed and changing shapes, sparse, frozen and typed arrays, array-likes,
arguments, lengths of strings and functions, 2^32-1 length) prints what
node prints with the JIT on and off, as do `lab/correct3.js` and
`lab/correct4.js`; `lab/correct.js` and `lab/correct2.js` are unchanged.

| jsbench test | 0032 (M) | 0034 (M) | 0034 vs 0032 | vs interpreter |
|---|---:|---:|---:|---:|
| arrays      |  25.8 |   7.3 | 3.53x |  8.5x |
| array (sort)|  76.8 |  71.3 | 1.08x |  2.1x |
| closures    |  10.9 |  10.1 | 1.07x |  1.9x |
| calls       |  12.0 |  11.6 | 1.03x |  8.8x |
| int-loop, double-loop, properties, strings, regexp, json | | | 1.00x | |

## 20 Oct 2026, stage J6 (loop variables in registers; patch 0032)

Integer loops get a second copy of their body with up to six variables in
d2-d7, and calls into JavaScript from C++ (the sort comparator here) no
longer copy the whole register file with memcpy twice per call: they keep
only the slots a C call must give back, with a run of move.l. Calls from
compiled code to a compiled function skip the interpreter's prologue (each
function's code starts with its own).
`lab/correct4.js` (sort, map, forEach, reduce, replace callbacks, getters,
JSON reviver, valueOf, toString, exceptions and deep recursion through
callbacks) prints what node prints, with the JIT on and off.
`lab/correct.js`, `lab/correct2.js` and `lab/correct3.js` (overflow
of add, sub, mul and inc/dec inside the copy, values turning into doubles
and back, strings, nested loops, early exits before a variable is written,
-0, valueOf) print the same with the JIT on and off. 0032 leaves the
interpreter's templates as they were.

| jsbench test | J4 (M) | J6 (M) | J6 vs J4 | J6 vs interpreter 0029 |
|---|---:|---:|---:|---:|
| int-loop    |   9.9 |   5.9 | 1.68x | 17.3x |
| properties  |  13.1 |  12.3 | 1.07x |  8.6x |
| double-loop |   6.5 |   6.5 | 1.00x |  7.9x |
| calls       |  16.5 |  12.0 | 1.37x |  8.5x |
| closures    |  10.8 |  10.8 | 1.00x |  1.8x |
| strings     |   5.7 |   5.7 | 1.00x |  1.6x |
| array (sort)| 107.9 |  76.8 | 1.41x |  1.9x |
| regexp      |   7.8 |   7.8 | 1.00x |  1.1x |
| json        |   3.7 |   3.7 | 1.00x |  1.0x |

The register copy covers int32 work and get_by_id/put_by_id (int-loop,
properties); loops that call or work on doubles are not covered yet.
`lab/correct3.js` also covers property loops (values turning into doubles
and strings, two objects at one site, getters, delete), checked against
node.

## 20 Oct 2026, stage J4 (whole functions, calls, property access, doubles; patch 0031)

Interpreter columns: as of patch 0029, and with 0031 (it no longer fills
profiles, and its double arithmetic uses fdadd and friends). Speed-up is J4
against the interpreter as of 0029. `lab/correct.js` and `lab/correct2.js`
(doubles, NaN, -0, overflow, inline and out-of-line properties, polymorphic
sites, prototypes, getters, setters, delete, frozen objects, objects stored,
recursion) print the same with the JIT on and off; `check_templates.py`: 585
templates, 0 problems.

| jsbench test | interpreter 0029 (M) | interpreter 0031 (M) | J3 (M) | J4 (M) | speed-up | J4 vs J3 |
|---|---:|---:|---:|---:|---:|---:|
| int-loop    | 101.8 |  91.0 |   9.9 |   9.9 | 10.33x | 1.00x |
| properties  | 105.1 |  94.0 |  39.6 |  13.1 |  8.01x | 3.02x |
| double-loop |  51.7 |  46.3 |  24.7 |   6.5 |  7.90x | 3.77x |
| calls       | 101.9 |  95.9 |  54.4 |  16.5 |  6.19x | 3.30x |
| closures    |  19.0 |  18.1 |  13.8 |  10.8 |  1.75x | 1.28x |
| strings     |   9.2 |   8.8 |   6.6 |   5.7 |  1.61x | 1.16x |
| array (sort)| 146.6 | 143.4 | 135.7 | 107.9 |  1.36x | 1.26x |
| regexp      |   8.9 |   8.7 |   8.0 |   7.8 |  1.14x | 1.03x |
| json        |   3.6 |   3.6 |   3.7 |   3.7 |  0.98x | 1.00x |

`lab/correct2.js` also covers exceptions thrown through compiled calls,
stack overflow (RangeError), native callees, polymorphic call sites,
constructors, missing and extra arguments, and getters that call.

## 20 Oct 2026, stage J3 (32-bit integer fast paths, patch 0027)

Interpreter column as below; JIT column with the template JIT on (`JSC_useM68KTemplateJIT=1`, the default since patch 0029).

| jsbench test | interpreter (M) | J3 (M) | speed-up |
|---|---:|---:|---:|
| int-loop    | 101.8 |   9.9 | 10.30x |
| properties  | 105.1 |  39.6 |  2.65x |
| double-loop |  51.7 |  24.7 |  2.10x |
| calls       |  99.4 |  54.4 |  1.83x |
| strings     |   9.2 |   6.7 |  1.39x |
| closures    |  18.8 |  13.9 |  1.35x |
| regexp      |   8.9 |   8.0 |  1.10x |
| array (sort)| 143.9 | 135.7 |  1.06x |
| json        |   3.6 |   3.7 |  0.97x |

int-loop's loop body is now about 49 instructions per iteration (was ~460).
jsbench results and `lab/correct.js` are identical with the JIT on and off.

## 20 Oct 2026, stage T4 (peephole pass)

| jsbench test | JIT off (M) | JIT on (M) | change | speed-up |
|---|---:|---:|---:|---:|
| int-loop    | 101.8 |  69.7 | -31.5% | 1.46x |
| double-loop |  51.7 |  36.6 | -29.2% | 1.41x |
| calls       |  99.4 |  77.5 | -22.1% | 1.28x |
| properties  | 105.1 |  69.3 | -34.0% | 1.52x |
| closures    |  18.8 |  15.8 | -15.9% | 1.19x |
| strings     |   9.2 |   7.6 | -17.8% | 1.22x |
| regexp      |   8.9 |   8.4 |  -5.7% | 1.06x |
| array (sort)| 143.9 | 139.5 |  -3.1% | 1.03x |
| json        |   3.6 |   3.6 |  +1.0% | 0.99x |

`lab/correct.js` (15 loops: int overflow, doubles, exceptions, getters,
closures, for-in, switch) prints the same with the JIT on and off.

Wall time under qemu is not a measure: qemu retranslates code whose page is
written, so before JIT code had pages of its own (test hosts only) sort ran
25x slower in the emulator while executing fewer instructions.

## 20 Oct 2026, stage T1 (plain templates)

| jsbench test | JIT off (M) | JIT on (M) | change |
|---|---:|---:|---:|
| int-loop    | 101.8 | 92.7 | -9.0% |
| double-loop |  51.7 | 48.2 | -6.7% |
| calls       |  99.4 | 92.9 | -6.6% |
| properties  | 105.1 | 93.9 | -10.7% |
| closures    |  18.8 | 17.9 | -4.5% |
| array (sort)| 143.9 | 142.8 | -0.7% |
| strings, regexp, json | 9.2, 8.9, 3.6 | same | 0% |

- Only loops are compiled (entry is op_loop_hint); functions without a loop
  (call targets, sort comparators) still run in the interpreter.
- strings, regexp, json and sort spend their time in C++ (string builder,
  Yarr, JSON, the sort), which no JS JIT changes.
- What the templates remove is dispatch and operand decoding. What remains
  is the interpreter's own code: int-loop costs about 460 instructions per
  iteration (8 bytecodes), mostly 64-bit value moves through the register
  file in memory, high-word clears and value profiling. That is the next
  target (DESIGN.md T4, then type-specialised int32 code).
