/*
 * The few Exec types weblayout's calls use, for its host tests (the
 * engine's own code is built on the host against these and
 * tests/host/graphics, tests/host/proto: see tests/test_wl_document.c).
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#ifndef EXEC_TYPES_H
#define EXEC_TYPES_H
typedef void *APTR;
typedef long LONG;
typedef unsigned long ULONG;
typedef short WORD;
typedef unsigned short UWORD;
typedef signed char BYTE;
typedef unsigned char UBYTE;
typedef short BOOL;
typedef unsigned char *STRPTR;
typedef const unsigned char *CONST_STRPTR;
typedef unsigned char *PLANEPTR;
#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif
#ifndef NULL
#define NULL ((void *)0)
#endif
#endif
