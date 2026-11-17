#!/bin/sh
# The scripts folder (WebKit patch 0099) is on by default from 0.9.0 RC3,
# keeping each script's top-level code only: on 4 November pages that read
# back files holding functions' code as well stopped loading (reddit.com),
# so it was turned off (#172). OB_SCRIPT_CACHE=0 turns it off.
# MIT, Copyright (c) 2026 AmiCradle Team.
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
P="$ROOT/webkit/patches/0099-amiga-script-bytecode-cache.patch"
V="$ROOT/src/webcore/ob_webview.cpp"
pass=0 fail=0
ok() { if eval "$1"; then pass=$((pass + 1)); else fail=$((fail + 1)); echo "FAIL: $2"; fi; }

# updateCache(), which JavaScriptCore calls with each function's code as it
# is first compiled, adds nothing to what is written.
body=$(awk '/^\+void AmigaScriptBytecode::updateCache\(/ { p = 1; next } p && /^\+\}/ { exit } p' "$P")
ok '[ "$body" = "+{" ]' "updateCache() keeps no function's code (body: $body)"
ok '! grep -q "addFunctionUpdate" "$P"' "nothing adds a function update"
ok 'grep -q "addGlobalUpdate" "$P"' "the top-level code is still kept (cacheBytecode)"
ok 'grep -q "%08lx-%08lx.jsg" "$P" && ! grep -q "%08lx-%08lx.jsc" "$P"' "files are .jsg, so the old .jsc files are never read"
# The patch still applies as a new file of the length its header says.
n=$(awk '/^\+\+\+ b\/Source\/WebCore\/bindings\/js\/amiga\/AmigaScriptBytecode.cpp/ { f = 1; next } f && /^@@ / { split($3, a, ","); print a[2]; exit }' "$P")
got=$(awk '/^\+\+\+ b\/Source\/WebCore\/bindings\/js\/amiga\/AmigaScriptBytecode.cpp/ { f = 1; next } f && /^@@ / { c = 1; next } c && /^diff --git/ { exit } c && /^\+/ { k++ } END { print k }' "$P")
ok '[ "$n" = "$got" ]' "AmigaScriptBytecode.cpp's hunk counts its lines ($n header, $got lines)"

# The browser sets the folder unless OB_SCRIPT_CACHE is "0".
ok 'grep -q "if (!scriptCache || strcmp(scriptCache, \"0\")) {" "$V"' "on unless OB_SCRIPT_CACHE=0"
ok 'grep -q "setAmigaScriptBytecodeFolder(String::fromLatin1(OB_SCRIPTCACHE_DIR))" "$V"' "the folder is PROGDIR:Cache/Scripts"

echo "script cache default: $pass passed, $fail failed"
[ "$fail" -eq 0 ]
