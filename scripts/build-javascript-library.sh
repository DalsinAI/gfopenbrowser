#!/bin/sh
# GFOpenBrowser (DalsinAI/openamigabrowser)
# Copyright (c) 2026 AmigaChrome Team. MIT licence, see LICENSE.
#
# javascript.library 1 (src/jslib) and the JS command, for AmigaOS 3.2 on a
# 68040. The library is C with no C library, built for size (-Os); it holds
# no engine. The engine, JavaScriptEngine, is built with JavaScriptCore by
# scripts/build-javascript-engine.sh. JS is soft float
# (toolchain/m68k-soft-m68040.specs). Lives here for now; it moves to its
# own repository later.
#
#   build-javascript-library.sh [OUT_DIR]     (default build/javascript-library)
#
#   OUT_DIR/javascript.library       for LIBS:
#   OUT_DIR/javascript.library.map   the linker's map
#   OUT_DIR/JS                       the Shell command (C:)
#
#   TOOLCHAIN_DIR, STOVE and NDK_INCLUDE as for build-openbrowser.sh.
#   OPENSDK_SRC     an openamigasdk checkout: when set, src/jslib/sdk is
#                   checked against the SFD with its OpenNDK/tools/ndkgen.py
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
OUT=${1:-"$HERE/build/javascript-library"}
if [ -n "${TOOLCHAIN_DIR:-}" ]; then
    BIN="$TOOLCHAIN_DIR/bin"
else
    BIN="${STOVE:-"$HOME/AmiCradle/stoves/os32-gcc16"}/prefix/bin"
fi
CC="$BIN/m68k-amigaos-gcc"
NM="$BIN/m68k-amigaos-nm"
LIB="$HERE/src/jslib"
[ -x "$CC" ] || { echo "build-javascript-library.sh: no compiler at $CC (set TOOLCHAIN_DIR or STOVE)" >&2; exit 2; }

python3 "$LIB/check_vectors.py"
if [ -n "${OPENSDK_SRC:-}" ]; then
    python3 "$OPENSDK_SRC/OpenNDK/tools/ndkgen.py" --check --out "$LIB/sdk" "$LIB/sfd/javascript_lib.sfd" ||
        { echo "build-javascript-library.sh: src/jslib/sdk is stale: run scripts/gen-jslib-sdk.sh" >&2; exit 1; }
fi

INC="-I$LIB -I$LIB/sdk/Include_H"
[ -n "${NDK_INCLUDE:-}" ] && INC="$INC -I$NDK_INCLUDE"
mkdir -p "$OUT"

# The library: javascript_lib.c alone (its start() is the first code)
"$CC" -m68040 -Os -std=gnu99 -fno-delete-null-pointer-checks -fno-common -fomit-frame-pointer \
    -fno-toplevel-reorder -fno-builtin -fno-tree-loop-distribute-patterns \
    -Wall -Wextra -Werror -Wno-unused-parameter -nostartfiles -nostdlib $INC \
    "$LIB/javascript_lib.c" -lgcc -Wl,-Map,"$OUT/javascript.library.map" -o "$OUT/javascript.library"
und=$("$NM" -u "$OUT/javascript.library" || true)
[ -z "$und" ] || { echo "build-javascript-library.sh: undefined in javascript.library: $und" >&2; exit 1; }
echo "$OUT/javascript.library ($(wc -c < "$OUT/javascript.library") bytes)"

# JS: an ordinary program that opens the library from LIBS:
"$CC" -m68040 -specs="$HERE/toolchain/m68k-soft-m68040.specs" -mcrt=nix20 -std=gnu99 -Os \
    -fno-delete-null-pointer-checks -fno-common -Wall -Wextra -Werror -Wno-pointer-sign $INC \
    "$LIB/JS.c" -o "$OUT/JS"
echo "$OUT/JS ($(wc -c < "$OUT/JS") bytes)"
