#!/bin/sh
# GFOpenBrowser (DalsinAI/gfopenbrowser)
# Copyright (c) 2026 AmiCradle Team. MIT licence, see LICENSE.
#
# The Unicode Character Database that unicode.library's own code is made
# from (src/unicodelib/own, PLAN.md step 7): Unicode 17.0.0, the version of
# ICU 78. Pinned by URL and sha256; the files the build reads are checked
# again against src/unicodelib/own/ucd.sha256.
#
#   fetch-ucd.sh DIR      DIR gets UCD.zip's files (UnicodeData.txt, ...)
#
# scripts/build-unicode-library.sh takes DIR as UCD_DIR, by default
# $OAB_DEPS/ucd-17.0.0.
set -eu
DIR=${1:?usage: fetch-ucd.sh DIR}
URL=https://www.unicode.org/Public/17.0.0/ucd/UCD.zip
SHA256=2066d1909b2ea93916ce092da1c0ee4808ea3ef8407c94b4f14f5b7eb263d28e
mkdir -p "$DIR"
curl -fsSL -o "$DIR/UCD.zip" "$URL"
echo "$SHA256  $DIR/UCD.zip" | sha256sum -c -
(cd "$DIR" && unzip -qo UCD.zip && rm UCD.zip)
echo "fetch-ucd.sh: Unicode 17.0.0 in $DIR"
