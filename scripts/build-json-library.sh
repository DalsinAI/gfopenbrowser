#!/bin/sh
# GFOpenBrowser (DalsinAI/gfopenbrowser)
# Copyright (c) 2026 AmiCradle Team. MIT licence, see LICENSE.
#
# json.library 1 (src/jsonlib, GFOpenJSON) and its Amiga test program, for
# AmigaOS 3.2 on a 68040. Soft float (toolchain/m68k-soft-m68040.specs) for
# JSONTest; the library itself has no floating point at all and links with
# no C library. Built for size (-Os). The core is lean C++
# (DalsinAI/gfopengcc docs/lean-cxx.md, the library profile), the shell
# C. Lives here for now; it moves to its own repository later.
#
#   build-json-library.sh [OUT_DIR]     (default build/json-library)
#
#   OUT_DIR/json.library         for LIBS:
#   OUT_DIR/json.library.map     the linker's map
#   OUT_DIR/tests/JSONTest       the self-test; JSONTest FILE [BENCH] parses a file
#
#   TOOLCHAIN_DIR, STOVE and NDK_INCLUDE as for build-openbrowser.sh.
#   OPENSDK_SRC     a gfopensdk checkout: when set, src/jsonlib/sdk is
#                   checked against the SFD with its OpenNDK/tools/ndkgen.py
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
OUT=${1:-"$HERE/build/json-library"}
if [ -n "${TOOLCHAIN_DIR:-}" ]; then
    BIN="$TOOLCHAIN_DIR/bin"
else
    BIN="${STOVE:-"$HOME/AmiCradle/stoves/os32-gcc16"}/prefix/bin"
fi
CC="$BIN/m68k-amigaos-gcc"
CXX="$BIN/m68k-amigaos-g++"
NM="$BIN/m68k-amigaos-nm"
LIB="$HERE/src/jsonlib"
[ -x "$CC" ] || { echo "build-json-library.sh: no compiler at $CC (set TOOLCHAIN_DIR or STOVE)" >&2; exit 2; }

python3 "$LIB/check_vectors.py"
if [ -n "${OPENSDK_SRC:-}" ]; then
    python3 "$OPENSDK_SRC/OpenNDK/tools/ndkgen.py" --check --out "$LIB/sdk" "$LIB/sfd/json_lib.sfd" ||
        { echo "build-json-library.sh: src/jsonlib/sdk is stale: run scripts/gen-jsonlib-sdk.sh" >&2; exit 1; }
fi

SOFT="-m68040 -specs=$HERE/toolchain/m68k-soft-m68040.specs"
INC="-I$LIB -I$LIB/sdk/Include_H"
[ -n "${NDK_INCLUDE:-}" ] && INC="$INC -I$NDK_INCLUDE"
mkdir -p "$OUT/obj" "$OUT/tests"

# The core: no C++ runtime, no data of its own, nothing called but libgcc
"$CXX" -m68040 -Os -std=gnu++23 -fno-exceptions -fno-rtti -fno-threadsafe-statics \
    -fno-asynchronous-unwind-tables -fno-unwind-tables -nostdlib++ \
    -fno-delete-null-pointer-checks -fomit-frame-pointer -fno-lto -fno-builtin \
    -fno-tree-loop-distribute-patterns -Wall -Wextra -Werror \
    -c "$LIB/json_core.cpp" -o "$OUT/obj/json_core.o"
bad=$("$NM" "$OUT/obj/json_core.o" | awk '{print $NF}' |
      grep -E '^_?(__cxa_|__gxx_personality|_ZTI|_ZTS|_ZTV|_GLOBAL__sub_I_|_ZGV|_Znw|_Zna|_ZdlPv|_ZdaPv)' || true)
[ -z "$bad" ] || { echo "build-json-library.sh: C++ runtime in the core: $bad" >&2; exit 1; }
und=$("$NM" -u "$OUT/obj/json_core.o" | awk '{print $NF}' |
      grep -v '^_*\(mul\|div\|udiv\|mod\|umod\|ashl\|ashr\|lshr\)[sd]i3$' || true)
[ -z "$und" ] || { echo "build-json-library.sh: the core calls $und" >&2; exit 1; }

# The library: openjson_lib.c first (its start() is the first code)
"$CC" -m68040 -Os -std=gnu99 -fno-delete-null-pointer-checks -fno-common -fomit-frame-pointer \
    -fno-toplevel-reorder -fno-builtin -fno-tree-loop-distribute-patterns \
    -Wall -Wextra -Werror -Wno-unused-parameter -nostartfiles -nostdlib $INC \
    "$LIB/openjson_lib.c" "$OUT/obj/json_core.o" \
    -lgcc -Wl,-Map,"$OUT/json.library.map" -o "$OUT/json.library"
und=$("$NM" -u "$OUT/json.library" || true)
[ -z "$und" ] || { echo "build-json-library.sh: undefined in json.library: $und" >&2; exit 1; }
echo "$OUT/json.library ($(wc -c < "$OUT/json.library") bytes)"

# JSONTest: an ordinary program that opens the library from LIBS:
"$CC" $SOFT -mcrt=nix20 -std=gnu99 -Os -fno-delete-null-pointer-checks -fno-common \
    -Wall -Wextra -Werror -Wno-pointer-sign -Wno-format $INC \
    "$LIB/tests/JSONTest.c" -o "$OUT/tests/JSONTest"
echo "$OUT/tests/JSONTest ($(wc -c < "$OUT/tests/JSONTest") bytes)"
