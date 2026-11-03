#!/bin/sh
# GFOpenBrowser (DalsinAI/gfopenbrowser)
# Copyright (c) 2026 AmiCradle Team. MIT licence, see LICENSE.
#
# unicode.library 1 (src/unicodelib): ICU 78's code and data, one shared
# copy for every program, for AmigaOS 3.2 on a 68040 (hard float, as ICU is
# built for the engine). PLAN.md step 7. Lives here for now; it moves to
# its own repository, openamigaunicode, later.
#
#   build-unicode-library.sh [OUT_DIR]     (default build/unicode-library)
#
#   OUT_DIR/unicode.library        for LIBS:
#   OUT_DIR/unicode.library.map    the linker's map
#   OUT_DIR/Unicode/icudt78b.dat   ICU's data, for LOCALE:Unicode/, LIBS:Unicode/
#                                  or a program's own Unicode/ drawer
#   OUT_DIR/tests/UnicodeTest      the self-test (tests/UnicodeTest.c)
#   OUT_DIR/tests/UnicodeProps     checksums of the property and case calls
#                                  (tests/UnicodeProps.c), to compare with ICU's
#   OUT_DIR/icu/                   for building programs on the library:
#                                  ICU's headers and ul_client.h; lib/libicuuc.a
#                                  sends each of ICU's calls to the library,
#                                  lib/libicui18n.a and lib/libicudata.a are
#                                  empty. scripts/build-webcore.sh takes it as
#                                  OB_ICU_DIR (with OB_ICU_DATA=library), and
#                                  build-jsc.sh's JavaScriptEngine through the
#                                  same toolchain file.
#
#   OAB_DEPS        holds icu78-m68k-amigaos from scripts/build-icu.sh: the
#                   library links its static libraries and ships its data
#   TOOLCHAIN_DIR   the GFOpenAmigaGCC release, or STOVE (os32-gcc16)
#   NDK_INCLUDE     GFOpenNDK's Include_H
#   UCD_DIR         the Unicode Character Database 17.0.0 our own code is made
#                   from (scripts/fetch-ucd.sh), by default $OAB_DEPS/ucd-17.0.0
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
OUT=${1:-"$HERE/build/unicode-library"}
if [ -n "${TOOLCHAIN_DIR:-}" ]; then
    BIN="$TOOLCHAIN_DIR/bin"
else
    BIN="${STOVE:-"$HOME/AmiCradle/stoves/os32-gcc16"}/prefix/bin"
fi
CC="$BIN/m68k-amigaos-gcc"
CXX="$BIN/m68k-amigaos-g++"
AR="$BIN/m68k-amigaos-ar"
NM="$BIN/m68k-amigaos-nm"
STRIP="$BIN/m68k-amigaos-strip"
ICU="${OAB_DEPS:?set OAB_DEPS to the folder with icu78-m68k-amigaos}/icu78-m68k-amigaos"
LIB="$HERE/src/unicodelib"
: "${NDK_INCLUDE:?set NDK_INCLUDE to GFOpenNDK's Include_H}"
[ -x "$CC" ] || { echo "build-unicode-library.sh: no compiler at $CC (set TOOLCHAIN_DIR or STOVE)" >&2; exit 2; }
for f in lib/libicuuc.a lib/libicui18n.a include/unicode/utypes.h share/icudt78b.dat; do
    [ -f "$ICU/$f" ] || { echo "build-unicode-library.sh: no $ICU/$f (scripts/build-icu.sh)" >&2; exit 2; }
done
UCD=${UCD_DIR:-"$OAB_DEPS/ucd-17.0.0"}
# in GitHub's CI the UCD is fetched here, pinned (elsewhere: scripts/fetch-ucd.sh first)
if [ ! -f "$UCD/UnicodeData.txt" ] && [ "${GITHUB_ACTIONS:-}" = true ]; then
    sh "$HERE/scripts/fetch-ucd.sh" "$UCD"
fi
[ -f "$UCD/UnicodeData.txt" ] ||
    { echo "build-unicode-library.sh: no Unicode 17.0.0 UCD in $UCD (scripts/fetch-ucd.sh $UCD, or set UCD_DIR)" >&2; exit 2; }

rm -rf "$OUT"
mkdir -p "$OUT/obj" "$OUT/gen" "$OUT/tests" "$OUT/Unicode" "$OUT/icu/lib"
python3 "$LIB/gen_calls.py" "$LIB/calls.txt" "$OUT/gen" "$LIB/own.txt"
# our own code's tables, from the UCD (src/unicodelib/own)
python3 "$LIB/own/gen_props.py" "$UCD" "$ICU/include" "$OUT/gen"
python3 "$LIB/own/gen_brk.py" "$UCD" "$OUT/gen"

# As build-icu.sh builds ICU for the engine
CPU="-m68040 -m68881"
WARN="-Wall -Wextra -Wno-unused-parameter -Werror"
INC="-I$OUT/gen -I$LIB -I$LIB/sdk/Include_H -I$ICU/include -I$NDK_INCLUDE"

# ---- the library
"$CC" $CPU -c "$LIB/ul_start.s" -o "$OUT/obj/ul_start.o"
"$CC" $CPU -O2 -std=gnu99 -fno-delete-null-pointer-checks -fno-common $WARN $INC \
    -c "$LIB/unicode_lib.c" -o "$OUT/obj/unicode_lib.o"
"$CC" $CPU -O2 -std=gnu99 -fno-delete-null-pointer-checks -fno-common -fno-builtin $WARN $INC \
    -c "$LIB/ul_runtime.c" -o "$OUT/obj/ul_runtime.o"
"$CC" $CPU -O2 -c "$OUT/gen/ul_table.c" -o "$OUT/obj/ul_table.o"
OWN=
for f in uo_props uo_case uo_brk uo_utext uo_icu; do
    "$CC" $CPU -O2 -std=gnu99 -fno-delete-null-pointer-checks -fno-common $WARN $INC -I"$LIB/own" \
        -c "$LIB/own/$f.c" -o "$OUT/obj/$f.o"
    OWN="$OWN $OUT/obj/$f.o"
done
for f in uo_data uo_brk_data; do
    "$CC" $CPU -O2 -std=gnu99 -fno-common $WARN $INC -c "$OUT/gen/$f.c" -o "$OUT/obj/$f.o"
    OWN="$OWN $OUT/obj/$f.o"
done
# ul_start.o first: its code is the file's first. ICU's archives, then the
# pure parts of libstdc++, libnix and libm; ul_runtime.o's own versions of
# the rest come first and win. The duplicate-section notes are ICU's inline
# functions, compiled in several files.
"$CXX" $CPU -nostartfiles -o "$OUT/unicode.library" \
    "$OUT/obj/ul_start.o" "$OUT/obj/unicode_lib.o" "$OUT/obj/ul_runtime.o" "$OUT/obj/ul_table.o" $OWN \
    "$ICU/lib/libicui18n.a" "$ICU/lib/libicuuc.a" -lstdc++ -lm -lgcc \
    -Wl,-Map,"$OUT/unicode.library.map" 2>"$OUT/obj/link.log" ||
    { grep -v 'duplicate section\|has different contents' "$OUT/obj/link.log" >&2; exit 1; }
# ICU's files own/uo_icu.c stands in for must not be in the link
for m in ubidi_props; do
    if grep -q "libicuuc\.a($m\." "$OUT/unicode.library.map"; then
        echo "ICU's $m is linked: own/uo_icu.c misses one of its names" >&2
        exit 1
    fi
done

# Checks: nothing undefined; none of libnix's per-program start-up or stdio;
# the only start-up lists left are those ul_runtime_init stands in for.
und=$("$NM" -u "$OUT/unicode.library" || true)
[ -z "$und" ] || { echo "build-unicode-library.sh: undefined in unicode.library: $und" >&2; exit 1; }
if grep -E 'lib(nix|nix20)\.a\((__initstdio|environ|malloc|calloc|realloc|setlocale|abort|atexit|getenv|fopen|open|read|write|__chkabort)\.o\)' \
        "$OUT/unicode.library.map"; then
    echo "build-unicode-library.sh: unicode.library links libnix's per-program code" >&2; exit 1
fi
lists=$(awk '/^ \.list___(INIT|EXIT)_LIST__/ { getline; print $NF }' "$OUT/unicode.library.map" | sed 's#.*/##' | LC_ALL=C sort -u | tr '\n' ' ')
case "$lists" in
    "libnix.a(cxxglue.o) libnix20.a(__gmtoffset.o) ") ;;
    *) echo "build-unicode-library.sh: start-up or exit code nobody runs: $lists" >&2; exit 1 ;;
esac
"$STRIP" -o "$OUT/unicode.library.stripped" "$OUT/unicode.library"
mv "$OUT/unicode.library.stripped" "$OUT/unicode.library"
strings "$OUT/unicode.library" | grep -q '^\$VER: unicode.library 1\.' ||
    { echo "build-unicode-library.sh: no version string" >&2; exit 1; }
cp "$ICU/share/icudt78b.dat" "$OUT/Unicode/icudt78b.dat"

# ---- for programs: ICU's headers and the stubs
cp -R "$ICU/include" "$OUT/icu/include"
cp "$LIB/ul_client.h" "$OUT/icu/include/"
"$CC" $CPU -c "$OUT/gen/ul_stubs.s" -o "$OUT/obj/ul_stubs.o"
"$CC" $CPU -O2 -std=gnu99 -fno-delete-null-pointer-checks -fno-common $WARN $INC \
    -c "$LIB/ul_client.c" -o "$OUT/obj/ul_client.o"
"$AR" rcs "$OUT/icu/lib/libicuuc.a" "$OUT/obj/ul_stubs.o" "$OUT/obj/ul_client.o"
"$AR" rcs "$OUT/icu/lib/libicui18n.a"
"$AR" rcs "$OUT/icu/lib/libicudata.a"
# what FindICU and pkg-config look at
cp -R "$ICU/lib/pkgconfig" "$OUT/icu/lib/" 2>/dev/null || true

# ---- the self-test, built as a program on the library is
"$CC" $CPU -O2 -std=gnu99 -fno-delete-null-pointer-checks -fno-common -Wall -Wextra -Wno-unused-parameter \
    -Wno-pointer-sign -Wno-format -I"$OUT/icu/include" -I"$NDK_INCLUDE" \
    "$LIB/tests/UnicodeTest.c" -L"$OUT/icu/lib" -licuuc -lm -o "$OUT/tests/UnicodeTest"

"$CC" $CPU -O2 -std=gnu99 -fno-delete-null-pointer-checks -fno-common -Wall -Wextra -Wno-unused-parameter \
    -I"$OUT/icu/include" -I"$NDK_INCLUDE" \
    "$LIB/tests/UnicodeProps.c" -L"$OUT/icu/lib" -licuuc -lm -o "$OUT/tests/UnicodeProps"

for f in unicode.library Unicode/icudt78b.dat tests/UnicodeTest tests/UnicodeProps icu/lib/libicuuc.a; do
    printf '%s %s bytes\n' "$OUT/$f" "$(wc -c < "$OUT/$f")"
done
