#!/bin/sh
# GFOpenBrowser (DalsinAI/gfopenbrowser)
# Copyright (c) 2026 AmiCradle Team. MIT licence, see LICENSE.
#
# GFOpenBrowser Lite, the GadTools browser, for AmigaOS 3.2 on a 68040 with
# its FPU (-m68040 -m68881; not an LC or EC040). Its page engine is
# weblayout.library 1 (src/weblayout, scripts/build-weblayout-library.sh),
# which the program opens from LIBS: or its drawer's Libs and which opens
# gfopenlayout.library (DalsinAI/gfopenlayout): neither is built into the
# program. TLS is GFOpenTLS, whose API headers are in third_party/opentls
# (no AmiSSL).
#
#   build-openbrowser.sh [OUT_DIR]          (default build/os3)
#
#   TOOLCHAIN_DIR   an unpacked GFOpenAmigaGCC release (gfopenamigagcc-16.2.0-open6-
#                   x86_64-linux), or
#   STOVE           a stove root holding prefix/bin/m68k-amigaos-gcc
#                   (default ~/AmiCradle/stoves/os32-gcc16)
#   NDK_INCLUDE     GFOpenNDK's Include_H (gfopensdk OpenNDK/Include_H); not
#                   needed with a stove that carries the NDK
#   OPENLAYOUT_SRC  a gfopenlayout checkout, for its headers (default
#                   ../gfopenlayout)
#   TESTS           1: the Amiga test programs as well (OUT_DIR/tests)
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
if [ $# -ge 1 ] && [ "$1" = "-" ]; then shift; fi     # the old AmiSSL argument's place
OUT=${1:-"$HERE/build/os3"}
if [ -n "${TOOLCHAIN_DIR:-}" ]; then
    BIN="$TOOLCHAIN_DIR/bin"
else
    BIN="${STOVE:-"$HOME/AmiCradle/stoves/os32-gcc16"}/prefix/bin"
fi
CC="$BIN/m68k-amigaos-gcc"
OL=${OPENLAYOUT_SRC:-"$HERE/../gfopenlayout"}
OM="$HERE/third_party/openmail"
OT="$HERE/third_party/opentls/include"
WL="$HERE/src/weblayout"
[ -x "$CC" ] || { echo "build-openbrowser.sh: no compiler at $CC (set TOOLCHAIN_DIR or STOVE)" >&2; exit 2; }
[ -f "$OL/include/gfopenlayout.h" ] || { echo "build-openbrowser.sh: no GFOpenLayout at $OL (set OPENLAYOUT_SRC)" >&2; exit 2; }

# 68040 with its FPU, libnix; address 0 is real memory on the Amiga
CPU="-m68040 -m68881 -mcrt=nix20"
CFLAGS="$CPU -std=gnu99 -O2 -fno-delete-null-pointer-checks -fno-common -Wall -Wextra -Werror"
INC="-I$HERE/src -I$WL -I$WL/library/sdk/Include_H -I$OM -I$OT -I$OL/include"
[ -n "${NDK_INCLUDE:-}" ] && INC="$INC -I$NDK_INCLUDE"
# the engine's allocator and its own C library calls
RT="$WL/wl_mem.c $WL/wl_libc.c"
mkdir -p "$OUT"

# GFOpenBrowserLite: the window (src/ob_main.c), HTTP (src/ob_http.c) and
# GFOpenMail's network layer. weblayout and GFOpenLayout are libraries; the
# program keeps only the address resolver its HTTP layer follows redirects
# with.
"$CC" $CFLAGS $INC \
    "$HERE"/src/ob_main.c "$HERE"/src/ob_http.c "$WL/wl_url.c" $RT "$OM"/*.c \
    -lamiga -o "$OUT/GFOpenBrowserLite"
echo "$OUT/GFOpenBrowserLite ($(wc -c < "$OUT/GFOpenBrowserLite") bytes)"

# obfetch: the same HTTP/HTTPS engine from a Shell, without the window or
# the layout, so network faults can be told from layout faults
"$CC" $CFLAGS $INC \
    "$HERE/src/ob_http.c" "$WL/wl_url.c" $RT "$OM"/*.c "$HERE/tools/obfetch.c" \
    -o "$OUT/obfetch"
echo "$OUT/obfetch ($(wc -c < "$OUT/obfetch") bytes)"

if [ "${TESTS:-0}" = 1 ]; then
    T="$OUT/tests"
    mkdir -p "$T"
    # a picture through picture.datatype: DTImageTest <picture>
    "$CC" $CFLAGS $INC "$WL/wl_image_dt.c" "$HERE/tests/test_wl_image_dt.c" \
        -lamiga -o "$T/DTImageTest"
    # a picture over HTTPS, then through picture.datatype: ImageFetchTest [address]
    "$CC" $CFLAGS $INC "$HERE/src/ob_http.c" "$WL/wl_url.c" $RT "$WL/wl_image_dt.c" "$OM"/*.c \
        "$HERE/tests/test_image_fetch.c" -lamiga -o "$T/ImageFetchTest"
    # the engine's malloc (the stove's os32compat_malloc.c) against libnix's,
    # linked with malloc and free wrapped as the engine is: MallocBench [pairs] [live]
    "$CC" $CFLAGS $INC "$HERE/tools/mallocbench.c" "$HERE/stove/compat/os32compat_malloc.c" \
        -Wl,--wrap=malloc -Wl,--wrap=calloc -Wl,--wrap=realloc -Wl,--wrap=free \
        -Wl,-u,___wrap_malloc -Wl,-u,___wrap_free -Wl,-u,___wrap_calloc -Wl,-u,___wrap_realloc \
        -o "$T/MallocBench"
    for f in "$T"/*; do echo "$f ($(wc -c < "$f") bytes)"; done
fi
