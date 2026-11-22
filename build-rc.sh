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
