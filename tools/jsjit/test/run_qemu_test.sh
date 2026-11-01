#!/bin/sh
# GFOpenBrowser (DalsinAI/openamigabrowser)
# Copyright (c) 2026 AmiCradle Team. MIT licence, see LICENSE.
#
# Builds op_mov's template from a generated 68k interpreter and runs the
# stitcher test under qemu-m68k (68040). Needs binutils/g++ for
# m68k-linux-gnu and qemu-user (apt: binutils-m68k-linux-gnu
# g++-m68k-linux-gnu qemu-user).
#
# usage: run_qemu_test.sh path/to/LLIntAssembly.h [workdir]
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
LLINT=${1:?LLIntAssembly.h from a 68k build (offlineasm-py, M68K)}
WORK=${2:-"$PWD/jsjit-test"}
mkdir -p "$WORK"
python3 "$HERE/../m68k_templates.py" --only op_mov "$LLINT" "$WORK/LLIntTemplates.h"
cat > "$WORK/templates.cpp" <<'CPP'
#define SYMBOL_STRING(name) "_" #name
#define LABEL_REFERENCE(name) SYMBOL_STRING(name)
#define LOCAL_LABEL_STRING(name) "L" #name
__asm__(
#include "LLIntTemplates.h"
);
CPP
m68k-linux-gnu-g++ -m68040 -O1 -Wa,--register-prefix-optional -c "$WORK/templates.cpp" -I"$WORK" -o "$WORK/templates.o"
m68k-linux-gnu-gcc -m68040 -O1 -static "$HERE/qemu_stitch.c" "$WORK/templates.o" -o "$WORK/qemu_stitch"
qemu-m68k -cpu m68040 "$WORK/qemu_stitch"
