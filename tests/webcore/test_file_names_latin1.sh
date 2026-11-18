#!/bin/sh
# File names go to the Amiga filing system as ISO 8859-1 (WebKit patch 0125,
# 18 November 2026). WTF passed paths as UTF-8, so a file with an accent in
# its name was not found: a page that got "Grün-café.txt" from its file
# button saw size 0 and FileReader failed.
#
# WTF is not built on this machine, so this reads the patch: each of the
# four places where a path crosses to or from the filing system must
# convert on OS(AMIGA), and the other platforms' lines must stay. That an
# accented file then reads is the lab's check (tests/pages/blob-registry.html
# with a file whose name has an accent).
# MIT, Copyright (c) 2026 AmiCradle Team.
set -u
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
P="$ROOT/webkit/patches/0125-amiga-file-names-are-latin1.patch"
pass=0 fail=0
ok() { if eval "$1"; then pass=$((pass + 1)); else fail=$((fail + 1)); echo "FAIL: $2"; fi; }

ok '[ -f "$P" ]' "patch 0125 is there"
if [ -f "$P" ]; then
    ok 'grep -q "^diff --git a/Source/WTF/wtf/FileSystem.cpp " "$P"' "it changes FileSystem.cpp"
    ok 'grep -q "^diff --git a/Source/WTF/wtf/posix/FileSystemPOSIX.cpp " "$P"' "it changes FileSystemPOSIX.cpp"
    ok '[ "$(grep -c "^diff --git " "$P")" -eq 2 ]' "and no other file"
    ok '[ "$(grep -c "^+#if OS(AMIGA)$" "$P")" -eq 4 ]' "four places convert, each only on OS(AMIGA)"
    ok 'grep -q "^+    auto latin1 = path.toString().latin1();" "$P"' "toStdFileSystemPath sends ISO 8859-1"
    ok 'grep -q "^+    return String::fromLatin1(path.string().c_str());" "$P"' "fromStdFileSystemPath reads ISO 8859-1"
    ok 'grep -q "^+    auto latin1 = path.latin1();" "$P"' "fileSystemRepresentation sends ISO 8859-1"
    ok 'grep -q "^+    return String::fromLatin1(reinterpret_cast<const char\*>(path.span().data()));" "$P"' "stringFromFileSystemRepresentation reads ISO 8859-1"
    # the other platforms keep their UTF-8 lines: only the two #if lines that became #elif change
    ok '[ "$(grep -c "^-[^-]" "$P")" -eq 2 ]' "two lines are replaced, no more"
    ok '[ "$(grep -c "^-#if HAVE(MISSING_U8STRING)$" "$P")" -eq 2 ] && [ "$(grep -c "^+#elif HAVE(MISSING_U8STRING)$" "$P")" -eq 2 ]' "the std::filesystem branches stay, as #elif"
    ok 'grep -q "^     return path.utf8();" "$P" && grep -q "^     return String::fromUTF8(path.span());" "$P"' "the POSIX UTF-8 lines stay, in #else"
    ok '! grep -q "^+.*utf8()\\|^+.*fromUTF8(" "$P"' "no new UTF-8 path is added"
    ok 'grep -q "0125-amiga-file-names-are-latin1.patch" "$ROOT/webkit/README.md"' "webkit/README.md lists the patch"
    ok 'head -3 "$P" | grep -q "^From: SacredTrees "' "the patch has the project's author line"
fi

echo "test_file_names_latin1: $pass passed, $fail failed"
[ "$fail" -eq 0 ]
