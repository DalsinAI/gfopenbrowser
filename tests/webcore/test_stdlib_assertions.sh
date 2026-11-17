#!/bin/sh
# A new engine build folder turns WebKit's libstdc++ assertions off
# (-DUSE_CXX_STDLIB_ASSERTIONS=OFF, so no -D_GLIBCXX_ASSERTIONS on every
# file); OB_STDLIB_ASSERTIONS=1 keeps them. The case is taken out of
# build-webcore.sh's configure_engine and run as it is there.
# MIT, Copyright (c) 2026 AmiCradle Team.
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
WS="$ROOT/scripts/build-webcore.sh"
pass=0 fail=0
ok() { if eval "$1"; then pass=$((pass + 1)); else fail=$((fail + 1)); echo "FAIL: $2"; fi; }
T=$(mktemp -d "${TMPDIR:-/tmp}/ob-stdlib.XXXXXX")
trap 'rm -rf "$T"' EXIT

awk '/^    case "\$\{OB_STDLIB_ASSERTIONS:-0\}" in$/ { p = 1 } p { print } p && /^    esac$/ { exit }' "$WS" > "$T/case.sh"
ok '[ "$(grep -c . "$T/case.sh")" -ge 5 ]' "configure_engine has the OB_STDLIB_ASSERTIONS case"
args() { (set -- -DPORT=JSCOnly; . "$T/case.sh"; printf '%s\n' "$@") 2>&1; }
ok '[ "$(unset OB_STDLIB_ASSERTIONS; args)" = "$(printf "%s\n" -DPORT=JSCOnly -DUSE_CXX_STDLIB_ASSERTIONS=OFF)" ]' "by default the assertions are off, after the options before them"
ok '[ "$(OB_STDLIB_ASSERTIONS=0 args)" = "$(printf "%s\n" -DPORT=JSCOnly -DUSE_CXX_STDLIB_ASSERTIONS=OFF)" ]' "OB_STDLIB_ASSERTIONS=0: off"
ok '[ "$(OB_STDLIB_ASSERTIONS=1 args)" = "$(printf "%s\n" -DPORT=JSCOnly -DUSE_CXX_STDLIB_ASSERTIONS=ON)" ]' "OB_STDLIB_ASSERTIONS=1: on"
ok '! (OB_STDLIB_ASSERTIONS=yes; set --; . "$T/case.sh") >/dev/null 2>&1' "another value stops the build"

# Only a new folder takes it (an existing one keeps its cache), and the
# engine's fingerprint sees it: engine_options reads USE_* from the cache.
n_case=$(grep -n '^    case "\${OB_STDLIB_ASSERTIONS:-0}" in$' "$WS" | cut -d: -f1)
n_keep=$(grep -n '^        cmake "\$BUILD" "\$@"$' "$WS" | cut -d: -f1)
n_new=$(grep -n '^    cmake -S "\$WEBKIT_SOURCE" -B "\$BUILD"' "$WS" | cut -d: -f1)
ok '[ "$n_keep" -lt "$n_case" ] && [ "$n_case" -lt "$n_new" ]' "the option is set between the existing folder's return and the new folder's cmake"
ok 'grep -q "USE_\[A-Z0-9_\]+" "$WS"' "engine_options takes USE_* options into the fingerprint"
ok 'grep -q "^#   OB_STDLIB_ASSERTIONS" "$WS"' "the option is documented in the script's head"

# WebKit has the option (when a checkout is at hand).
if [ -f "${WEBKIT_SOURCE:-/nonexistent}/Source/cmake/OptionsCommon.cmake" ]; then
    ok 'grep -q "^option(USE_CXX_STDLIB_ASSERTIONS" "$WEBKIT_SOURCE/Source/cmake/OptionsCommon.cmake"' "WebKit's OptionsCommon.cmake has USE_CXX_STDLIB_ASSERTIONS"
fi

echo "stdlib assertions: $pass passed, $fail failed"
[ "$fail" -eq 0 ]
