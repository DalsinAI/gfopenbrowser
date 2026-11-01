#ifndef LIBRARIES_UNICODE_H
#define LIBRARIES_UNICODE_H
/*
**  unicode.library 1: one shared copy of Unicode (ICU 78) for every program.
**  MIT, Copyright (c) 2026 AmiCradle Team.
**
**  UnicodeTable(count) returns a table of pointers to ICU 78's C
**  functions, in the order of src/unicodelib/calls.txt (append only), for
**  a program that knows the first `count' of them; NULL when the library
**  has fewer, or cannot read ICU's data file. The functions are called as
**  ordinary C functions (arguments on the stack), with ICU's own headers.
**  Most programs link the stubs instead (ul_client.h), which call through
**  the table under ICU's own names.
*/

#ifndef EXEC_TYPES_H
#include <exec/types.h>
#endif

#define UNICODENAME "unicode.library"

/* ICU's data, looked for in LOCALE:Unicode/, LIBS:Unicode/ and the first
 * caller's PROGDIR:Unicode/ */
#define UNICODE_DATA_FILE "icudt78b.dat"

#endif /* LIBRARIES_UNICODE_H */
