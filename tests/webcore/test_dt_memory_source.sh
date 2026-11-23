#!/bin/sh
# Pictures go to the datatypes from memory, with no file in T: (WebKit patch
# 0127, 23 November 2026). Every picture the datatypes decode, and every
# animated GIF, was first written to T:OpenBrowser-<task>-<n>.img. With
# datatypes.library 47.100 or later the datatype reads the bytes where they
# are (DTST_MEMORY); OS 3.2's library, and a class that refuses memory, get
# the file as before.
#
# The version rule is plain C (DatatypesSource.h, a new file in the patch):
# this takes it out of the patch, compiles it on the host and checks it
# (tests/webcore/dt_memory_source_test.c). WebCore is not built on this
# machine, so the rest reads the patch. That a page's pictures and an
# animated GIF show with nothing written to T: is the lab's check.
# MIT, Copyright (c) 2026 AmiCradle Team.
#
#   CC   the host compiler (default cc)
#   OUT  where the header and the test go (default a temporary folder)
set -u
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
P="$ROOT/webkit/patches/0127-amiga-pictures-to-datatypes-from-memory.patch"
CC=${CC:-cc}
TMP=
if [ -z "${OUT:-}" ]; then TMP=$(mktemp -d); OUT=$TMP; fi
mkdir -p "$OUT"
pass=0 fail=0
ok() { if eval "$1"; then pass=$((pass + 1)); else fail=$((fail + 1)); echo "FAIL: $2"; fi; }

ok '[ -f "$P" ]' "patch 0127 is there"
if [ -f "$P" ]; then
    # DatatypesSource.h, as the patch adds it: the lines of its hunk, without the +
    awk '/^diff --git /{on = ($0 ~ /amiga\/DatatypesSource\.h$/)} on && /^\+/ && !/^\+\+\+ /{print substr($0, 2)}' \
        "$P" > "$OUT/DatatypesSource.h"
    ok '[ -s "$OUT/DatatypesSource.h" ]' "the patch adds DatatypesSource.h"
    ok '"$CC" -std=c99 -O1 -Wall -Wextra -Werror -I"$OUT" "$ROOT/tests/webcore/dt_memory_source_test.c" -o "$OUT/dt_memory_source_test"' \
        "the version rule compiles on the host"
    ok '"$OUT/dt_memory_source_test"' "the version rule: memory from 47.100, a file below it"

    ok '[ "$(grep -c "^diff --git " "$P")" -eq 2 ]' "it changes DatatypesDecode.c and adds DatatypesSource.h, no other file"
    ok 'grep -q "^diff --git a/Source/WebCore/platform/image-decoders/amiga/DatatypesDecode.c " "$P"' "it changes DatatypesDecode.c"
    ok 'grep -q "^+static Object\* newPicture(const void\* data, unsigned long size, char\* fileName, size_t fileNameSize)$" "$P"' \
        "one function makes the datatype object"
    ok 'grep -q "^+    if (obdt_source_from_memory(DataTypesBase->lib_Version, DataTypesBase->lib_Revision)) {$" "$P"' \
        "memory is asked for only by the library version rule"
    ok 'grep -q "^+            DTA_SourceType, DTST_MEMORY,$" "$P" && grep -q "^+            DTA_SourceAddress, (ULONG)data,$" "$P" && grep -q "^+            DTA_SourceSize, (ULONG)size,$" "$P"' \
        "the memory source carries its address and size"
    ok 'grep -q "^+    if (!writeTemporaryFile(data, size, fileName, fileNameSize)) {$" "$P"' "the file in T: stays as the fallback"
    ok '[ "$(grep -c "^+.*= newPicture(" "$P")" -eq 2 ]' "pictures and animated GIFs both go through it"
    ok 'grep -q "^+    if (fileName\[0\])$" "$P"' "a file is deleted only when there is one"
    ok '[ "$(grep -c "^+#define DTST_MEMORY 5$" "$P")" -eq 1 ] && grep -q "^+#define DTA_SourceAddress (DTA_Dummy + 39)$" "$P" && grep -q "^+#define DTA_SourceSize (DTA_Dummy + 40)$" "$P"' \
        "the V44 numbers are the NDK's"
    # the bytes outlive the object: the changed copy is freed after the decode
    ok 'awk "/^\\+    decoded = changed/{seen = 1} seen && /^[ +]    free\\(changed\\);\$/{found = 1} /^[ +]    free\\(changed\\);\$/ && !seen{early = 1} END{exit !(found && !early)}" "$P"' \
        "an APNG's or GIF's changed copy is freed after the decode, not before"
    ok 'grep -q "0127-amiga-pictures-to-datatypes-from-memory.patch" "$ROOT/webkit/README.md"' "webkit/README.md lists the patch"
    ok 'head -3 "$P" | grep -q "^From: SacredTrees "' "the patch has the project's author line"
fi
[ -n "$TMP" ] && rm -rf "$TMP"

echo "test_dt_memory_source: $pass passed, $fail failed"
[ "$fail" -eq 0 ]
