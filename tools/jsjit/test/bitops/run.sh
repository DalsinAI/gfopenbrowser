#!/bin/sh
# Patch 0101's bit operation code under qemu-m68k -cpu m68040: the JIT's own
# Builder and FastPaths methods, cut from a patched M68KTemplateJIT.cpp,
# against JavaScript's ToInt32, with FPCR set as C, mathieeedoubbas.library
# (round to zero) and a double-precision task leave it.
#   run.sh path/to/WebKit/Source/JavaScriptCore/llint/M68KTemplateJIT.cpp
# Needs m68k-linux-gnu-g++ (14 or later) and qemu-m68k.
# MIT, Copyright (c) 2026 The AmiCradle Team.
set -eu
here=$(cd "$(dirname "$0")" && pwd)
jit=${1:?usage: run.sh path/to/M68KTemplateJIT.cpp}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
python3 "$here/extract.py" "$jit" "$work/extracted.inc"
cxx=$(command -v m68k-linux-gnu-g++ || command -v m68k-linux-gnu-g++-14)
"$cxx" -std=c++20 -O1 -m68040 -static -I"$work" "$here/harness.cpp" -o "$work/harness"
for fpcr in 0 0x10 0x30 0x80; do
    FPCR=$fpcr qemu-m68k -cpu m68040 "$work/harness" | tail -1
done
