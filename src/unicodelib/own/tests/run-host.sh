#!/bin/sh
# unicode.library, our own code: the host checks against ICU 78.
# Copyright (c) 2026 AmigaChrome Team. MIT.
#
#   run-host.sh UCD_DIR ICU_SRC ICU_HOST_LIB [OUT]
#
# UCD_DIR       Unicode 17.0.0 (scripts/fetch-ucd.sh)
# ICU_SRC       ICU 78.3's source folder (with common/ and i18n/)
# ICU_HOST_LIB  the host build's lib/ (libicuuc, libicudata)
#
# 1. host_props: our calls against ICU's, every code point (exits 1 on
#    any difference).
# 2. UnicodeProps (../tests/UnicodeProps.c) built against ICU itself: its
#    lines, OUT/UnicodeProps.expected, are what the Amiga build of the same
#    program, on the library, must print.
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
UCD=$1; SRC=$2; LIB=$3; OUT=${4:-"$HERE/../../../build/unicode-own-host"}
mkdir -p "$OUT"
python3 "$HERE/gen_props.py" "$UCD" "$SRC/common" "$OUT"
CFLAGS="-O2 -std=gnu99 -Wall -Wextra -Wno-unused-parameter -Werror -I$SRC/common -I$SRC/i18n"
cc $CFLAGS -I"$OUT" -I"$HERE" "$HERE/tests/host_props.c" "$HERE/uo_props.c" "$HERE/uo_case.c" "$OUT/uo_data.c" \
    -L"$LIB" -licuuc -licudata -o "$OUT/host_props"
cc $CFLAGS "$HERE/../tests/UnicodeProps.c" -L"$LIB" -licuuc -licudata -o "$OUT/UnicodeProps"
LD_LIBRARY_PATH="$LIB" "$OUT/host_props"
LD_LIBRARY_PATH="$LIB" "$OUT/UnicodeProps" | grep '^UPROPS ' > "$OUT/UnicodeProps.expected"
cat "$OUT/UnicodeProps.expected"
