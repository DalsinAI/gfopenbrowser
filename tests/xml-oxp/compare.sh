#!/bin/sh
# The OXP bridge (WebKit patch 0030's XMLDocumentParserOXPBridge.h) against
# libxml2: for each file, the calls XMLDocumentParser would get from OXP,
# and libxml2's own SAX2 events, in one text form; the two must match.
#   sh tests/xml-oxp/compare.sh WEBKIT_SOURCE OXP_DIR LIST
# WEBKIT_SOURCE: WebKit with the patches applied (for the bridge header).
# OXP_DIR: an openamigaxml checkout's oxp/, built with REF=1 sh build-host.sh
#          (liboxp.a and tests/ref's libxml2dump, which needs libxml2's headers).
# LIST: files to read, one path a line.
# MIT licence. Copyright (c) 2026 AmiCradle Team.
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
WK=$1; OXP=$2; LIST=$3
B="$OXP/build"
mkdir -p out
${CXX:-c++} -std=c++20 -O2 -Wall -Wextra -Werror -I"$WK/Source/WebCore/xml/parser" -I"$OXP/include" \
    -o out/bridgetest "$HERE/bridgetest.cpp" "$B/liboxp.a"
: > out/results.txt
while read -r f; do
  [ -r "$f" ] && [ -f "$f" ] || continue
  # libxml2 gives the end tag's name and its own error levels; the bridge passes neither on
  "$B/libxml2dump" "$f" 2>/dev/null | sed 's/^E .*/E ?/; s/^ERROR .*/ERROR/' > out/r.txt || true
  r=0; "$B/libxml2dump" "$f" >/dev/null 2>&1 || r=$?
  out/bridgetest "$f" 2>/dev/null | sed 's/^ERROR .*/ERROR/' > out/o.txt || true
  o=0; out/bridgetest "$f" >/dev/null 2>&1 || o=$?
  if [ "$r" != 0 ] || [ "$o" != 0 ]; then echo "BOTH-OR-ONE-REFUSED libxml2=$r oxp=$o $f"
  elif cmp -s out/r.txt out/o.txt; then echo "SAME $f"
  else echo "DIFF $f"; fi
done < "$LIST" >> out/results.txt
cut -d' ' -f1 out/results.txt | sort | uniq -c
