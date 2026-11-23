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
# unzip, which the kitchen lacks (gfopensqlite unpacks SQLite's zip with it): Python's zipfile does the same
mkdir -p "$PACK_DIR/bin"
if ! command -v unzip >/dev/null 2>&1; then
    printf '%s\n' '#!/usr/bin/env python3' 'import sys, zipfile' \
        'a = [x for x in sys.argv[1:] if not x.startswith("-")]' 'zipfile.ZipFile(a[0]).extractall(".")' > "$PACK_DIR/bin/unzip"
    chmod +x "$PACK_DIR/bin/unzip"
fi
export PATH="$PACK_DIR/bin:$PATH"
# the engine stage's tools, said at the start rather than an hour in
missing=
for t in cmake ninja perl ruby gperf python3 make patch g++; do command -v "$t" >/dev/null 2>&1 || missing="$missing $t"; done
[ -z "$missing" ] || echo "build-rc: WARNING: not on this machine:$missing (the engine stage needs them)"
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
# the fonts at fonts.sha256's pins (this machine's DejaVu is another build): the media repository's copy
FT=/srv/samsung/media-repository/warehouse/sources/gfopenbrowser-fonts.tar
if [ -z "${FONTS_FROM:-}" ] && [ -f "$FT" ]; then
    rm -rf "$PACK_DIR/fonts-from"; mkdir -p "$PACK_DIR/fonts-from"; tar -xf "$FT" -C "$PACK_DIR/fonts-from"
    export FONTS_FROM="$PACK_DIR/fonts-from"
fi
# the earlier try's hand-fed checkouts: fetched again from the mirror
for n in gfopengadtools amicradle-guest; do
    if [ -f "$PACK_DIR/deps/$n/.git/FETCH_HEAD" ] && grep -q bundle "$PACK_DIR/deps/$n/.git/FETCH_HEAD"; then rm -rf "$PACK_DIR/deps/$n"; fi
done
# gperf (WebKit's generators need it) when this machine has none: GNU gperf 3.1 built here, for the build only
if ! command -v gperf >/dev/null 2>&1; then
    G="$PACK_DIR/gperf-3.1"
    if [ ! -x "$PACK_DIR/bin/gperf" ]; then
        curl -fsSL -o "$PACK_DIR/gperf-3.1.tar.gz" https://ftp.gnu.org/pub/gnu/gperf/gperf-3.1.tar.gz
        echo "588546b945bba4b70b6a3a616e80b4ab466e3f33024a352fc2198112cdbb3ae2  $PACK_DIR/gperf-3.1.tar.gz" | sha256sum -c -
        rm -rf "$G"; tar -xzf "$PACK_DIR/gperf-3.1.tar.gz" -C "$PACK_DIR"
        (cd "$G" && ./configure --prefix="$PACK_DIR/gperf" >/dev/null && make -j"$(nproc)" >/dev/null && make install >/dev/null)
        ln -sf "$PACK_DIR/gperf/bin/gperf" "$PACK_DIR/bin/gperf"
    fi
    echo "build-rc: gperf $("$PACK_DIR/bin/gperf" --version | head -1)"
fi
command -v ruby >/dev/null 2>&1 || echo "build-rc: WARNING: no ruby on this machine (WebKit's offlineasm needs it)"
# only the stages not yet done (build-pack.sh logs each one's exit in pack.log); RC_STAGES overrides
STAGES=${RC_STAGES:-}
if [ -z "$STAGES" ]; then
    for st in fetch tarballs fonts ca oab-deps libs; do
        if [ -f "$PACK_DIR/pack.log" ] && grep -q " $st: [0-9]* s, exit 0$" "$PACK_DIR/pack.log"; then :; else STAGES="$STAGES $st"; fi
    done
    # the engine and the package always: their sources are what changes between tries, and the
    # engine's build is incremental (ninja, ccache)
    STAGES="$STAGES engine package"
fi
echo "build-rc: stages:$STAGES"
sh "$HERE/scripts/build-pack.sh" $STAGES
mkdir -p "$HERE/out"
NAME=${RC_NAME:-GFOpenBrowser-0.9.0-RC5-trace}
rm -rf "$HERE/out/$NAME"; cp -R "$PACK_DIR/out" "$HERE/out/$NAME"
(cd "$HERE/out" && tar czf "$NAME.tgz" "$NAME")
if [ -f "$PACK_DIR/out.lha" ]; then cp "$PACK_DIR/out.lha" "$HERE/out/$NAME.lha"; fi
ls -la "$HERE/out"
RC_UPLOAD=${RC_UPLOAD:-http://192.168.0.195:18991}
for f in "$HERE/out/$NAME.tgz" "$HERE/out/$NAME.lha"; do
    if [ -f "$f" ]; then curl -fsS -m 600 -T "$f" "$RC_UPLOAD/$(basename "$f")" && echo "sent $(basename "$f")" || echo "could not send $(basename "$f")"; fi
done
