#!/bin/sh
# GFOpenBrowser (DalsinAI/gfopenbrowser)
# Copyright (c) 2026 AmiCradle Team. MIT licence, see LICENSE.
#
# Host test of scripts/fetch-deps.sh and scripts/deps.lock, on local
# repositories only (no network): pins, sparse checkouts, WebKit's patches,
# re-runs, and every refusal.
#   sh tests/pack/test_fetch_deps.sh
set -u
HERE=$(cd "$(dirname "$0")/../.." && pwd)
FETCH="$HERE/scripts/fetch-deps.sh"
T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT
pass=0 fail=0
ok() { if eval "$1"; then pass=$((pass + 1)); else fail=$((fail + 1)); echo "FAIL: $2"; fi; }

export GIT_AUTHOR_NAME=t GIT_AUTHOR_EMAIL=t@t GIT_COMMITTER_NAME=t GIT_COMMITTER_EMAIL=t@t
# file:// fetches, for this test's processes only (no global config is touched)
export GIT_CONFIG_COUNT=1 GIT_CONFIG_KEY_0=protocol.file.allow GIT_CONFIG_VALUE_0=always

# Two upstreams: "lib" with two commits, and "webkit" with a file the patch changes.
mk() { git init -q "$T/up/$1" && git -C "$T/up/$1" config uploadpack.allowAnySHA1InWant true; }
mk lib
mkdir -p "$T/up/lib/keep" "$T/up/lib/skip"
echo one > "$T/up/lib/keep/a.txt"; echo x > "$T/up/lib/skip/b.txt"
git -C "$T/up/lib" add -A && git -C "$T/up/lib" commit -qm one
LIB1=$(git -C "$T/up/lib" rev-parse HEAD)
echo two > "$T/up/lib/keep/a.txt"
git -C "$T/up/lib" commit -qam two
LIB2=$(git -C "$T/up/lib" rev-parse HEAD)
mk webkit
mkdir -p "$T/up/webkit/Source"
printf 'line\n' > "$T/up/webkit/Source/f.c"
git -C "$T/up/webkit" add -A && git -C "$T/up/webkit" commit -qm wk
WK=$(git -C "$T/up/webkit" rev-parse HEAD)
mkdir -p "$T/patches"
cat > "$T/patches/0001-a.patch" <<'EOF'
--- a/Source/f.c
+++ b/Source/f.c
@@ -1 +1,2 @@
 line
+patched one
EOF
cat > "$T/patches/0002-b.patch" <<'EOF'
--- a/Source/f.c
+++ b/Source/f.c
@@ -1,2 +1,3 @@
 line
 patched one
+patched two
EOF

lock() { printf '%s\n' "# test" "" "$@" > "$T/lock"; }
run() { LOCK="$T/lock" PATCHES_DIR="$T/patches" sh "$FETCH" "$T/deps" > "$T/out" 2>&1; }

# The real lock parses, and the dry run names every input without fetching.
LOCK="$HERE/scripts/deps.lock" DRY_RUN=1 sh "$FETCH" "$T/dry" > "$T/dry.out" 2>&1
ok '[ $? -eq 0 ]' "the repository's deps.lock parses"
ok '[ ! -e "$T/dry" ]' "a dry run makes nothing"
ok '[ "$(wc -l < "$T/dry.out")" -eq "$(grep -c "^[a-z]" "$HERE/scripts/deps.lock")" ]' "a dry run lists each input once"
ok 'grep -q "^webkit $(cut -d" " -f2 "$HERE/webkit/UPSTREAM") " "$T/dry.out"' "deps.lock's WebKit pin is webkit/UPSTREAM's"

# A pinned, sparse checkout of an older commit, and WebKit with its patches.
lock "lib file://$T/up/lib $LIB1 sparse=keep  the library" "webkit file://$T/up/webkit $WK  the engine"
run; ok '[ $? -eq 0 ]' "fetch succeeds"
ok '[ "$(git -C "$T/deps/lib" rev-parse HEAD)" = "$LIB1" ]' "lib is at its pin, not at upstream's newest"
ok '[ "$(cat "$T/deps/lib/keep/a.txt")" = one ]' "the pinned commit's file"
ok '[ ! -e "$T/deps/lib/skip/b.txt" ]' "sparse= leaves the other paths out"
ok '[ "$(tail -1 "$T/deps/webkit/Source/f.c")" = "patched two" ]' "WebKit's patches applied, in number order"
ok '[ -s "$T/deps/webkit/.ob-patches" ]' "the patch stamp is written"

# A second run leaves both as they are.
run; ok '[ $? -eq 0 ] && grep -q "lib: at $LIB1 already" "$T/out" && grep -q "webkit: at $WK already" "$T/out"' "a re-run changes nothing"

# A changed sparse= list is applied to the checkout at its pin.
lock "lib file://$T/up/lib $LIB1 sparse=keep,skip  the library" "webkit file://$T/up/webkit $WK  the engine"
run; ok '[ $? -eq 0 ] && grep -q "lib: sparse paths now keep,skip" "$T/out" && [ -f "$T/deps/lib/skip/b.txt" ]' "a widened sparse= list is checked out"
run; ok '[ $? -eq 0 ] && ! grep -q "sparse paths now" "$T/out"' "and left alone on the next run"
lock "lib file://$T/up/lib $LIB1 sparse=keep  the library" "webkit file://$T/up/webkit $WK  the engine"
run; ok '[ $? -eq 0 ] && [ ! -e "$T/deps/lib/skip/b.txt" ] && [ "$(git -C "$T/deps/lib" rev-parse HEAD)" = "$LIB1" ]' "a narrowed one too, still at the pin"

# Refusals, each leaving the checkout untouched.
echo local > "$T/deps/lib/keep/a.txt"
run; ok '[ $? -ne 0 ] && grep -q "local changes" "$T/out"' "local changes are refused"
ok '[ "$(cat "$T/deps/lib/keep/a.txt")" = local ]' "and kept"
git -C "$T/deps/lib" checkout -q -- keep/a.txt
lock "lib file://$T/up/lib $LIB2  the library" "webkit file://$T/up/webkit $WK  the engine"
run; ok '[ $? -ne 0 ] && grep -q "not the pin" "$T/out"' "a checkout at another commit is refused"
echo "+ changed" >> "$T/patches/0002-b.patch"
lock "webkit file://$T/up/webkit $WK  the engine"
run; ok '[ $? -ne 0 ] && grep -q "patches are not" "$T/out"' "WebKit with other patches is refused"
rm -rf "$T/deps"
printf -- '--- a/Source/f.c\n+++ b/Source/f.c\n@@ -1 +1 @@\n-no such line\n+x\n' > "$T/patches/0003-c.patch"
lock "webkit file://$T/up/webkit $WK  the engine"
run; ok '[ $? -ne 0 ] && grep -q "0003-c.patch does not apply" "$T/out"' "a patch that does not apply stops the fetch"

# ONLY fetches the named inputs.
rm -rf "$T/deps"
lock "lib file://$T/up/lib $LIB2  the library" "other file://$T/up/lib $LIB1  another"
ONLY=other LOCK="$T/lock" PATCHES_DIR="$T/patches" sh "$FETCH" "$T/deps" > "$T/out" 2>&1
ok '[ $? -eq 0 ] && [ -d "$T/deps/other" ] && [ ! -e "$T/deps/lib" ]' "ONLY fetches just those"

# Bad lock files are refused before anything is fetched.
rm -rf "$T/deps"
for bad in "lib file://$T/up/lib abc123  short sha" "lib file://$T/up/lib" "lib file://$T/up/lib $LIB1 x
lib file://$T/up/lib $LIB1 y" "li/b file://$T/up/lib $LIB1 bad name"; do
    printf '%s\n' "$bad" > "$T/lock"
    run; ok '[ $? -ne 0 ] && [ ! -e "$T/deps" ]' "bad lock refused: $(printf '%s' "$bad" | head -1 | cut -c1-40)"
done
printf '# nothing\n' > "$T/lock"
run; ok '[ $? -ne 0 ] && grep -q "no inputs" "$T/out"' "an empty lock is refused"
LOCK="$T/none" sh "$FETCH" "$T/deps" > "$T/out" 2>&1
ok '[ $? -ne 0 ] && grep -q "no pin file" "$T/out"' "a missing lock is refused"

# An unknown commit is reported.
lock "lib file://$T/up/lib 0000000000000000000000000000000000000001  gone"
run; ok '[ $? -ne 0 ] && grep -q "cannot fetch" "$T/out"' "a commit the server lacks is reported"

echo "fetch-deps: $pass passed, $fail failed"
[ "$fail" -eq 0 ]
