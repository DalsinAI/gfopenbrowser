#!/bin/sh
# GFOpenTLS's headers come from the gfopentls checkout: this repository
# keeps no copy of them for the engine (18 November 2026). A copy went
# stale once already (it missed gfopentls.library's revision 5 calls).
# MIT, Copyright (c) 2026 AmiCradle Team.
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
C="$ROOT/src/webcore/CMakeLists.txt"
pass=0 fail=0
ok() { if eval "$1"; then pass=$((pass + 1)); else fail=$((fail + 1)); echo "FAIL: $2"; fi; }

ok '[ ! -e "$ROOT/third_party/gfopentls" ]' "third_party/gfopentls is gone"
ok '[ -z "$(find "$ROOT/src" "$ROOT/third_party" -name "gfopentls*.h" 2>/dev/null)" ]' "no gfopentls header is kept here"
ok '! grep -q "third_party/gfopentls" "$C" "$ROOT"/scripts/*.sh' "nothing builds against the old copy"
ok 'grep -q "INCLUDE_DIRECTORIES \"\${OB_GFOPENTLS_DIR}/include\")" "$C"' "the native transport is given the checkout headers"
ok 'grep -q "set(OB_GFOPENTLS_DIR \"\$ENV{GFOPENTLS_DIR}\")" "$C"' "GFOPENTLS_DIR names the checkout"
ok 'grep -q "gfopentls not found at" "$C"' "configure stops when the checkout is missing"
ok 'grep -q "GFOPENTLS_DIR=\"\$D/gfopentls\"" "$ROOT/scripts/build-pack.sh"' "build-pack.sh passes its gfopentls checkout"

echo "test_tls_headers_from_checkout: $pass passed, $fail failed"
[ "$fail" -eq 0 ]
