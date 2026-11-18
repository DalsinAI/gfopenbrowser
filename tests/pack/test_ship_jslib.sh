#!/bin/sh
# GFOpenBrowser (DalsinAI/gfopenbrowser)
# Copyright (c) 2026 AmiCradle Team. MIT licence, see LICENSE.
#
# The package carries javascript.library and JS, and both installers put
# them in LIBS: and C: (18 November 2026). Host test, no stove: the pack
# script builds the library and hands it to the packer, the packer puts it
# in the drawer, and the installers' steps are there, name the right
# version and are well formed. That an Amiga then runs JS and a program's
# JS_Evaluate from an install is the lab's check; nothing here can run the
# Installer.
#   sh tests/pack/test_ship_jslib.sh
set -u
HERE=$(cd "$(dirname "$0")/../.." && pwd)
T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT
pass=0 fail=0
ok() { if eval "$1"; then pass=$((pass + 1)); else fail=$((fail + 1)); echo "FAIL: $2"; fi; }
PB="$HERE/scripts/package-browser.sh"
INST="$HERE/dist/Install-GFOpenBrowser"
SH="$HERE/dist/GFOpenBrowser/Install"

# build-pack.sh: the libs stage builds it, the package stage is told where.
DRY_RUN=1 PACK_DIR="$T/dry" sh "$HERE/scripts/build-pack.sh" > "$T/out" 2>&1
ok 'grep -q "build-javascript-library.sh $T/dry/libs/javascript-library" "$T/out"' "the libs stage builds javascript.library and JS"
ok 'grep -q "JS_LIB_DIR=$T/dry/libs/javascript-library " "$T/out"' "the package stage is given that folder"

# package-browser.sh: its javascript.library lines, run here on made-up files.
PART=$(sed -n '/^    JSLIBDIR=/,/^    cp "\$JSLIBDIR\/JS"/p' "$PB")
ok '[ -n "$PART" ]' "the packer has its javascript.library step"
mkdir -p "$T/lib" "$T/d/Libs"
echo library > "$T/lib/javascript.library"; echo command > "$T/lib/JS"
(set -eu; HERE="$T/none"; D="$T/d"; JS_LIB_DIR="$T/lib"; eval "$PART") > "$T/p.out" 2>&1
ok '[ $? -eq 0 ] && cmp -s "$T/lib/javascript.library" "$T/d/Libs/javascript.library" && cmp -s "$T/lib/JS" "$T/d/C/JS"' "the packer puts Libs/javascript.library and C/JS in the drawer"
rm "$T/lib/JS"
(set -eu; HERE="$T/none"; D="$T/d2"; JS_LIB_DIR="$T/lib"; eval "$PART") > "$T/p.out" 2>&1
ok '[ $? -eq 2 ] && grep -q "build-javascript-library.sh" "$T/p.out"' "the packer stops when JS is not built, and names the script"
ok '! grep -q "EXTRAS\|Extras" "$PB"' "the packer has no Extras folder any more"

# The version the Installer asks for is the library's own.
major=$(sed -n 's/^#define LIB_VERSION  *\([0-9]*\).*/\1/p' "$HERE/src/jslib/javascript_lib.c")
minor=$(sed -n 's/^#define LIB_REVISION  *\([0-9]*\).*/\1/p' "$HERE/src/jslib/javascript_lib.c")
want=$((major * 65536 + minor))
ok '[ -n "$major" ] && [ -n "$minor" ]' "javascript_lib.c gives its version"
ok 'grep -q "(P_newer \"LIBS:javascript.library\" \"GFOpenBrowser/Libs/javascript.library\" \"LIBS:\" $want)" "$INST"' "Install-GFOpenBrowser puts javascript.library $major.$minor in when missing or older"
ok 'grep -q "(P_newer \"C:JS\" \"GFOpenBrowser/C/JS\" \"C:\" 65536)" "$INST"' "Install-GFOpenBrowser puts JS in C: when missing or older"
ok 'grep -q "\$VER: JS 1\.0 " "$HERE/src/jslib/JS.c"' "JS is 1.0, the version asked for"
ok '! grep -q "askoptions\|#extras\|Extras/" "$INST"' "Install-GFOpenBrowser asks nothing about extras"
ok 'grep -q "(getdiskspace \"LIBS:\")" "$INST"' "Install-GFOpenBrowser looks at the room in LIBS: first"
# Step 5 runs on every install: it must not stop the Installer on a full
# System:. Its room counts the Unicode data too, and both copies may fail.
STEP=$(sed -n '/^; 5\. javascript.library/,/^(if (not (exists "LIBS:bsdsocket.library"))/p' "$INST")
ok 'echo "$STEP" | grep -q "(set #jsneed (+ (if #jssame 0 (getsize #jsenew)) (if #jswanticu (getsize #icunew) 0) 2000000))"' "step 5's room is the engine, the Unicode data when it is to be copied, and 2,000,000 bytes"
ok '[ "$(echo "$STEP" | grep -c "(copyfiles")" -eq 2 ] && [ "$(echo "$STEP" | grep -c "(optional \"nofail\")")" -eq 2 ]' "both of step 5's copies are nofail"
ok 'echo "$STEP" | grep -q "(if #jsok" && [ "$(echo "$STEP" | grep -c "\.incomplete-")" -eq 2 ]' "step 5 checks both files after the copy and renames a part-written one"
ok '[ "$(grep -n "(set #icunew" "$INST" | cut -d: -f1)" -lt "$(grep -n "(set #jswanticu" "$INST" | cut -d: -f1)" ]' "#icunew is set before step 5 uses it"
ok 'grep -q "^  Copy Libs/WebBrowser/Unicode/icudt78b.dat LIBS:JavaScript/Unicode QUIET" "$SH"' "the Shell Install gives the engine its Unicode data when LOCALE: has none"
# P_newer is defined before its use in step 5
ok '[ "$(grep -n "^(procedure P_newer" "$INST" | cut -d: -f1)" -lt "$(grep -n "P_newer \"LIBS:javascript.library\"" "$INST" | cut -d: -f1)" ]' "P_newer is defined before step 5"
# Parentheses outside strings and comments balance, and never close too far.
bal=$(awk '{ line = $0; instr = 0; out = ""
        for (i = 1; i <= length(line); i++) { c = substr(line, i, 1)
            if (instr) { if (c == "\\") i++; else if (c == "\"") instr = 0; continue }
            if (c == "\"") { instr = 1; continue }
            if (c == ";") break
            if (c == "(") d++
            if (c == ")") { d--; if (d < 0) bad = NR } } }
    END { print d + 0, bad + 0 }' "$INST")
ok '[ "$bal" = "0 0" ]' "Install-GFOpenBrowser's parentheses balance ($bal)"

# The Shell script: the same step, well formed.
ok 'grep -q "^  Copy Libs/javascript.library LIBS: QUIET" "$SH" && grep -q "^  Copy C/JS C: QUIET" "$SH" && grep -q "^Copy Libs/JavaScript/JavaScriptEngine LIBS:JavaScript QUIET" "$SH"' "the Shell Install copies the library, JS and the engine"
ok '[ "$(grep -c "^Lab jsdone" "$SH")" -eq 1 ] && [ "$(grep -c "Skip jsdone" "$SH")" -ge 2 ]' "the Shell Install's step has its label"
ok '[ "$(grep -c "^ *If " "$SH")" -eq "$(grep -c "^ *EndIf" "$SH")" ]' "the Shell Install's If and EndIf pair up"

echo "test_ship_jslib: $pass passed, $fail failed"
[ "$fail" -eq 0 ]
