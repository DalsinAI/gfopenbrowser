#!/bin/sh
# GFOpenBrowser (DalsinAI/gfopenbrowser)
# Copyright (c) 2026 AmiCradle Team. MIT licence, see LICENSE.
#
# JavaScriptEngine, the program behind javascript.library (src/jslib): the
# engine core (jsl_core.cpp, on JavaScriptCore's C interface; jsl_cache.cpp,
# the saved translations, on its private one), its AmigaOS
# layer and its main, linked the way the jsc shell is, with the same
# JavaScriptCore build. Hard float, -O2 (the GFOpenBrowser stack's exception).
#
# The engine the browser ships is built with the browser instead
# (scripts/build-webcore.sh build-javascript): its JavaScriptCore is then
# the browser's own. This script is the older way, on a JSCOnly build.
#
# Build JavaScriptCore first with scripts/build-jsc.sh (same WEBKIT_BUILD):
# this compiles the four files with the flags CMake gave jsc.cpp, then
# runs jsc's own link command with them in jsc.cpp's place.
#
#   WEBKIT_BUILD   the build-jsc.sh build folder        (required)
#   OS32_GCC16     stove root (prefix/, compat/)       (default ~/AmiCradle/stoves/os32-gcc16)
#   OUT            where JavaScriptEngine goes          (default $WEBKIT_BUILD/bin)
#
# MIT, Copyright (c) 2026 AmiCradle Team.
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
: "${WEBKIT_BUILD:?set WEBKIT_BUILD to the build-jsc.sh build folder}"
STOVE=${OS32_GCC16:-"$HOME/AmiCradle/stoves/os32-gcc16"}
OUT=${OUT:-"$WEBKIT_BUILD/bin"}
LIB="$HERE/src/jslib"
SHELLDIR="$WEBKIT_BUILD/Source/JavaScriptCore/shell"
JSCDIR="$SHELLDIR/CMakeFiles/jsc.dir"
[ -f "$JSCDIR/link.txt" ] || { echo "build-javascript-engine.sh: no jsc link command in $JSCDIR: run build-jsc.sh first" >&2; exit 2; }
[ -f "$WEBKIT_BUILD/lib/libJavaScriptCore.a" ] || { echo "build-javascript-engine.sh: no libJavaScriptCore.a in $WEBKIT_BUILD/lib" >&2; exit 2; }

flag() { sed -n "s/^$1 = //p" "$JSCDIR/flags.make" | head -1; }
CXX="$STOVE/prefix/bin/m68k-amigaos-g++"
CC="$STOVE/prefix/bin/m68k-amigaos-gcc"
CXXFLAGS=$(flag CXX_FLAGS)
DEFINES=$(flag CXX_DEFINES)
INCLUDES=$(flag CXX_INCLUDES)
OBJ="$WEBKIT_BUILD/jslib-obj"
mkdir -p "$OBJ" "$OUT"
JSINC="-I$WEBKIT_BUILD/JavaScriptCore/Headers -I$WEBKIT_BUILD/JavaScriptCore/PrivateHeaders -I$LIB -I$LIB/sdk/Include_H"

for f in jsl_core jsl_cache; do
    (cd "$SHELLDIR" && eval "\"$CXX\" $DEFINES $INCLUDES $CXXFLAGS $JSINC -c \"$LIB/$f.cpp\" -o \"$OBJ/$f.o\"")
done
for f in jsl_os_exec jsl_engine; do
    "$CC" -m68040 -O2 -Wall -Wextra -Wno-unused-parameter -fno-common $JSINC -c "$LIB/$f.c" -o "$OBJ/$f.o"
done

# jsc's link command, with our objects for jsc.cpp's and our name for its output
python3 - "$JSCDIR/link.txt" "$OBJ" "$OUT/JavaScriptEngine" > "$OBJ/link.sh" <<'PY'
import shlex, sys
link, obj, out = sys.argv[1], sys.argv[2], sys.argv[3]
words = shlex.split(open(link).read().strip().splitlines()[0])
cmd, skip = [], False
for i, w in enumerate(words):
    if skip:
        skip = False
        continue
    if w.endswith('jsc.cpp.o') or w.endswith('jsc.cpp.obj'):
        cmd += [obj + '/jsl_core.o', obj + '/jsl_cache.o', obj + '/jsl_os_exec.o', obj + '/jsl_engine.o']
    elif w == '-o':
        cmd += ['-o', out, '-Wl,-Map,' + out + '.map']
        skip = True
    else:
        cmd.append(w)
print(' '.join(shlex.quote(w) for w in cmd))
PY
(cd "$SHELLDIR" && sh "$OBJ/link.sh")
ls -l "$OUT/JavaScriptEngine"
sha256sum "$OUT/JavaScriptEngine"
