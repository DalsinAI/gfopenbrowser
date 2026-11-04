#!/bin/sh
# The browser's dialogs on the host (test_dialogs.c), against GFOpenLayout's
# sources, and each one drawn as a picture with GFOpenLayout's
# tests/ui_render.py (needs python3 with PIL; without it, only the ops).
#
#   tests/dialogs/run.sh [GFOPENLAYOUT_DIR] [OUT_DIR]
#
# GFOPENLAYOUT_DIR defaults to ../gfopenlayout beside this repository,
# OUT_DIR to build/dialogs.
# MIT, Copyright (c) 2026 AmiCradle Team.
set -eu
HERE=$(cd "$(dirname "$0")/../.." && pwd)
OL=${1:-${GFOPENLAYOUT_DIR:-"$HERE/../gfopenlayout"}}
OUT=${2:-"$HERE/build/dialogs"}
CC=${CC:-cc}
[ -f "$OL/ui/olui.c" ] || { echo "run.sh: no GFOpenLayout at $OL (give its checkout)" >&2; exit 2; }
mkdir -p "$OUT"
$CC -std=c99 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined \
    -I"$OL/include" -I"$OL/ui" -I"$OL/src" -I"$HERE/src/webcore" \
    "$HERE/tests/dialogs/test_dialogs.c" \
    "$OL/ui/olui.c" "$OL/ui/olui_css.c" "$OL/ui/olui_markup.c" "$OL/ui/olui_theme_none.c" \
    "$OL/src/openlayout.c" "$OL/src/gfcss_parse.c" "$OL/src/gfcss_values.c" "$OL/src/gfcss_select.c" \
    "$OL/src/openlayout_css.c" -o "$OUT/test_dialogs"
"$OUT/test_dialogs" "$OUT"
if python3 -c 'import PIL' 2>/dev/null; then
    for f in "$OUT"/*.ops; do
        python3 "$OL/tests/ui_render.py" "$f" "${f%.ops}.png"
    done
    echo "pictures in $OUT"
fi
