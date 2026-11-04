#!/bin/sh
# unicode.library, our own code: the host checks against ICU 78.
# Copyright (c) 2026 AmiCradle Team. MIT.
#
#   run-host.sh UCD_DIR ICU_SRC ICU_HOST_LIB [OUT]
#
# UCD_DIR       Unicode 17.0.0 (scripts/fetch-ucd.sh)
# ICU_SRC       ICU 78.3's source folder (with common/, i18n/ and
#               test/testdata/, which has Unicode's break tests)
# ICU_HOST_LIB  the host build's lib/ (libicuuc, libicudata)
# ICU_DATA      (environment) where ICU finds its data, when the host build
#               keeps it in a .dat file (--with-data-packaging=archive)
# ICU_TRIMMED   (environment) a folder with the library's own trimmed data
#               (icu/data-keep.lst) as icudt78l.dat: ICU breaks text on it
#               for the comparison, as unicode.library's ICU did
#
# 1. host_props: our calls against ICU's, every code point (exits 1 on
#    any difference).
# 2. host_brk: our text breaking against Unicode's break tests, then
#    against ICU's iterators and u_strToTitle on random strings (exits 1 on
#    any difference).
# 3. UnicodeProps (../tests/UnicodeProps.c) built against ICU itself: its
#    lines, OUT/UnicodeProps.expected, are what the Amiga build of the same
#    program, on the library, must print.
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
UCD=$1; SRC=$2; LIB=$3; OUT=${4:-"$HERE/../../../build/unicode-own-host"}
mkdir -p "$OUT"
python3 "$HERE/gen_props.py" "$UCD" "$SRC/common" "$OUT"
python3 "$HERE/gen_brk.py" "$UCD" "$OUT"
OWN="$HERE/uo_props.c $HERE/uo_case.c $HERE/uo_brk.c $HERE/uo_utext.c $OUT/uo_data.c $OUT/uo_brk_data.c"
CFLAGS="-O2 -std=gnu99 -Wall -Wextra -Wno-unused-parameter -Werror -I$SRC/common -I$SRC/i18n"
cc $CFLAGS -I"$OUT" -I"$HERE" "$HERE/tests/host_props.c" $OWN \
    -L"$LIB" -licuuc -licudata -o "$OUT/host_props"
cc $CFLAGS -DWITH_ICU -I"$OUT" -I"$HERE" "$HERE/tests/host_brk.c" $OWN \
    -L"$LIB" -licuuc -licudata -o "$OUT/host_brk"
cc $CFLAGS "$HERE/../tests/UnicodeProps.c" -L"$LIB" -licuuc -licudata -o "$OUT/UnicodeProps"
LD_LIBRARY_PATH="$LIB" "$OUT/host_props"
LD_LIBRARY_PATH="$LIB" ICU_DATA="${ICU_TRIMMED:-${ICU_DATA:-}}" MAXSHOW="${MAXSHOW:-200}" "$OUT/host_brk" "$SRC/test/testdata" 300000
LD_LIBRARY_PATH="$LIB" "$OUT/UnicodeProps" | grep '^UPROPS ' > "$OUT/UnicodeProps.expected"
cat "$OUT/UnicodeProps.expected"
