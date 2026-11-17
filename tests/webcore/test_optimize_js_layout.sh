#!/bin/sh
# OB_OPTIMIZE=js-layout, the default for a new engine build folder: -Os
# (MinSizeRel) except JavaScriptCore, its interpreter and WebCore's css/,
# dom/, layout/, rendering/ and style/ code, at -O2. CMake configures a
# small project with those targets (and WebCore's unified sources) through
# scripts/cmake/ob-optimize-js-layout.cmake, and its compile commands are
# read. Needs cmake 3.19+ and a host C++ compiler.
# MIT, Copyright (c) 2026 AmiCradle Team.
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
WS="$ROOT/scripts/build-webcore.sh"
pass=0 fail=0
ok() { if eval "$1"; then pass=$((pass + 1)); else fail=$((fail + 1)); echo "FAIL: $2"; fi; }
T=$(mktemp -d "${TMPDIR:-/tmp}/ob-optimize.XXXXXX")
trap 'rm -rf "$T"' EXIT

# 1. build-webcore.sh
ok 'grep -q "case \"\${OB_OPTIMIZE:-js-layout}\" in" "$WS"' "js-layout is the default"
ok 'grep -q "ob-optimize-js-layout.cmake" "$WS"' "js-layout reads ob-optimize-js-layout.cmake"
ok 'grep -q "CMAKE_PROJECT_INCLUDE|" "$WS"' "the engine's fingerprint reads CMAKE_PROJECT_INCLUDE (the OB_OPTIMIZE choice)"

if ! command -v cmake >/dev/null 2>&1; then
    echo "optimize js-layout: no cmake here, the CMake part is skipped"
else
# 2. A project shaped like WebKit's: the targets the includes look for.
S="$T/src"
mkdir -p "$S/WebCore/css" "$S/WebCore/dom" "$S/WebCore/style" "$S/WebCore/platform"
for f in jsc llint WebCore/css/a WebCore/dom/b WebCore/style/c WebCore/platform/d; do
    echo "int f_$(basename "$f")() { return 0; }" > "$S/$f.cpp"
done
cat > "$S/CMakeLists.txt" <<'CM'
cmake_minimum_required(VERSION 3.20)
project(OBOptimizeTest CXX)
add_library(JavaScriptCore STATIC jsc.cpp)
add_library(LowLevelInterpreterLib STATIC llint.cpp)
add_subdirectory(WebCore)
CM
cat > "$S/WebCore/CMakeLists.txt" <<'CM'
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/UnifiedSource-style.cpp "#include \"style/c.cpp\"\n")
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/UnifiedSource-platform.cpp "#include \"platform/d.cpp\"\n")
add_library(WebCore STATIC css/a.cpp dom/b.cpp platform/d.cpp
    ${CMAKE_CURRENT_BINARY_DIR}/UnifiedSource-style.cpp ${CMAKE_CURRENT_BINARY_DIR}/UnifiedSource-platform.cpp)
target_include_directories(WebCore PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
CM
cmake -S "$S" -B "$T/b" -DCMAKE_BUILD_TYPE=MinSizeRel -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
    -DCMAKE_PROJECT_INCLUDE="$ROOT/scripts/cmake/ob-optimize-js-layout.cmake" > "$T/cmake.out" 2>&1
ok '[ -f "$T/b/compile_commands.json" ]' "CMake configures the project (see $T/cmake.out)"
ok 'grep -q "OB_OPTIMIZE=layout: 3 WebCore sources at -O2" "$T/cmake.out"' "the layout part finds css/a, dom/b and the style bundle"
ok '! grep -q "WARNING" "$T/cmake.out"' "no target is missing"
# The level GCC keeps for each source: the last -O on its command line.
python3 - "$T/b/compile_commands.json" > "$T/levels" <<'PY'
import json, re, sys
for e in json.load(open(sys.argv[1])):
    o = re.findall(r"(?:^|\s)(-O[0-9sz]?)(?=\s|$)", e["command"])
    print(e["file"].split("/src/")[-1].split("/b/")[-1], o[-1] if o else "none")
PY
level() { awk -v f="$1" '$1 == f { print $2 }' "$T/levels"; }
ok '[ "$(level jsc.cpp)" = -O2 ]' "JavaScriptCore at -O2 (got $(level jsc.cpp))"
ok '[ "$(level llint.cpp)" = -O2 ]' "the interpreter at -O2 (got $(level llint.cpp))"
ok '[ "$(level WebCore/css/a.cpp)" = -O2 ]' "WebCore css/ at -O2 (got $(level WebCore/css/a.cpp))"
ok '[ "$(level WebCore/dom/b.cpp)" = -O2 ]' "WebCore dom/ at -O2 (got $(level WebCore/dom/b.cpp))"
ok '[ "$(level WebCore/UnifiedSource-style.cpp)" = -O2 ]' "a unified bundle of style/ at -O2 (got $(level WebCore/UnifiedSource-style.cpp))"
ok '[ "$(level WebCore/platform/d.cpp)" = -Os ]' "WebCore platform/ stays -Os (got $(level WebCore/platform/d.cpp))"
ok '[ "$(level WebCore/UnifiedSource-platform.cpp)" = -Os ]' "a unified bundle of platform/ stays -Os (got $(level WebCore/UnifiedSource-platform.cpp))"
fi

echo "optimize js-layout: $pass passed, $fail failed"
[ "$fail" -eq 0 ]
