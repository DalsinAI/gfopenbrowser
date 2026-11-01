| unicode.library: the first code in the file. Run as a program, the
| library does nothing (scripts/build-unicode-library.sh links this first).
| Copyright (c) 2026 AmiCradle Team. MIT.
	.text
	.globl	_start
_start:
	moveq	#-1,d0
	rts
