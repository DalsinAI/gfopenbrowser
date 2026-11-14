#!/bin/sh
# GFOpenBrowser (DalsinAI/gfopenbrowser)
# Copyright (c) 2026 AmiCradle Team. MIT licence, see LICENSE.
#
# The GFOpenBrowser package from the repositories alone: every input checked
# out at its pin (scripts/deps.lock), every upstream tarball at its sha256
# (scripts/tarballs.lock), each library built by its own repository's
# script, then the engine and the package. No folder from an older machine
# is read, so any PC with the os32-gcc16 stove gives the same package.
#
#   build-pack.sh [STAGE...]          (default: all of them, in order)
#
#   PACK_DIR      the work folder: deps/, tarballs/, oab-deps/, libs/,
#                 build/, out/ and pack.log      (required)
#   OS32_GCC16    stove root                     (default ~/AmiCradle/stoves/os32-gcc16)
#   FONTS_FROM    where the fonts stage takes the Liberation and DejaVu
#                 .ttf files from (default the system's own,
#                 /usr/share/fonts/truetype/liberation and .../dejavu); each
#                 is checked against scripts/fonts.sha256
#   CA_FROM       Mozilla's CA list for the CA bundle (default
#                 /usr/share/ca-certificates/mozilla, Debian's package)
#   ICU_HOST_BUILD  a host ICU 78.3 build folder (ICU's cross build needs
#                 one; default PACK_DIR/icu-host, built here when missing)
#   TARBALLS_FROM an older tarballs folder to copy from (zlib.tar.gz has no
#                 public URL; see scripts/tarballs.lock)
#   JOBS          parallel jobs                  (default: CPU threads)
#   NICE          the niceness each stage runs at (default 10)
#   DRY_RUN       1: print each stage's commands, run nothing
#
# Stages, in order:
#   fetch      scripts/fetch-deps.sh into PACK_DIR/deps
#   tarballs   the upstream tarballs (tarballs.lock) into PACK_DIR/tarballs
#   fonts      the 14 fonts (fonts.sha256) into PACK_DIR/fonts, the OB_FONTS
#              of the engine and package stages
#   ca         gfopentls's tools/make_ca_bundle.py into PACK_DIR/ca
#   oab-deps   zlib and libpng (gfopenimage), SQLite (gfopensqlite), cairo
#              and pixman with user fonts (gfopencairo), OXP (gfopenxml),
#              ICU 78.3 (scripts/build-icu.sh), Unicode 17's database
#              (scripts/fetch-ucd.sh) and gfopenfont's SDK, into
#              PACK_DIR/oab-deps, the OAB_DEPS build-webcore.sh reads
#   libs       the Amiga libraries the package carries: gfopentls.library
#              and gfopencrypto.library, gfopenfont.library,
#              gfopengpu.library, gfopenlayout.library (with GFOpenGadTools'
#              theme), z.library, the stream, gzip, brotli, gif and png
#              datatypes, json.library and webbrowser.library, into
#              PACK_DIR/libs
#   engine     build-webcore.sh configure-engine, build-engine, jsimage and
#              build-browser, in PACK_DIR/build
#   package    scripts/package-browser.sh PACK_DIR/out
#
# Each stage's time and result go to PACK_DIR/pack.log; a stage that fails
# stops the build there, and running that stage again carries on.
set -eu

HERE=$(cd "$(dirname "$0")/.." && pwd)
STAGES="fetch tarballs fonts ca oab-deps libs engine package"
DRY_RUN=${DRY_RUN:-0}

die() { echo "build-pack: $*" >&2; exit 1; }

if [ "${1:-}" = --list ]; then
    for s in $STAGES; do echo "$s"; done
    exit 0
fi

: "${PACK_DIR:?set PACK_DIR to the work folder}"
GCC16=${OS32_GCC16:-"$HOME/AmiCradle/stoves/os32-gcc16"}
JOBS=${JOBS:-$( (nproc || sysctl -n hw.ncpu) 2>/dev/null || echo 2)}
NICE=${NICE:-10}
D="$PACK_DIR/deps"
TB="$PACK_DIR/tarballs"
OAB="$PACK_DIR/oab-deps"
L="$PACK_DIR/libs"
LOG="$PACK_DIR/pack.log"
OB_FONTS="$PACK_DIR/fonts"
FONTS_FROM=${FONTS_FROM:-"/usr/share/fonts/truetype/liberation /usr/share/fonts/truetype/dejavu"}
CA_FROM=${CA_FROM:-/usr/share/ca-certificates/mozilla}

for s in "$@"; do
    case " $STAGES " in *" $s "*) ;; *) die "no stage $s (stages: $STAGES)" ;; esac
done
[ $# -gt 0 ] || set -- $STAGES

# A command run niced, or printed for DRY_RUN.
run() {
    if [ "$DRY_RUN" = 1 ]; then echo "  $*"; return 0; fi
    nice -n "$NICE" "$@"
}

sha256() {
    if command -v sha256sum >/dev/null 2>&1; then sha256sum | cut -d' ' -f1
    else shasum -a 256 | cut -d' ' -f1; fi
}

need_stove() {
    [ "$DRY_RUN" = 1 ] || [ -x "$GCC16/prefix/bin/m68k-amigaos-gcc" ] ||
        die "no os32-gcc16 stove at $GCC16 (set OS32_GCC16)"
}

need_fonts() {
    [ "$DRY_RUN" = 1 ] || [ -f "$OB_FONTS/.checked" ] || die "no checked fonts in $OB_FONTS (run the fonts stage)"
}

# ---- stages ----

stage_fetch() {
    run sh "$HERE/scripts/fetch-deps.sh" "$D"
}

stage_tarballs() {
    run env TARBALLS_FROM="${TARBALLS_FROM:-}" sh "$HERE/scripts/fetch-tarballs.sh" "$TB"
}

# Each font in fonts.sha256 from the first FONTS_FROM folder that has it, at its sha256.
stage_fonts() {
    if [ "$DRY_RUN" = 1 ]; then echo "  fonts.sha256 from $FONTS_FROM -> $OB_FONTS"; return 0; fi
    rm -rf "$OB_FONTS" && mkdir -p "$OB_FONTS"
    grep -v '^#' "$HERE/scripts/fonts.sha256" | while read -r sum f; do
        [ -n "$f" ] || continue
        got=
        for d in $FONTS_FROM; do [ -f "$d/$f" ] && { got="$d/$f"; break; }; done
        [ -n "$got" ] || die "font $f is in none of: $FONTS_FROM"
        [ "$(sha256 < "$got")" = "$sum" ] || die "font $got is not the pinned one (fonts.sha256)"
        cp "$got" "$OB_FONTS/"
    done
    touch "$OB_FONTS/.checked"
}

stage_ca() {
    run python3 "$D/gfopentls/tools/make_ca_bundle.py" "$PACK_DIR/ca" --source "$CA_FROM"
}

stage_oab_deps() {
    need_stove
    P="$OAB/m68k-amigaos"
    run mkdir -p "$P"
    run env OS32_GCC16="$GCC16" PREFIX="$P" TARBALLS="$TB" JOBS="$JOBS" AC_HELPERS=0 sh "$D/gfopenimage/build.sh"
    run env OS32_GCC16="$GCC16" PREFIX="$P" TARBALLS="$TB" JOBS="$JOBS" sh "$D/gfopensqlite/build.sh"
    run env OS32_GCC16="$GCC16" PREFIX="$P" TARBALLS="$TB" JOBS="$JOBS" CAIRO_FONTS=user DEPS_PREFIX="$P" \
        sh "$D/gfopencairo/build.sh"
    run env OS32_GCC16="$GCC16" OUT="$PACK_DIR/oxp" sh "$D/gfopenxml/oxp/build-amiga.sh"
    run sh -c "mkdir -p '$P/include' '$P/lib' && cp '$PACK_DIR/oxp/lib/liboxp.a' '$P/lib/' && cp -R '$PACK_DIR'/oxp/include/. '$P/include/'"
    run sh -c "cp -R '$D'/gfopenfont/sdk/Include_H/. '$P/include/'"
    # ICU: the sources from the tarball with our patch, a host build for the cross build, then ours.
    run sh -c "rm -rf '$PACK_DIR/icu-src' && mkdir -p '$PACK_DIR/icu-src' && tar -xzf '$TB/icu4c-78.3-sources.tgz' -C '$PACK_DIR/icu-src' && cd '$PACK_DIR/icu-src/icu' && patch -p1 < '$HERE/icu/patches/0001-icu78-amigaos3.patch'"
    HOSTICU=${ICU_HOST_BUILD:-"$PACK_DIR/icu-host"}
    if [ "$DRY_RUN" = 1 ] || [ ! -f "$HOSTICU/config/icucross.mk" ]; then
        run sh -c "rm -rf '$HOSTICU' && mkdir -p '$HOSTICU' && cd '$HOSTICU' && '$PACK_DIR/icu-src/icu/source/configure' --disable-tests --disable-samples && make -j$JOBS"
    fi
    # the Unicode 17 database unicode.library's own code is made from (pinned in fetch-ucd.sh)
    [ "$DRY_RUN" != 1 ] && [ -f "$OAB/ucd-17.0.0/UnicodeData.txt" ] ||
        run sh "$HERE/scripts/fetch-ucd.sh" "$OAB/ucd-17.0.0"
    run env ICU_SRC="$PACK_DIR/icu-src/icu" ICU_HOST_BUILD="$HOSTICU" OS32_GCC16="$GCC16" OAB_DEPS="$OAB" JOBS="$JOBS" \
        sh "$HERE/scripts/build-icu.sh"
}

stage_libs() {
    need_stove
    run mkdir -p "$L"
    run env STOVE="$GCC16" OUT="$L/gfopencrypto" sh "$D/gfopentls/build-amiga.sh"
    run env STOVE="$GCC16" OUT="$L/gfopentls" sh "$D/gfopentls/build-amiga-tls.sh"
    run sh -c "mkdir -p '$L/opentls' && cp '$L/gfopentls/lib/gfopentls.library' '$L/gfopencrypto/lib/gfopencrypto.library' '$L/opentls/'"
    run env NDK="$D/gfopensdk/OpenNDK/Include_H" OPENGPU_INCLUDE="$D/gfopensdk/Include" \
        sh "$D/gfopenfont/amiga/build-library.sh" "$L/gfopenfont"
    run env STOVE="$GCC16" OUT="$L/gfopengpu" sh "$D/gfopenrtg/library/opengpu/build-amiga.sh"
    run env OS32_GCC16="$GCC16" OGT_ROOT="$D/amicradle-guest/libraries/opengadtools" OUT="$L/gfopenlayout" \
        sh "$D/gfopenlayout/build-library.sh"
    run env OS32_GCC16="$GCC16" OUT="$L/openzlib" sh "$D/amicradle-guest/libraries/openzlib/build-amiga.sh"
    run env OS32_GCC16="$GCC16" PREFIX="$L/datatypes" sh "$D/gfopendatatypes/Datatypes/stream/build.sh"
    run env OS32_GCC16="$GCC16" PREFIX="$L/datatypes" sh "$D/gfopendatatypes/Datatypes/gif/build.sh"
    run env OS32_GCC16="$GCC16" PREFIX="$L/datatypes" sh "$D/gfopendatatypes/Datatypes/png/build.sh"
    run env OS32_GCC16="$GCC16" sh "$HERE/scripts/build-json-library.sh" "$L/json-library"
    run env OS32_GCC16="$GCC16" sh "$HERE/src/webbrowser/build-library.sh"
}

engine_env() {
    echo WEBKIT_SOURCE="$D/webkit" WEBKIT_BUILD="$PACK_DIR/build" OS32_GCC16="$GCC16" OAB_DEPS="$OAB" \
        OPENSERVICE_DIR="$D/gfopenservice" OPENDATATYPES_DIR="$D/gfopendatatypes" \
        OPENAMIGARTG_DIR="$D/gfopenrtg" GFOPENLAYOUT_DIR="$D/gfopenlayout" \
        NDK_INCLUDE="$D/gfopensdk/OpenNDK/Include_H" OB_FONTS="$OB_FONTS" JOBS="$JOBS"
}

stage_engine() {
    need_stove
    need_fonts
    for c in configure-engine build-engine jsimage build-browser; do
        run env $(engine_env) sh "$HERE/scripts/build-webcore.sh" "$c"
    done
}

stage_package() {
    need_fonts
    run env $(engine_env) OPENTLS_LIBS="$L/opentls" CA_BUNDLE="$PACK_DIR/ca" \
        OPENFONT_LIB="$L/gfopenfont/gfopenfont.library" OPENFONT_SRC="$D/gfopenfont" \
        OPENGPU_LIB="$L/gfopengpu/libs/gfopengpu.library" OPENLAYOUT_LIB="$L/gfopenlayout/lib/gfopenlayout.library" \
        DATATYPES_DIR="$L/datatypes" ZLIB_LIB="$L/openzlib/libs/z.library" JSON_LIB="$L/json-library/json.library" \
        UNICODE_LIB="$PACK_DIR/build/unicode-library/unicode.library" \
        sh "$HERE/scripts/package-browser.sh" "$PACK_DIR/out"
}

[ "$DRY_RUN" = 1 ] || mkdir -p "$PACK_DIR"
for s in "$@"; do
    echo "== $s"
    start=$(date +%s)
    # set -e does not hold inside a function called from || or if, so each
    # stage runs in a subshell of its own with set -e, and its status is read after.
    set +e
    ( set -e; "stage_$(echo "$s" | tr - _)" )
    rc=$?
    set -e
    [ "$DRY_RUN" = 1 ] && continue
    echo "$(date -u +%Y-%m-%dT%H:%M:%SZ) $s: $(( $(date +%s) - start )) s, exit $rc" >> "$LOG"
    [ "$rc" -eq 0 ] || die "stage $s failed (exit $rc); see $LOG, fix it, then: build-pack.sh $s"
done
