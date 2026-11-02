# GFOpenJSON: json.library

JSON (RFC 8259) for native AmigaOS 3.2 programs on the 68040: a resident
library that parses JSON text into **one flat block of memory** you can pass
around, writes JSON back out, and reads JSON as a stream of events. All our
own code: no third-party JSON or number-conversion code. It lives in
GFOpenBrowser for now and moves to its own repository later.

| File | What it is |
| --- | --- |
| `json_core.cpp`, `.h` | All of the work, in lean C++ with no Amiga headers: the parser, the block calls, JSON Pointer, the writer, the streaming reader, and exact decimal to double conversion both ways. Memory only through `struct oj_env`. |
| `openjson_lib.c` | The library, in C: RomTag, the jump table in the SFD's order, each call in its registers, AllocVec and FreeVec as the env. |
| `sfd/json_lib.sfd` | The 43 calls, LVO -30 to -282. |
| `sdk/` | `Include_H/libraries/json.h` (by hand: the block, the flags, the errors, the events) and what GFOpenNDK's `ndkgen.py` makes from the SFD (`scripts/gen-jsonlib-sdk.sh`): clib, inline, pragmas, proto, FD, LVO equates. |
| `check_vectors.py` | The shell's vector table against the SFD. |
| `tests/JSONTest.c` | The Amiga self-test (every call through the registers), and `JSONTest FILE [BENCH]` to parse a file and time parse, reader and stringify. |

Build: `scripts/build-json-library.sh` (os32-gcc16 or the GFOpenAmigaGCC
release, GFOpenNDK, `-Os`). Tests: `tests/jsonlib/run.sh` (below). CI:
`.github/workflows/jsonlib.yml`.

## The block

`JSONParse` reads the text twice: once to check it and count, once to fill
one allocation of exactly the right size. The block is

```
struct JSONDoc   32 bytes: magic 'JSN1', size, node count, offsets, pool size, flags
struct JSONNode  8 bytes each, in document order
pool             strings (unescaped UTF-8, each NUL-terminated) and number texts
```

Nothing in it is a pointer: every reference is an offset or a node index,
so a block can be copied with `CopyMem`, sent in a message to another task,
written to disk and loaded back, and it is still the same document. Many
tasks may read one block at once; the library never changes a block after
making it. `JSONCheckDoc` checks a block that came from somewhere you do
not trust (every offset, length, string's UTF-8, number's grammar and
container's extent) before you read it; the tests damage blocks at random
and walk the ones it passes under the sanitizers.

A node is `jn_Head` (type in the top 4 bits, a 28-bit field) and `jn_Data`:

| Type | Field | Data |
| --- | --- | --- |
| `JSON_NULL`, `JSON_FALSE`, `JSON_TRUE` | 0 | 0 |
| `JSON_INT` | 0 | the value (an integer that fits a LONG) |
| `JSON_NUMBER` | pool offset of its text | its length |
| `JSON_STRING` | pool offset of its UTF-8 | its length in bytes |
| `JSON_ARRAY` | number of elements | index just past its whole subtree |
| `JSON_OBJECT` | number of members | as an array |

The root is node 0. A container's children follow it directly; an
object's are key, value, key, value. `jn_Data` of a container skips the
whole subtree in one step, so walking is a loop, never a recursion:

```c
struct JSONNode *nv = JSON_NODES(doc);
ULONG i, n = 1;                              /* the root's first child */
for (i = 0; i < JSON_NFIELD(&nv[0]); i++) {
    /* nv[n] is an element (or, in an object, the key and nv[n + 1] the value) */
    n = JSON_NSKIP(nv, n);
}
```

or with the calls: `JSONType`, `JSONCount`, `JSONSkip`, `JSONIndex`,
`JSONFind` (an object's member by key: the last of equal keys, as
JavaScript's `JSON.parse` keeps), `JSONPointer` (RFC 6901, `"/a/0/b"`),
`JSONGetString`, `JSONGetLong`, `JSONGetDouble`, `JSONGetLatin1`.

Integers that fit a LONG are stored as values; every other number (fractions,
exponents, big integers, `-0`) keeps its exact text, converted only when you
ask, so nothing is lost and parsing never does floating point.
`JSONParseInto` builds the block in memory you give it (`JSONDocSize` says
how much), for a pool, a stack buffer or shared memory; `JSONFreeDoc` frees
only blocks `JSONParse` allocated.

## Numbers

The library has no floating point code at all, so it needs no FPU and no
maths library: decimal text to IEEE 754 double and back is done with
integers.

- **Text to double** (`JSONGetDouble`, `JSONTextToDouble`) is correctly
  rounded (to nearest, ties to even, subnormals included) for any length of
  text. Up to 19 digits with a non-negative exponent it is 64-bit integer
  work; otherwise exact bignum arithmetic (the digits times or divided by a
  power of ten, by long division), with the first 768 significant digits
  and a sticky digit, which decides every double. Small numbers keep their
  bignums on the stack (112 bytes); long or extreme ones borrow a few
  hundred bytes to a few KB from the heap for the call.
- **Double to text** (`JSONWriteDouble`, `JSONDoubleToText`) gives the
  shortest text that reads back as the same double, laid out as
  ECMAScript's `Number::toString` (so `1e+21`, `1.5e-7`, `0.000001`,
  `123456789`), the same text `JSON.stringify` writes. NaN and the
  infinities write `null`, `-0` writes `0`, as `JSON.stringify` does.
  Integers below 2^53 are printed directly; other values use Steele and
  White's free-format digit generation in bignums (800 bytes, made once
  per writer).
- **Text to LONG** (`JSONGetLong`, `JSONTextToLong`) is exact from the
  decimal digits: `1.5e1` is 15, `1.5` and `2147483648` are
  `JSONERR_RANGE`.

The tests check text to double against Python's correctly rounded
`float()` (3,040 texts: halfway points between doubles written out exactly,
800-digit texts, subnormals, the overflow edge) and double to text against
Node's `String(x)` (3,000 doubles), on the host and on the 68040 under qemu.

## Writing

`JSONNewWriter(flags)` and then `JSONWriteBegin(w, JSON_OBJECT or
JSON_ARRAY)`, `JSONWriteKey`, `JSONWriteString`, `JSONWriteLong`,
`JSONWriteDouble`, `JSONWriteBool`, `JSONWriteNull`, `JSONWriteNumberText`
(a number's exact text, checked), `JSONWriteNode` (a whole subtree of a
block), `JSONWriteEnd`; `JSONWriterText` gives the text once a whole value
is written. A call out of order (a value where a key must be, an end with
nothing open) sets `JSONERR_STATE`, which sticks: check once at the end.
`JSONResetWriter` starts again with the same buffer. `JSONStringify(doc,
node, flags)` is the one-call form; free its text with `JSONFreeText`.

Flags: `JSONW_PRETTY` (two-space indent) or `JSONW_TABS`, `JSONW_ASCII`
(every non-ASCII character as `\uXXXX`, surrogate pairs above U+FFFF),
`JSONW_LATIN1` (the strings you give are ISO-8859-1, as most Amiga text is;
they are written as UTF-8), `JSONW_MULTI` (several top-level values, one per
line: JSON Lines). Strings are escaped as `JSON.stringify` does (`\"`,
`\\`, `\b\f\n\r\t`, other controls `\u00XX`); invalid UTF-8 is
`JSONERR_UTF8`.

## The streaming reader

For text too big to hold, or arriving from a socket: `JSONNewReader`,
then `JSONReaderFeed(r, buf, len)` with each piece (the piece must stay
valid until `JSONReaderNext` answers `JSONEV_MORE`), and a 0-byte feed at
the end. `JSONReaderNext` gives one event at a time: `JSONEV_OBJECT`,
`_OBJECT_END`, `_ARRAY`, `_ARRAY_END`, `_KEY`, `_STRING`, `_NUMBER` (their
unescaped text from `JSONReaderText`), `_TRUE`, `_FALSE`, `_NULL`, then
`_DONE`, or `_MORE` when it needs more text, or `_ERROR`. A token may be
split anywhere between pieces, even inside a `\u` escape or a UTF-8
sequence; the reader keeps only the token at hand, so memory stays small
whatever the text's size. `JSONF_MULTI` reads one value after another
(JSON Lines). The tests feed every case to the reader whole and in random
pieces of 1 to 64 bytes and require exactly the parser's answer, errors and
their positions included.

## Text it accepts

RFC 8259 exactly, by default: UTF-8 checked strictly (RFC 3629: no
overlong forms, no surrogates, nothing past U+10FFFF), no control
characters in strings, numbers by the grammar (no leading zeros, no `+`,
no `.5`, no NaN or Infinity), one value and only whitespace after it. A
UTF-8 byte order mark at the start is skipped. Nesting is limited to 1024
levels (`JSONERR_DEPTH`), and a block to 2^28 - 1 nodes and pool bytes.

- `\u` escapes of an unpaired surrogate become U+FFFD (the text cannot be
  held as UTF-8 otherwise). A pair becomes its one character.
- `\u0000` is kept: the string's length counts it, so `JSONGetString`'s
  length is the truth; as a C string it ends there.
- Duplicate keys are all kept in the block; `JSONFind` and `JSONPointer`
  answer with the last.

Flags for looser text: `JSONF_COMMENTS` (`//` and `/* */` where
whitespace may be), `JSONF_TRAILING` (a comma before `]` or `}`),
`JSONF_LATIN1` (the text is ISO-8859-1; strings come out UTF-8).

Errors give a `struct JSONError`: the code (`JSONErrorString` names it),
the byte offset, and the line and column (1-based, in bytes) of the byte
at fault, or of the end of the text when it ends too soon.

## Tests

`tests/jsonlib/run.sh` builds the core three ways (host C++ with
AddressSanitizer and UBSan; m68k cross G++ for the 68040 at `-O2` and
`-Os`), checks with nm that the 68040 core calls nothing but libgcc's
arithmetic and holds no C++ runtime and no data, and runs, on the host and
under `qemu-m68k -cpu m68040`:

- 101 hand-written cases with their exact answers (error code and offset);
- 600 random documents (every escape, control characters, the BMP and
  astral planes, deep nesting, every number form, random whitespace) whose
  compact form is known as they are made; each is also written pretty,
  tabbed and ASCII-only and read back to the same document;
- 1,500 random damagings of those, which must be accepted or refused as
  Python's strict `json` module does, with the same value when accepted;
- every case through the streaming reader, whole and in random pieces;
- every block checked by `JSONCheckDoc`, copied and walked; 24 random
  damagings of each, which `JSONCheckDoc` refuses or which walk safely;
- the numbers above;
- `JSONTest`'s self-test over the core (`JSON_HOSTED`).

With `OPENSDK_SRC` (a gfopensdk checkout) it also checks the SDK
headers against the SFD and compiles the shell and JSONTest against
GFOpenNDK. The library itself, on an Amiga: `JSONTest` (the self-test, every
call through its registers) and `JSONTest FILE BENCH`.

## Later

- The JavaScript engine may one day wrap a block lazily (JSON.parse
  without making every object up front). Since WebKit patch 0058,
  JavaScriptCore's `JSON.parse` on the 68k parses through this library
  (`JSONParse`, then one loop over the nodes), and JavaScriptCore's own
  JSON parser is no longer built there.
- A tags-taking parse call if options outgrow the flags; new calls go at
  the end of the SFD.

Copyright (c) 2026 AmiCradle Team. MIT.
