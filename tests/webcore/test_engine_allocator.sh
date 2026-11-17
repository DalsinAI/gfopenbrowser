#!/bin/sh
# The engine's programs link the checkout's allocator (stove/compat), not a
# stove's copy of libos32compat.a: src/webcore/CMakeLists.txt builds it
# (OBCompat) and links it into every program, and build-webcore.sh's
# check_allocator stops the build when a program's link map shows malloc
# from anywhere else. Until 0.9.0 RC2 the engine took the stove's copy,
# built on 9 October without the size-class allocator (#26).
# MIT, Copyright (c) 2026 AmiCradle Team.
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
CM="$ROOT/src/webcore/CMakeLists.txt"
WS="$ROOT/scripts/build-webcore.sh"
pass=0 fail=0
ok() { if eval "$1"; then pass=$((pass + 1)); else fail=$((fail + 1)); echo "FAIL: $2"; fi; }

# 1. CMakeLists.txt
ok 'grep -q "^add_library(OBCompat STATIC \${OB_COMPAT_SOURCES} \${OB_COMPAT_LDBL_SOURCES})" "$CM"' "OBCompat is built from stove/compat"
ok 'grep -q "^set(OB_COMPAT_DIR \${CMAKE_CURRENT_SOURCE_DIR}/../../stove/compat)" "$CM"' "OB_COMPAT_DIR is the checkout's stove/compat"
ok 'grep -q "OUTPUT_NAME os32compat" "$CM"' "the archive keeps the name libos32compat.a (jsimage shares its members)"
ok '[ -f "$ROOT/stove/compat/os32compat_malloc.c" ] && grep -q "size classes" "$ROOT/stove/compat/os32compat_malloc.c"' "stove/compat holds the size-class allocator"
# OBCompat inside both link groups: OB_LINK's and JavaScriptEngine's
groups=$(awk '/-Wl,--start-group/ { g = 1; has = 0; next } g && /OBCompat/ { has = 1 } g && /-Wl,--end-group/ { print has; g = 0 }' "$CM" | tr -d '\n')
ok '[ "$groups" = 11 ]' "both link groups (OB_LINK, JavaScriptEngine) link OBCompat (got $groups)"
ok 'grep -q "^target_link_libraries(OpenBrowserLauncher PRIVATE OBCompat pthread)" "$CM"' "the launcher links OBCompat"
ok '! grep -n "AC_OS32_STOVE}/compat" "$CM" >/dev/null' "no program links the stove's compat folder"

# 2. check_allocator, taken out of build-webcore.sh and run on made-up maps
T=$(mktemp -d "${TMPDIR:-/tmp}/ob-alloc.XXXXXX")
trap 'rm -rf "$T"' EXIT
{
    echo 'die() { echo "ERROR: $*" >&2; exit 2; }'
    awk '/^check_allocator\(\) \{/ { p = 1 } p { print } p && /^\}/ { exit }' "$WS"
    echo 'check_allocator "$1"'
} > "$T/check.sh"
ok 'grep -q "^check_allocator() {" "$T/check.sh"' "build-webcore.sh has check_allocator"
ok 'grep -q "check_allocator \"\$BUILD/logs/.browser-build-start\"" "$WS"' "browser_build runs check_allocator after its build"
mkdir -p "$T/build/bin" "$T/stove/compat"
run() { BUILD="$T/build" GCC16="$T/stove" sh -c ". \"$T/check.sh\"" check "$T/since" >/dev/null 2>&1; }
: > "$T/since"
sleep 1
printf '%s\n' "LOAD $T/build/lib/libos32compat.a(os32compat_malloc.c.obj)" > "$T/build/bin/WebBrowserEngine.map"
ok 'run' "a map with the build's own libos32compat.a passes"
printf '%s\n' " $T/build/lib-folded/WebBrowserEngine/libos32compat.a(os32compat_malloc.c.obj)" > "$T/build/bin/WebBrowserEngine.map"
ok 'run' "a folded link's map passes"
printf '%s\n' "$T/stove/compat/libos32compat.a(os32compat_malloc.o)" > "$T/build/bin/WebBrowserEngine.map"
ok '! run' "the stove's libos32compat.a stops the build"
printf '%s\n' "/home/x/AmiCradle/kitchen/stoves/os32-gcc16/compat/libos32compat.a(os32compat_malloc.o)" > "$T/build/bin/WebBrowserEngine.map"
ok '! run' "another stove's compat folder stops the build"
printf '%s\n' "lib/libWTF.a(FastMalloc.cpp.obj)" > "$T/build/bin/WebBrowserEngine.map"
ok '! run' "a map without os32compat_malloc stops the build"
# a map older than the build's start (a program this build did not link) is not checked
touch -t 202001010000 "$T/build/bin/WebBrowserEngine.map"
ok 'run' "a program not linked by this build is left alone"
cp "$T/build/bin/WebBrowserEngine.map" "$T/build/bin/WebBrowserEngine.plain.map"
touch "$T/build/bin/WebBrowserEngine.plain.map"
ok 'run' "fold's .plain.map copy is not checked"

echo "engine allocator: $pass passed, $fail failed"
[ "$fail" -eq 0 ]
