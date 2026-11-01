#!/bin/sh
# GFOpenBrowser (DalsinAI/openamigabrowser)
# Copyright (c) 2026 AmiCradle Team. MIT licence, see LICENSE.
#
# weblayout.library 1 (src/weblayout as a resident library) and its Amiga
# test program, for AmigaOS 3.2 on a 68040 with its FPU. The library uses
# the resident gfopenlayout.library 2 (DalsinAI/openamigalayout) and links
# with no C library: an Exec pool per document and its own string calls.
#
#   build-weblayout-library.sh [OUT_DIR]     (default build/weblayout-library)
#
#   OUT_DIR/weblayout.library        for LIBS:
#   OUT_DIR/weblayout.library.map    the linker's map
#   OUT_DIR/tests/WebLayoutLibTest   opens LIBS:weblayout.library and runs
#                                    its calls on a page
#
#   TOOLCHAIN_DIR, STOVE, NDK_INCLUDE and OPENLAYOUT_SRC as for
#   build-openbrowser.sh.
#   OPENSDK_SRC     an openamigasdk checkout: when set, the SDK headers in
#                   src/weblayout/library/sdk are checked against
#                   weblayout_lib.sfd with its OpenNDK/tools/ndkgen.py
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
OUT=${1:-"$HERE/build/weblayout-library"}
if [ -n "${TOOLCHAIN_DIR:-}" ]; then
    BIN="$TOOLCHAIN_DIR/bin"
else
    BIN="${STOVE:-"$HOME/AmiCradle/stoves/os32-gcc16"}/prefix/bin"
fi
CC="$BIN/m68k-amigaos-gcc"
OL=${OPENLAYOUT_SRC:-"$HERE/../openamigalayout"}
WL="$HERE/src/weblayout"
LIB="$WL/library"
[ -x "$CC" ] || { echo "build-weblayout-library.sh: no compiler at $CC (set TOOLCHAIN_DIR or STOVE)" >&2; exit 2; }
[ -f "$OL/include/libraries/openlayout.h" ] || { echo "build-weblayout-library.sh: no GFOpenLayout at $OL (set OPENLAYOUT_SRC)" >&2; exit 2; }

if [ -n "${OPENSDK_SRC:-}" ]; then
    # the headers are made from the SFD; a stale one fails the build
    python3 "$OPENSDK_SRC/OpenNDK/tools/ndkgen.py" --check --out "$LIB/sdk" "$LIB/weblayout_lib.sfd" ||
        { echo "build-weblayout-library.sh: src/weblayout/library/sdk is stale: run scripts/gen-weblayout-sdk.sh" >&2; exit 1; }
fi

CPU="-m68040 -m68881"
INC="-I$WL -I$OL/include -I$LIB/sdk/Include_H"
[ -n "${NDK_INCLUDE:-}" ] && INC="$INC -I$NDK_INCLUDE"
mkdir -p "$OUT/tests"

# The library: wl_library.c first (its start() is the first code), the
# engine with an Exec pool per document (WL_EXEC_POOLS) and the resident
# GFOpenLayout, and the library's own string calls. -nostdlib: nothing of a C
# library; -fno-tree-loop-distribute-patterns: no memset/memcpy calls made
# out of the runtime's own loops.
"$CC" $CPU -mcrt=nix20 -O2 -std=gnu99 -fno-delete-null-pointer-checks -fno-common \
    -fomit-frame-pointer -fno-toplevel-reorder -fno-builtin -fno-tree-loop-distribute-patterns \
    -Wall -Wextra -Werror -nostartfiles -nostdlib \
    -DWL_LIBRARY -DWL_EXEC_POOLS -DWL_USE_OPENLAYOUT_LIBRARY $INC \
    "$LIB/wl_library.c" \
    "$WL/wl_document.c" "$WL/wl_html.c" "$WL/wl_css.c" "$WL/wl_paint.c" "$WL/wl_image_dt.c" \
    "$WL/wl_url.c" "$WL/wl_openlayout.c" "$WL/wl_text.c" "$WL/wl_mem.c" "$WL/wl_libc.c" \
    "$LIB/wl_runtime.c" \
    -lamiga -lgcc -Wl,-Map,"$OUT/weblayout.library.map" -o "$OUT/weblayout.library"
echo "$OUT/weblayout.library ($(wc -c < "$OUT/weblayout.library") bytes)"

# WebLayoutLibTest: an ordinary program that opens the library from LIBS:
"$CC" $CPU -mcrt=nix20 -std=gnu99 -O2 -fno-delete-null-pointer-checks -fno-common \
    -Wall -Wextra -Werror $INC \
    "$HERE/tests/test_wl_library.c" -o "$OUT/tests/WebLayoutLibTest"
echo "$OUT/tests/WebLayoutLibTest ($(wc -c < "$OUT/tests/WebLayoutLibTest") bytes)"
