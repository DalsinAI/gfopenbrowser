#!/bin/sh
# GFOpenBrowser (DalsinAI/gfopenbrowser)
# Copyright (c) 2026 AmiCradle Team. MIT licence, see LICENSE.
#
# GFOpenBrowser's drawer and archive, ready to copy to an Amiga and install
# with its own Install script (dist/GFOpenBrowser/Install).
#
#   package-browser.sh OUT_DIR
#
# Environment:
#   WEBKIT_BUILD   the WebCore build folder (scripts/build-webcore.sh); its
#                  bin/ holds GFOpenBrowser (the launcher) and WebBrowserEngine
#                  (build-browser)
#   OS32_GCC16     the os32-gcc16 stove (for m68k-amigaos-strip)
#   OAB_DEPS       the dependency folder; ICU's data file comes from
#                  icu78-m68k-amigaos/share/icudt78b.dat (ICU_DATA overrides)
#   OB_FONTS       the Liberation and DejaVu .ttf files
#   OPENTLS_LIBS   folder with gfopentls.library and gfopencrypto.library
#                  (gfopentls build)
#   CA_BUNDLE      folder with ca-bundle.pem and ca-bundle.LICENSE
#                  (gfopentls; S:GFOpenTLS/ on an installed system)
#   WEBBROWSER_LIB webbrowser.library 1.1 or newer (default
#                  src/webbrowser/bin/, where src/webbrowser/build-library.sh
#                  puts it). Required: the launcher opens the browser through
#                  it, from LIBS: or, before Install, from the drawer's Libs
#   JSON_LIB       json.library (default build/json-library/, scripts/build-json-library.sh);
#                  the Installer puts it in LIBS:. Required: the engine's
#                  JSON.parse parses through it (WebKit patch 0058)
#   OPENFONT_LIB   gfopenfont.library 1.4 or newer (gfopenfont's
#                  build/amiga/gfopenfont.library); the Installer puts it in
#                  LIBS:. Required: the engine draws and shapes all its text
#                  with it (WebKit patch 0090)
#   OPENFONT_SRC   gfopenfont's checkout (default: the one OPENFONT_LIB was
#                  built in, build/amiga/../..); its fonts/cjk/fetch-cjk-font.sh fetches
#                  gfopenfont's CJK face, which the package carries in Fonts/
#                  and the Installer puts in FONTS: for every program
#   OPENGPU_LIB    gfopengpu.library 0.24 or newer (gfopenrtg's
#                  library/opengpu build); the Installer puts it in LIBS:.
#                  Required: the window is painted through it (WebKit
#                  patches 0076-0082b); without it the engine paints with cairo
#   OPENLAYOUT_LIB gfopenlayout.library 11 or newer (gfopenlayout's
#                  build-library.sh build); the Installer puts it in LIBS:.
#                  Required: the window's dialogs, find bar, downloads and
#                  preferences are drawn by it (without it, alerts are
#                  EasyRequests and the rest say they need it)
#   UNICODE_LIB    unicode.library (default WEBKIT_BUILD/unicode-library/,
#                  where build-webcore.sh makes it); the Installer puts it
#                  in LIBS:. Required when the engine was built on it
#                  (OB_ICU_DATA=library, the default): ICU's code is there
#   DATATYPES_DIR  the datatypes stream class's build (gfopendatatypes'
#                  Datatypes/stream/build.sh PREFIX): Classes/DataTypes/
#                  stream.datatype, gzip.datatype and brotli.datatype, and
#                  Devs/DataTypes/GZip and Brotli. Required: openweb.fetch's
#                  native transport decodes compressed pages through them
#                  (WebKit patch 0075); the Installer puts them in
#                  SYS:Classes/DataTypes and DEVS:DataTypes. With
#                  gfopengif.datatype and Devs/DataTypes/GFOpenGIF there too
#                  (Datatypes/gif/build.sh), animated GIFs play (patch 0110);
#                  with gfopenpng.datatype and Devs/DataTypes/GFOpenPNG
#                  (Datatypes/png/build.sh), pages can write PNG (patch 0074)
#   ZLIB_LIB       z.library 53.12 or newer (GFOpenZlib), which gzip.datatype
#                  opens. Required; the Installer puts it in LIBS:
#   OB_JSIMAGE     1 (the default when the WebCore build has one): the engine
#                  is the jsimage client (build-webcore.sh jsimage), which
#                  takes JavaScriptCore from JavaScriptEngine, packed beside
#                  it as Libs/JavaScript/JavaScriptEngine. 0: the engine
#                  with its own JavaScriptCore (bin/WebBrowserEngine)
#   JS_LIB_DIR     scripts/build-javascript-library.sh's output folder
#                  (default build/javascript-library): javascript.library
#                  and JS, packed with a jsimage engine, whose
#                  JavaScriptEngine is the one the library runs. The
#                  Installer puts them in LIBS: and C: when missing or older
#
# OUT_DIR/GFOpenBrowser/            the drawer: GFOpenBrowser (the launcher),
#                                 Libs/ (webbrowser.library, GFOpenTLS,
#                                 json.library, gfopenfont.library,
#                                 gfopengpu.library, gfopenlayout.library,
#                                 unicode.library, z.library and WebBrowser/: the engine with Fonts/,
#                                 Tata.ads, Unicode/ and Network/, and
#                                 with a jsimage engine JavaScript/
#                                 JavaScriptEngine and javascript.library),
#                                 C/JS (with a jsimage engine),
#                                 Classes/DataTypes/ and Devs/DataTypes/
#                                 (stream, gzip and brotli), S/GFOpenTLS/,
#                                 Install, icons and readme
# OUT_DIR/GFOpenBrowser.info        the drawer's icon
# OUT_DIR/Install-GFOpenBrowser     the Installer script (SYS:System/Installer),
# OUT_DIR/Install-GFOpenBrowser.info  and its icon: double-click to install
# OUT_DIR.lha                     the archive, beside OUT_DIR
#
# One engine: WebBrowserEngine, which webbrowser.library starts and which has
# GFOpenBrowser's window. The library runs LIBS:WebBrowser/WebBrowserEngine
# when there is one, otherwise the drawer's Libs/WebBrowser/WebBrowserEngine
# (PROGDIR: of the launcher); Install keeps the engine in the drawer and
# renames an older LIBS:WebBrowser/WebBrowserEngine. The engine reads its
# files from its own drawer (PROGDIR: of the engine, Libs/WebBrowser):
# Fonts/ (drawn by gfopenfont.library), Tata.ads (Tata's ad and tracker list when Tata is not installed), the Public Suffix List (Network/
# public-suffixes, WebKit patch 0070) and ICU's data (OB_ICU_DATA=file) from
# LOCALE:Unicode/ or else Unicode/, so the drawer carries one copy of each
# there; the Installer also puts the ICU data in LOCALE:Unicode/.
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
OUT=${1:?usage: package-browser.sh OUT_DIR}
: "${WEBKIT_BUILD:?set WEBKIT_BUILD}" "${OS32_GCC16:?set OS32_GCC16}" "${OB_FONTS:?set OB_FONTS}"
: "${OPENTLS_LIBS:?set OPENTLS_LIBS}" "${CA_BUNDLE:?set CA_BUNDLE}"
ICU_DATA=${ICU_DATA:-"${OAB_DEPS:?set OAB_DEPS or ICU_DATA}/icu78-m68k-amigaos/share/icudt78b.dat"}
STRIP="$OS32_GCC16/prefix/bin/m68k-amigaos-strip"
BIN="$WEBKIT_BUILD/bin"
WBLIB=${WEBBROWSER_LIB:-"$HERE/src/webbrowser/bin/webbrowser.library"}
for f in "$BIN/GFOpenBrowser" "$BIN/WebBrowserEngine" "$ICU_DATA" "$WBLIB" \
         "$OPENTLS_LIBS/gfopentls.library" "$OPENTLS_LIBS/gfopencrypto.library" "$CA_BUNDLE/ca-bundle.pem"; do
    [ -f "$f" ] || { echo "missing: $f" >&2; exit 1; }
done
# gfopencrypto.library 2.2 or newer: the engine takes its SHA digests from
# it (src/webcore/ob_digest.c), and with an older one every digest is empty
CRYPTOVER=$(LC_ALL=C grep -a -o '\$VER: gfopencrypto.library [0-9]*\.[0-9]*' "$OPENTLS_LIBS/gfopencrypto.library" | head -1 | sed 's/.* //')
case "$CRYPTOVER" in
    ''|0.*|1.*|2.0|2.1) echo "package-browser.sh: $OPENTLS_LIBS/gfopencrypto.library is ${CRYPTOVER:-of no version this can read}; the engine needs 2.2 or newer" >&2; exit 1 ;;
esac
# webbrowser.library not older than this tree's source: WEBBROWSER_LIB may
# name a library built earlier, and an old one fails quietly (with 1.1 the
# RUNSCRIPT=NO tool type does nothing, the switch came in 1.2)
WBWANT=$(sed -n 's/.*\$VER: webbrowser.library \([0-9]*\.[0-9]*\).*/\1/p' "$HERE/src/webbrowser/wb_library.c" | head -1)
WBVER=$(LC_ALL=C grep -a -o '\$VER: webbrowser.library [0-9]*\.[0-9]*' "$WBLIB" | head -1 | sed 's/.* //')
case "$WBWANT" in
    *.*) ;;
    *) echo "package-browser.sh: src/webbrowser/wb_library.c gives no version" >&2; exit 1 ;;
esac
case "$WBVER" in
    *.*) ;;
    *) echo "package-browser.sh: $WBLIB is of no version this can read; the package needs webbrowser.library $WBWANT or newer" >&2; exit 1 ;;
esac
if [ "${WBVER%.*}" -lt "${WBWANT%.*}" ] || { [ "${WBVER%.*}" -eq "${WBWANT%.*}" ] && [ "${WBVER#*.}" -lt "${WBWANT#*.}" ]; }; then
    echo "package-browser.sh: $WBLIB is $WBVER; the package needs webbrowser.library $WBWANT or newer (build src/webbrowser again)" >&2; exit 1
fi
rm -rf "$OUT"; mkdir -p "$OUT"
D="$OUT/GFOpenBrowser"; E="$D/Libs/WebBrowser"
mkdir -p "$D/S/GFOpenTLS" "$E/Fonts" "$E/Unicode"
"$STRIP" -o "$D/GFOpenBrowser" "$BIN/GFOpenBrowser"
# The jsimage client is a relink of bin/WebBrowserEngine: older than it, it
# is from an earlier build
JSI="$WEBKIT_BUILD/jsimage"
if [ "${OB_JSIMAGE:-}" != 0 ] && [ -f "$JSI/WebBrowserEngine/WebBrowserEngine" ] \
        && ! [ "$JSI/WebBrowserEngine/WebBrowserEngine" -ot "$BIN/WebBrowserEngine" ]; then
    [ -f "$JSI/JavaScriptEngine" ] || { echo "missing: $JSI/JavaScriptEngine (the client's image)" >&2; exit 1; }
    # the engine looks in LIBS:JavaScript/ first (where the Installer puts
    # it with javascript.library), then at PROGDIR:/JavaScript/
    # (src/jslib/jsimage_client.c)
    mkdir -p "$D/Libs/JavaScript"
    "$STRIP" -o "$E/WebBrowserEngine" "$JSI/WebBrowserEngine/WebBrowserEngine"
    "$STRIP" -o "$D/Libs/JavaScript/JavaScriptEngine" "$JSI/JavaScriptEngine"
    echo "engine: jsimage client, with Libs/JavaScript/JavaScriptEngine"
    # javascript.library (JavaScript for any program) and JS, its Shell
    # command: the library runs the same JavaScriptEngine, so the package
    # has all it needs. The Installer puts them in LIBS: and C:
    JSLIBDIR=${JS_LIB_DIR:-"$HERE/build/javascript-library"}
    for f in javascript.library JS; do
        [ -f "$JSLIBDIR/$f" ] || { echo "package-browser.sh: no $JSLIBDIR/$f (scripts/build-javascript-library.sh, or set JS_LIB_DIR)" >&2; exit 2; }
    done
    mkdir -p "$D/C"
    cp "$JSLIBDIR/javascript.library" "$D/Libs/javascript.library"
    cp "$JSLIBDIR/JS" "$D/C/JS"
elif [ "${OB_JSIMAGE:-}" = 1 ]; then
    echo "OB_JSIMAGE=1, but $JSI has no current client (build-webcore.sh jsimage)" >&2; exit 1
else
    "$STRIP" -o "$E/WebBrowserEngine" "$BIN/WebBrowserEngine"
    echo "engine: with its own JavaScriptCore; javascript.library is not packed (it needs the jsimage JavaScriptEngine)"
fi
cp "$WBLIB" "$D/Libs/"
cp "$OB_FONTS"/*.ttf "$E/Fonts/"
cp "$HERE/src/webcore/Tata.ads" "$E/"
# the toolbar's pictures: the Chrome icon set's strips, which GFOpenGadTools
# reads from PROGDIR:Images/Toolbar/ (then SYS:Prefs/Presets/Toolbar/). They
# are built from gfopengadtools' sources (python3 with Pillow, numpy and
# librsvg); without them the buttons show GFOpenGadTools' drawn icons, as
# they do on a four-colour screen. OB_TOOLBAR_STRIPS names a folder of built
# strips to take instead; OB_TOOLBAR_STRIPS=0 packs none.
OGT=${GFOPENGADTOOLS_DIR:-"$HERE/../gfopengadtools"}
STRIPS=${OB_TOOLBAR_STRIPS:-}
if [ "$STRIPS" = 0 ]; then
    echo "toolbar: no icon strips packed (OB_TOOLBAR_STRIPS=0); the buttons show the drawn icons"
else
    if [ -z "$STRIPS" ]; then
        [ -f "$OGT/tools/iconset/make_iconset.py" ] || { echo "package-browser.sh: no tools/iconset/make_iconset.py in $OGT (set GFOPENGADTOOLS_DIR to gfopengadtools' checkout, or OB_TOOLBAR_STRIPS)" >&2; exit 2; }
        STRIPS="$OUT/toolbar-strips"
        rm -rf "$STRIPS"
        python3 "$OGT/tools/iconset/make_iconset.py" --out "$STRIPS" >/dev/null ||
            { echo "package-browser.sh: the icon strips did not build (python3 with Pillow, numpy and librsvg; OB_TOOLBAR_STRIPS=0 packs none)" >&2; exit 2; }
    fi
    ls "$STRIPS"/*.ilbm >/dev/null 2>&1 || { echo "package-browser.sh: no .ilbm strips in $STRIPS" >&2; exit 2; }
    mkdir -p "$E/Images/Toolbar"
    cp "$STRIPS"/*.ilbm "$E/Images/Toolbar/"
    echo "toolbar: $(ls "$E/Images/Toolbar" | wc -l) icon strips in Libs/WebBrowser/Images/Toolbar"
fi
cp "$ICU_DATA" "$E/Unicode/icudt78b.dat"
# the Public Suffix List for cookies and sites (WebKit patch 0070), read from
# PROGDIR:Network/public-suffixes
mkdir -p "$E/Network"
cp "$HERE/data/public-suffixes" "$E/Network/"
cp "$OPENTLS_LIBS/gfopentls.library" "$OPENTLS_LIBS/gfopencrypto.library" "$D/Libs/"
JSONLIB=${JSON_LIB:-"$HERE/build/json-library/json.library"}
[ -f "$JSONLIB" ] || { echo "package-browser.sh: no $JSONLIB (scripts/build-json-library.sh); the engine's JSON.parse needs it" >&2; exit 2; }
cp "$JSONLIB" "$D/Libs/"
OPENFONTLIB=${OPENFONT_LIB:?set OPENFONT_LIB to gfopenfont.library 1.4 (gfopenfont); the engine draws its text with it}
[ -f "$OPENFONTLIB" ] || { echo "package-browser.sh: no $OPENFONTLIB" >&2; exit 2; }
cp "$OPENFONTLIB" "$D/Libs/gfopenfont.library"
# gfopenfont's CJK faces (Droid Sans Fallback for Chinese and Japanese,
# NanumGothic for Korean; gfopenfont's fonts/cjk), fetched at build time
# with their licences: the Installer puts them in FONTS:, where every
# program drawing with gfopenfont.library finds them
OPENFONTSRC=${OPENFONT_SRC:-$(CDPATH= cd -- "$(dirname -- "$OPENFONTLIB")/../.." && pwd)}
[ -x "$OPENFONTSRC/fonts/cjk/fetch-cjk-font.sh" ] || { echo "package-browser.sh: no fonts/cjk/fetch-cjk-font.sh in $OPENFONTSRC (set OPENFONT_SRC to gfopenfont's checkout)" >&2; exit 2; }
mkdir -p "$D/Fonts"
CJK_FILES=$("$OPENFONTSRC/fonts/cjk/fetch-cjk-font.sh")
printf '%s\n' "$CJK_FILES" | while IFS= read -r f; do cp "$f" "$D/Fonts/"; done
OPENGPULIB=${OPENGPU_LIB:?set OPENGPU_LIB to gfopengpu.library 0.24 or newer (gfopenrtg); the window is painted through it}
[ -f "$OPENGPULIB" ] || { echo "package-browser.sh: no $OPENGPULIB" >&2; exit 2; }
cp "$OPENGPULIB" "$D/Libs/gfopengpu.library"
OPENLAYOUTLIB=${OPENLAYOUT_LIB:?set OPENLAYOUT_LIB to gfopenlayout.library 11 or newer (gfopenlayout); the window dialogs are drawn by it}
[ -f "$OPENLAYOUTLIB" ] || { echo "package-browser.sh: no $OPENLAYOUTLIB" >&2; exit 2; }
cp "$OPENLAYOUTLIB" "$D/Libs/gfopenlayout.library"
# unicode.library, when the engine was built on it (OB_ICU_DATA=library):
# ICU's code, shared; the Installer puts it in LIBS:, and the engine also
# finds the drawer's Libs/ copy
if grep -q 'unicode\.library' "$BIN/WebBrowserEngine"; then
    UNICODELIB=${UNICODE_LIB:-"$WEBKIT_BUILD/unicode-library/unicode.library"}
    [ -f "$UNICODELIB" ] || { echo "package-browser.sh: no $UNICODELIB (scripts/build-unicode-library.sh); the engine's ICU calls need it" >&2; exit 2; }
    cp "$UNICODELIB" "$D/Libs/unicode.library"
fi
# compressed pages: openweb.fetch's native transport decodes gzip and br
# through the datatypes stream class (stream.datatype, gzip.datatype with
# z.library, brotli.datatype); the Installer puts them in SYS:Classes/DataTypes,
# DEVS:DataTypes and LIBS:
DTDIR=${DATATYPES_DIR:?set DATATYPES_DIR to the datatypes stream class build (gfopendatatypes Datatypes/stream/build.sh PREFIX); compressed pages are decoded through it}
ZLIBLIB=${ZLIB_LIB:?set ZLIB_LIB to z.library 53.12 (GFOpenZlib); gzip.datatype needs it}
for f in "$DTDIR/Classes/DataTypes/stream.datatype" "$DTDIR/Classes/DataTypes/gzip.datatype" \
         "$DTDIR/Classes/DataTypes/brotli.datatype" "$DTDIR/Devs/DataTypes/GZip" "$DTDIR/Devs/DataTypes/Brotli" "$ZLIBLIB"; do
    [ -f "$f" ] || { echo "package-browser.sh: no $f" >&2; exit 2; }
done
mkdir -p "$D/Classes/DataTypes" "$D/Devs/DataTypes"
cp "$DTDIR/Classes/DataTypes/stream.datatype" "$DTDIR/Classes/DataTypes/gzip.datatype" \
   "$DTDIR/Classes/DataTypes/brotli.datatype" "$D/Classes/DataTypes/"
cp "$DTDIR/Devs/DataTypes/GZip" "$DTDIR/Devs/DataTypes/Brotli" "$D/Devs/DataTypes/"
cp "$ZLIBLIB" "$D/Libs/z.library"
# animated GIFs: gfopengif.datatype gives every frame (WebKit patch 0110),
# from the same build (gfopendatatypes Datatypes/gif/build.sh PREFIX); the
# Installer puts it in SYS:Classes/DataTypes and its descriptor in
# DEVS:DataTypes. Without it GIFs show their first frame, as before.
if [ -f "$DTDIR/Classes/DataTypes/gfopengif.datatype" ] && [ -f "$DTDIR/Devs/DataTypes/GFOpenGIF" ]; then
    cp "$DTDIR/Classes/DataTypes/gfopengif.datatype" "$D/Classes/DataTypes/"
    cp "$DTDIR/Devs/DataTypes/GFOpenGIF" "$D/Devs/DataTypes/"
else
    echo "package-browser.sh: no gfopengif.datatype in $DTDIR: animated GIFs will show their first frame"
fi
# PNG out (canvas toDataURL, saving pictures): with cairo out of the engine,
# WebKit patch 0074 writes PNG through a datatype, gfopenpng.datatype first
# (gfopendatatypes Datatypes/png/build.sh PREFIX); the Installer puts it in
# SYS:Classes/DataTypes and its descriptor in DEVS:DataTypes.
if [ -f "$DTDIR/Classes/DataTypes/gfopenpng.datatype" ] && [ -f "$DTDIR/Devs/DataTypes/GFOpenPNG" ]; then
    cp "$DTDIR/Classes/DataTypes/gfopenpng.datatype" "$D/Classes/DataTypes/"
    cp "$DTDIR/Devs/DataTypes/GFOpenPNG" "$D/Devs/DataTypes/"
else
    echo "package-browser.sh: no gfopenpng.datatype in $DTDIR: PNG output needs a PNG datatype that writes"
fi
cp "$CA_BUNDLE/ca-bundle.pem" "$D/S/GFOpenTLS/"
[ -f "$CA_BUNDLE/ca-bundle.LICENSE" ] && cp "$CA_BUNDLE/ca-bundle.LICENSE" "$D/S/GFOpenTLS/"
cp "$HERE/dist/GFOpenBrowser/Install" "$D/Install"
mkdir -p "$D/Rexx"
cp "$HERE/dist/GFOpenBrowser/Rexx/SpeedTest.rexx" "$D/Rexx/"
cp "$HERE/icons/GFOpenBrowser.info" "$OUT/GFOpenBrowser.info"
cp "$HERE/dist/Install-GFOpenBrowser" "$HERE/dist/Install-GFOpenBrowser.info" "$OUT/"
cp "$HERE/icons/GFOpenBrowser/GFOpenBrowser.info" "$HERE/icons/GFOpenBrowser/GFOpenBrowser.readme.info" "$D/"
cp "$HERE/openbrowser.readme" "$D/GFOpenBrowser.readme"
cp "$HERE/THIRD_PARTY_NOTICES.md" "$HERE/LICENSE" "$D/"
# the LGPL parts' source offer and the licence texts (scripts/write-source-offer.sh)
sh "$HERE/scripts/write-source-offer.sh" "$D/SOURCE.txt"
mkdir -p "$D/Licences"
cp "$HERE"/dist/licences/*.txt "$D/Licences/"
if command -v jlha >/dev/null 2>&1; then
    (cd "$(dirname "$OUT")" && rm -f "$(basename "$OUT").lha" && jlha ao2q "$(basename "$OUT").lha" "$(basename "$OUT")" >/dev/null)
    echo "$OUT.lha"
else
    echo "no jlha (Ubuntu: jlha-utils): the drawer is in $OUT, unpacked" >&2
fi
