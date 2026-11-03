#!/bin/sh
# fetch-cjk-font.sh [CACHE_DIR]: the browser's CJK font, fetched at build
# time and never kept in git. Prints the font's path.
#
# Liberation and DejaVu have no Chinese, Japanese or Korean, so that text
# drew as boxes. The fallback (FontCache::systemFallbackForCharacterCluster,
# WebKit patch 0090) asks gfopenfont.library for any face in Fonts/ with the
# character, so one face that covers the CJK ideographs, kana and Hangul is
# enough. Droid Sans Fallback Full (Apache License 2.0, THIRD_PARTY_NOTICES.md)
# is TrueType and about 4 MB. gfopenfont.library holds a face's whole file in
# memory once it is opened, and only pages with such text open it.
#
# OB_CJK_FONT: a copy of the same file to use instead (it must match the
# pinned sha256).
#
# Copyright (c) 2026 AmiCradle Team. MIT licence, see LICENSE.
set -eu
CACHE=${1:-"${XDG_CACHE_HOME:-$HOME/.cache}/gfopenbrowser/fonts"}
NAME=DroidSansFallbackFull.ttf
# AOSP's copy, at a fixed tag; googlesource gives files base64 with ?format=TEXT
TAG=android-14.0.0_r1
BASE="https://android.googlesource.com/platform/frameworks/base/+/refs/tags/$TAG/data/fonts"
SHA256=${CJK_FONT_SHA256:-PINNED_ON_FIRST_FETCH}
NOTICE_SHA256=${CJK_NOTICE_SHA256:-PINNED_ON_FIRST_FETCH}

sha() {
    if command -v sha256sum >/dev/null 2>&1; then sha256sum "$1" | cut -c1-64; else shasum -a 256 "$1" | cut -c1-64; fi
}
check() {  # FILE [SHA256]
    want=${2:-$SHA256}
    got=$(sha "$1")
    [ "$got" = "$want" ] || { echo "fetch-cjk-font.sh: $1 has sha256 $got, not $want" >&2; return 1; }
}
fetch() {  # NAME SHA256 DEST
    curl -fsSL "$BASE/$1?format=TEXT" | base64 -d > "$3.tmp"
    check "$3.tmp" "$2" || { rm -f "$3.tmp"; exit 1; }
    mv "$3.tmp" "$3"
}

mkdir -p "$CACHE"
F="$CACHE/$NAME"
if [ -n "${OB_CJK_FONT:-}" ]; then
    check "$OB_CJK_FONT"
    cp "$OB_CJK_FONT" "$F.tmp" && mv "$F.tmp" "$F"
elif ! [ -f "$F" ] || ! check "$F" 2>/dev/null; then
    fetch "$NAME" "$SHA256" "$F"
fi
# the licence (Apache License 2.0, AOSP's NOTICE for these fonts) travels
# with the font: packages put it beside it as DroidSansFallback.NOTICE
N="$CACHE/DroidSansFallback.NOTICE"
if ! [ -f "$N" ] || ! check "$N" "$NOTICE_SHA256" 2>/dev/null; then
    fetch NOTICE "$NOTICE_SHA256" "$N"
fi
echo "$F"
