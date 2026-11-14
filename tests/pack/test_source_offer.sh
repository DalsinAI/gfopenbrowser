#!/bin/sh
# GFOpenBrowser (DalsinAI/gfopenbrowser)
# Copyright (c) 2026 AmiCradle Team. MIT licence, see LICENSE.
#
# Host test of scripts/write-source-offer.sh and dist/licences: SOURCE.txt
# names this commit, WebKit's pin and its patch count, the LGPL programs and
# every licence file; a tree with changes is refused or marked; the licence
# texts the package carries are all there.
#   sh tests/pack/test_source_offer.sh
set -u
HERE=$(cd "$(dirname "$0")/../.." && pwd)
W="$HERE/scripts/write-source-offer.sh"
T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT
pass=0 fail=0
ok() { if eval "$1"; then pass=$((pass + 1)); else fail=$((fail + 1)); echo "FAIL: $2"; fi; }

C=0123456789abcdef0123456789abcdef01234567
WK=$(cut -d' ' -f2 "$HERE/webkit/UPSTREAM")
N=$(ls "$HERE"/webkit/patches/*.patch | wc -l | tr -d ' ')
OB_COMMIT=$C sh "$W" "$T/S.txt" > "$T/out" 2>&1
ok '[ $? -eq 0 ]' "writes SOURCE.txt"
ok 'grep -q "commit $C" "$T/S.txt" && grep -q "tree/$C" "$T/S.txt"' "names this repository's commit"
ok 'grep -q "commit $WK," "$T/S.txt"' "names WebKit's pin (webkit/UPSTREAM)"
ok 'grep -q "the $N patches" "$T/S.txt"' "counts webkit/patches"
ok 'grep -q "Libs/WebBrowser/WebBrowserEngine" "$T/S.txt" && grep -q "Libs/JavaScript/JavaScriptEngine" "$T/S.txt"' "names both LGPL programs"
ok 'grep -q "GFOpenBrowser 0\." "$T/S.txt"' "carries the readme's version"
ok '! grep -q "NOT A RELEASE" "$T/S.txt"' "a commit given is not marked"
for f in $(sed -n 's|^  Licences/\([^ ]*\).*|\1|p' "$T/S.txt") LGPL-2.txt LGPL-2.1.txt; do
    ok '[ -s "$HERE/dist/licences/$f" ]' "dist/licences/$f is there"
done
ok 'grep -q "GNU LIBRARY GENERAL PUBLIC LICENSE" "$HERE/dist/licences/LGPL-2.txt"' "LGPL-2 is the LGPL 2 text"
ok 'grep -q "Version 2.1" "$HERE/dist/licences/LGPL-2.1.txt"' "LGPL-2.1 is the 2.1 text"
ok 'grep -q "SIL OPEN FONT LICENSE Version 1.1" "$HERE/dist/licences/Liberation-OFL-1.1.txt"' "Liberation's OFL"
ok 'grep -qi "Bitstream" "$HERE/dist/licences/DejaVu-LICENSE.txt"' "DejaVu's licence"
ok 'grep -q "UNICODE LICENSE V3" "$HERE/dist/licences/ICU-LICENSE.txt"' "ICU's licence"
ok 'grep -q "write-source-offer.sh" "$HERE/scripts/package-browser.sh" && grep -q "dist/licences" "$HERE/scripts/package-browser.sh"' "package-browser.sh carries both"

# A copy of the repository with a local change: refused, or marked.
git clone -q "$HERE" "$T/r" 2>/dev/null
cp "$W" "$T/r/scripts/"; git -C "$T/r" add -A >/dev/null 2>&1
git -C "$T/r" -c user.name=t -c user.email=t@t commit -qm t >/dev/null 2>&1
echo x >> "$T/r/openbrowser.readme"
sh "$T/r/scripts/write-source-offer.sh" "$T/D.txt" > "$T/out" 2>&1
ok '[ $? -ne 0 ] && grep -q "uncommitted changes" "$T/out" && [ ! -e "$T/D.txt" ]' "a changed tree is refused"
OB_ALLOW_DIRTY=1 sh "$T/r/scripts/write-source-offer.sh" "$T/D.txt" > "$T/out" 2>&1
ok '[ $? -eq 0 ] && grep -q "NOT A RELEASE" "$T/D.txt" && grep -q "commit $(git -C "$T/r" rev-parse HEAD)" "$T/D.txt"' "OB_ALLOW_DIRTY marks it"
sh "$W" > "$T/out" 2>&1
ok '[ $? -ne 0 ] && grep -q usage "$T/out"' "needs a FILE"

echo "source-offer: $pass passed, $fail failed"
[ "$fail" -eq 0 ]
