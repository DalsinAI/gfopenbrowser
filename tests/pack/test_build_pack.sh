#!/bin/sh
# GFOpenBrowser (DalsinAI/gfopenbrowser)
# Copyright (c) 2026 AmiCradle Team. MIT licence, see LICENSE.
#
# Host test of scripts/build-pack.sh, scripts/fetch-tarballs.sh and their pin
# files, with no network and no stove: stage order, the dry run, the fonts
# stage's checks, a failing stage stopping the build, and every tarball refusal.
#   sh tests/pack/test_build_pack.sh
set -u
HERE=$(cd "$(dirname "$0")/../.." && pwd)
PACK="$HERE/scripts/build-pack.sh"
TBF="$HERE/scripts/fetch-tarballs.sh"
T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT
pass=0 fail=0
ok() { if eval "$1"; then pass=$((pass + 1)); else fail=$((fail + 1)); echo "FAIL: $2"; fi; }
sum() { sha256sum < "$1" | cut -d' ' -f1; }

# Stages and the dry run.
ok '[ "$(sh "$PACK" --list | tr "\n" " ")" = "fetch tarballs fonts ca oab-deps libs engine package " ]' "stages in order"
DRY_RUN=1 PACK_DIR="$T/dry" sh "$PACK" > "$T/out" 2>&1
ok '[ $? -eq 0 ] && [ ! -e "$T/dry" ]' "a dry run makes nothing"
ok '[ "$(grep -c "^== " "$T/out")" -eq 8 ]' "a dry run prints every stage"
ok 'grep -q "fetch-deps.sh $T/dry/deps" "$T/out"' "the dry run names fetch-deps"
ok 'grep -q "fetch-ucd.sh $T/dry/oab-deps/ucd-17.0.0" "$T/out"' "oab-deps fetches the pinned Unicode database"
ok 'grep -q "CA_BUNDLE=$T/dry/ca " "$T/out" && grep -q "OB_FONTS=$T/dry/fonts " "$T/out"' "package takes the ca and fonts stages' output"
PACK_DIR="$T/p" sh "$PACK" nosuch > "$T/out" 2>&1
ok '[ $? -ne 0 ] && grep -q "no stage nosuch" "$T/out" && [ ! -e "$T/p" ]' "an unknown stage is refused before anything runs"
sh "$PACK" fetch > "$T/out" 2>&1
ok '[ $? -ne 0 ] && grep -q PACK_DIR "$T/out"' "PACK_DIR is required"

# The pin files parse.
DRY_RUN=1 sh "$TBF" "$T/tbdry" > "$T/out" 2>&1
ok '[ $? -eq 0 ] && [ ! -e "$T/tbdry" ]' "the repository's tarballs.lock parses"
ok '[ "$(wc -l < "$T/out")" -eq "$(grep -c "^[A-Za-z]" "$HERE/scripts/tarballs.lock")" ]' "every tarball listed once"
ok '[ "$(grep -vc "^#" "$HERE/scripts/fonts.sha256")" -eq 14 ]' "fonts.sha256 pins 14 fonts"

# The fonts stage: right fonts pass, a changed one is refused, a missing one is named.
mkdir -p "$T/f1" "$T/f2"
grep -v '^#' "$HERE/scripts/fonts.sha256" | while read -r s f; do echo "$f" > "$T/f1/$f"; printf '%s %s\n' "$(sum "$T/f1/$f")" "$f"; done > "$T/fonts.sha256"
mkdir -p "$T/repo/scripts"
cp "$PACK" "$T/repo/scripts/"; cp "$T/fonts.sha256" "$T/repo/scripts/fonts.sha256"
mv "$T/f1/DejaVuSans.ttf" "$T/f2/"
PACK_DIR="$T/p" FONTS_FROM="$T/f1 $T/f2" sh "$T/repo/scripts/build-pack.sh" fonts > "$T/out" 2>&1
ok '[ $? -eq 0 ] && [ "$(ls "$T/p/fonts" | wc -l)" -eq 14 ] && [ -f "$T/p/fonts/.checked" ]' "fonts from two folders, all checked"
ok 'grep -q "fonts: .* s, exit 0" "$T/p/pack.log"' "the stage is logged"
echo changed > "$T/f2/DejaVuSans.ttf"
PACK_DIR="$T/p" FONTS_FROM="$T/f1 $T/f2" sh "$T/repo/scripts/build-pack.sh" fonts > "$T/out" 2>&1
ok '[ $? -ne 0 ] && grep -q "DejaVuSans.ttf is not the pinned one" "$T/out" && [ ! -f "$T/p/fonts/.checked" ]' "a changed font is refused"
ok 'grep -q "fonts: .* s, exit [1-9]" "$T/p/pack.log"' "the failure is logged"
rm "$T/f2/DejaVuSans.ttf"
PACK_DIR="$T/p" FONTS_FROM="$T/f1 $T/f2" sh "$T/repo/scripts/build-pack.sh" fonts ca > "$T/out" 2>&1
ok '[ $? -ne 0 ] && grep -q "font DejaVuSans.ttf is in none" "$T/out" && ! grep -q "^== ca" "$T/out"' "a failing stage stops the build"
# A stage whose first command fails stops there (set -e holds in a stage).
PACK_DIR="$T/p" sh "$T/repo/scripts/build-pack.sh" ca > "$T/out" 2>&1
ok '[ $? -ne 0 ] && grep -q "stage ca failed" "$T/out"' "a failing command fails its stage"
PACK_DIR="$T/p" sh "$T/repo/scripts/build-pack.sh" engine > "$T/out" 2>&1
ok '[ $? -ne 0 ]' "engine without a stove or fonts is refused"

# fetch-tarballs: copy from TARBALLS_FROM, keep, refuse.
mkdir -p "$T/from"
echo a > "$T/from/a.tar.gz"; echo b > "$T/from/b.zip"
A=$(sum "$T/from/a.tar.gz"); B=$(sum "$T/from/b.zip")
printf '# t\n\na.tar.gz %s -  no url\nb.zip %s https://example.invalid/b.zip  one\n' "$A" "$B" > "$T/tlock"
tb() { LOCK="$T/tlock" TARBALLS_FROM="$T/from" sh "$TBF" "$T/tb" > "$T/out" 2>&1; }
tb; ok '[ $? -eq 0 ] && [ "$(sum "$T/tb/a.tar.gz")" = "$A" ] && [ -f "$T/tb/b.zip" ]' "tarballs copied from TARBALLS_FROM"
tb; ok '[ $? -eq 0 ] && [ "$(grep -c "there already" "$T/out")" -eq 2 ]' "a re-run keeps them"
echo x > "$T/tb/b.zip"
tb; ok '[ $? -ne 0 ] && grep -q "another sha256; move it aside" "$T/out" && [ "$(cat "$T/tb/b.zip")" = x ]' "a wrong file is refused and kept"
rm -rf "$T/tb"; echo y > "$T/from/b.zip"
tb; ok '[ $? -ne 0 ] && grep -q "from/b.zip has another sha256" "$T/out" && [ ! -e "$T/tb/b.zip" ]' "a wrong source is refused"
rm -rf "$T/tb" "$T/from/a.tar.gz"
LOCK="$T/tlock" sh "$TBF" "$T/tb" > "$T/out" 2>&1
ok '[ $? -ne 0 ] && grep -q "a.tar.gz: no URL" "$T/out"' "a file with no URL is named"
for bad in "a.tar.gz abc -" "a.tar.gz $A" "a.tar.gz $A http://x/a" "a/b $A -" "a.tar.gz $A -
a.tar.gz $A -"; do
    rm -rf "$T/tb"; printf '%s\n' "$bad" > "$T/tlock"
    LOCK="$T/tlock" sh "$TBF" "$T/tb" > "$T/out" 2>&1
    ok '[ $? -ne 0 ] && [ ! -e "$T/tb" ]' "bad lock refused: $(printf '%s' "$bad" | head -1 | cut -c1-30)"
done
printf '# none\n' > "$T/tlock"
LOCK="$T/tlock" sh "$TBF" "$T/tb" > "$T/out" 2>&1
ok '[ $? -ne 0 ] && grep -q "no files" "$T/out"' "an empty lock is refused"

echo "build-pack: $pass passed, $fail failed"
[ "$fail" -eq 0 ]
