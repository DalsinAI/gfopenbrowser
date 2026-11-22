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
# GitHub's addresses answered by bigguy's git mirror (no login; the nightly clones the same way)
export GIT_CONFIG_COUNT=1 GIT_CONFIG_KEY_0="url.${RC_MIRROR:-http://127.0.0.1:8418}/DalsinAI/.insteadOf" GIT_CONFIG_VALUE_0=https://github.com/DalsinAI/
# zlib.tar.gz has no public URL (scripts/tarballs.lock): the media repository's copy, sources/zlib.tar.gz
if [ -z "${TARBALLS_FROM:-}" ]; then
    for z in /srv/samsung/media-repository/warehouse/sources/zlib.tar.gz "$STOVES"/*/ports/zlib.tar.gz "$STOVES"/archive/*/ports/zlib.tar.gz; do
        if [ -f "$z" ]; then export TARBALLS_FROM=$(dirname "$z"); break; fi
    done
    if [ -z "${TARBALLS_FROM:-}" ]; then
        mkdir -p "$PACK_DIR/tarballs-from"
        curl -fsS -o "$PACK_DIR/tarballs-from/zlib.tar.gz" "${RC_BUNDLES:-http://192.168.0.195:18977}/zlib.tar.gz" && export TARBALLS_FROM="$PACK_DIR/tarballs-from"
    fi
    echo "build-rc: TARBALLS_FROM=${TARBALLS_FROM:-none}"
fi
# the earlier try's hand-fed checkouts: fetched again from the mirror
for n in gfopengadtools amicradle-guest; do
    if [ -f "$PACK_DIR/deps/$n/.git/FETCH_HEAD" ] && grep -q bundle "$PACK_DIR/deps/$n/.git/FETCH_HEAD"; then rm -rf "$PACK_DIR/deps/$n"; fi
done
sh "$HERE/scripts/build-pack.sh"
mkdir -p "$HERE/out"
NAME=GFOpenBrowser-0.9.0-RC5
rm -rf "$HERE/out/$NAME"; cp -R "$PACK_DIR/out" "$HERE/out/$NAME"
(cd "$HERE/out" && tar czf "$NAME.tgz" "$NAME")
if [ -f "$PACK_DIR/out.lha" ]; then cp "$PACK_DIR/out.lha" "$HERE/out/$NAME.lha"; fi
ls -la "$HERE/out"
RC_UPLOAD=${RC_UPLOAD:-http://192.168.0.195:18991}
for f in "$HERE/out/$NAME.tgz" "$HERE/out/$NAME.lha"; do
    if [ -f "$f" ]; then curl -fsS -m 600 -T "$f" "$RC_UPLOAD/$(basename "$f")" && echo "sent $(basename "$f")" || echo "could not send $(basename "$f")"; fi
done
