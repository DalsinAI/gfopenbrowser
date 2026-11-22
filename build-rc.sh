#!/bin/sh
# Team 5's kitchen build of a release candidate (not for main): scripts/build-pack.sh
# with every stage, in the kitchen's os32-gcc16 stove, the work folder beside this
# file; the package then sent to the PC that asked for it (RC_UPLOAD below).
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
[ -n "${STOVES:-}" ] || if [ -d "$HOME/AmiCradle/kitchen/stoves" ]; then STOVES=$HOME/AmiCradle/kitchen/stoves; else STOVES=$HOME/AmiCradle/stoves; fi
export OS32_GCC16=${OS32_GCC16:-"$STOVES/os32-gcc16"}
export PACK_DIR=${PACK_DIR:-"$HERE/pack"}
export NICE=${NICE:-15}
echo "build-rc: stove $OS32_GCC16, work folder $PACK_DIR, $(nproc) threads"
# the two inputs GitHub will not give without a login: their pinned commits as bundles from the PC
# that asked (RC_BUNDLES), checked out where fetch-deps.sh looks, so it finds them at their pins
RC_BUNDLES=${RC_BUNDLES:-http://192.168.0.195:18977}
for nb in gfopengadtools:gfopengadtools-9aaa8d59.bundle amicradle-guest:amicradle-guest-92494b82.bundle; do
    n=${nb%%:*}; b=${nb#*:}; d="$PACK_DIR/deps/$n"
    pin=$(awk -v n="$n" '$1 == n {print $3}' "$HERE/scripts/deps.lock")
    [ "$(git -C "$d" rev-parse HEAD 2>/dev/null)" = "$pin" ] && continue
    rm -rf "$d"                                   # a fetch that failed before (this build's own folder)
    mkdir -p "$d"; curl -fsS -o "$PACK_DIR/$b" "$RC_BUNDLES/$b"
    git -C "$d" init -q; git -C "$d" fetch -q "$PACK_DIR/$b" "$pin" 2>/dev/null || git -C "$d" fetch -q "$PACK_DIR/$b" refs/heads/fetched
    git -C "$d" -c advice.detachedHead=false checkout -q "$pin"
    echo "build-rc: $n at $pin from $b"
done
sh "$HERE/scripts/build-pack.sh"
mkdir -p "$HERE/out"
NAME=GFOpenBrowser-0.9.0-RC5
rm -rf "$HERE/out/$NAME"; cp -R "$PACK_DIR/out" "$HERE/out/$NAME"
(cd "$HERE/out" && tar czf "$NAME.tgz" "$NAME")
[ -f "$PACK_DIR/out.lha" ] && cp "$PACK_DIR/out.lha" "$HERE/out/$NAME.lha"
ls -la "$HERE/out"
RC_UPLOAD=${RC_UPLOAD:-http://192.168.0.195:18991}
for f in "$HERE/out/$NAME.tgz" "$HERE/out/$NAME.lha"; do
    [ -f "$f" ] && { curl -fsS -m 600 -T "$f" "$RC_UPLOAD/$(basename "$f")" && echo "sent $(basename "$f")" || echo "could not send $(basename "$f")"; }
done
