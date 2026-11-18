#!/bin/sh
# GFOpenBrowser (DalsinAI/gfopenbrowser)
# Copyright (c) 2026 AmiCradle Team. MIT licence, see LICENSE.
#
# The packer refuses a webbrowser.library older than this tree's source
# (18 November 2026). WEBBROWSER_LIB may name a library built earlier, and
# a lab ran with a 1.1 whose RUNSCRIPT=NO did nothing. Host test, no stove:
# the packer's own lines run here on made-up library files.
#   sh tests/pack/test_webbrowser_lib_version.sh
set -u
HERE=$(cd "$(dirname "$0")/../.." && pwd)
T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT
pass=0 fail=0
ok() { if eval "$1"; then pass=$((pass + 1)); else fail=$((fail + 1)); echo "FAIL: $2"; fi; }
PB="$HERE/scripts/package-browser.sh"

PART=$(sed -n '/^WBWANT=/,/^fi$/p' "$PB")
ok '[ -n "$PART" ]' "the packer has its webbrowser.library version step"
ok '[ "$(grep -n "^WBWANT=" "$PB" | cut -d: -f1)" -lt "$(grep -n "^rm -rf \"\$OUT\"" "$PB" | cut -d: -f1)" ]' "the step runs before the packer clears the output folder"

# A made-up source tree whose library says 1.2, and libraries of each kind.
mkdir -p "$T/src/webbrowser"
echo 'const char LibVersion[] __attribute__((used)) = "$VER: webbrowser.library 1.2 (17.11.2026)";' > "$T/src/webbrowser/wb_library.c"
lib() { printf 'junk\000\001$VER: webbrowser.library %s (1.1.2026)\000more' "$2" > "$T/$1"; }
lib l11 1.1; lib l12 1.2; lib l13 1.3; lib l110 1.10; lib l20 2.0; lib l09 0.9
printf 'junk\000no version here' > "$T/lnone"
run() { (set -eu; HERE="$T"; WBLIB="$T/$1"; eval "$PART") > "$T/out" 2>&1; }

run l12;  ok '[ $? -eq 0 ] && [ ! -s "$T/out" ]' "the same version as the source passes, and says nothing"
run l13;  ok '[ $? -eq 0 ]' "a newer revision passes"
run l110; ok '[ $? -eq 0 ]' "1.10 is newer than 1.2 (numbers, not text)"
run l20;  ok '[ $? -eq 0 ]' "a newer version passes"
run l11;  ok '[ $? -eq 1 ] && grep -q "is 1.1; the package needs webbrowser.library 1.2 or newer" "$T/out"' "1.1 is refused, with both versions named"
run l09;  ok '[ $? -eq 1 ] && grep -q "is 0.9;" "$T/out"' "an older version is refused"
run lnone; ok '[ $? -eq 1 ] && grep -q "of no version this can read" "$T/out"' "a file with no version line is refused"
echo 'no version' > "$T/src/webbrowser/wb_library.c"
run l12;  ok '[ $? -eq 1 ] && grep -q "wb_library.c gives no version" "$T/out"' "a source with no version line stops the packer"

# The real source gives a version the step can read, 1.2 or newer.
real=$(sed -n 's/.*\$VER: webbrowser.library \([0-9]*\.[0-9]*\).*/\1/p' "$HERE/src/webbrowser/wb_library.c" | head -1)
ok '[ -n "$real" ]' "wb_library.c gives its version"
ok '[ "${real%.*}" -gt 1 ] || [ "${real#*.}" -ge 2 ]' "the source is 1.2 or newer ($real)"

echo "test_webbrowser_lib_version: $pass passed, $fail failed"
[ "$fail" -eq 0 ]
