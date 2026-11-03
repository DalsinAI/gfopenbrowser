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
#   OPENGPU_LIB    gfopengpu.library 0.24 or newer (gfopenrtg's
#                  library/opengpu build); the Installer puts it in LIBS:.
#                  Required: the window is painted through it (WebKit
#                  patches 0076-0082b); without it the engine paints with cairo
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
#                  SYS:Classes/DataTypes and DEVS:DataTypes
#   ZLIB_LIB       z.library 53.12 or newer (GFOpenZlib), which gzip.datatype
#                  opens. Required; the Installer puts it in LIBS:
#   OB_JSIMAGE     1 (the default when the WebCore build has one): the engine
#                  is the jsimage client (build-webcore.sh jsimage), which
#                  takes JavaScriptCore from JavaScriptEngine, packed beside
#                  it as Libs/JavaScript/JavaScriptEngine. 0: the engine
#                  with its own JavaScriptCore (bin/WebBrowserEngine)
#   EXTRAS         optional: a folder with Libs/javascript.library,
#                  Libs/JavaScript/JavaScriptEngine, C/JS and
#                  Libs/json.library, which the Installer offers
#
# OUT_DIR/GFOpenBrowser/            the drawer: GFOpenBrowser (the launcher),
#                                 Libs/ (webbrowser.library, GFOpenTLS,
#                                 json.library, gfopenfont.library,
#                                 gfopengpu.library,
#                                 unicode.library, z.library and WebBrowser/: the engine with Fonts/,
#                                 Tata.ads, Unicode/ and Network/, and
#                                 with a jsimage engine JavaScript/
#                                 JavaScriptEngine),
#                                 Classes/DataTypes/ and Devs/DataTypes/
#                                 (stream, gzip and brotli), S/GFOpenTLS/,
#                                 Install, icons and readme
# OUT_DIR/GFOpenBrowser.info        the drawer's icon
# OUT_DIR/Install-GFOpenBrowser     the Installer script (SYS:System/Installer),
# OUT_DIR/Install-GFOpenBrowser.info  and its icon: double-click to install
# OUT_DIR/Extras/                 when EXTRAS is set
# OUT_DIR.lha                     the archive, beside OUT_DIR
#
# One engine: WebBrowserEngine, which webbrowser.library starts and which has
# GFOpenBrowser's window. The library runs LIBS:WebBrowser/WebBrowserEngine
# when there is one, otherwise the drawer's Libs/WebBrowser/WebBrowserEngine
# (PROGDIR: of the launcher); Install keeps the engine in the drawer and
# renames an older LIBS:WebBrowser/WebBrowserEngine. The engine reads its
# files from its own drawer (PROGDIR: of the engine, Libs/WebBrowser):
# Fonts/ (drawn by gfopenfont.library; the CJK one fetched by
# scripts/fetch-cjk-font.sh), Tata.ads (Tata's ad and tracker list when Tata is not installed), the Public Suffix List (Network/
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
elif [ "${OB_JSIMAGE:-}" = 1 ]; then
    echo "OB_JSIMAGE=1, but $JSI has no current client (build-webcore.sh jsimage)" >&2; exit 1
else
    "$STRIP" -o "$E/WebBrowserEngine" "$BIN/WebBrowserEngine"
fi
cp "$WBLIB" "$D/Libs/"
cp "$OB_FONTS"/*.ttf "$E/Fonts/"
# the CJK font, fetched at build time (scripts/fetch-cjk-font.sh), and its
# licence beside it
CJK_FONT=$("$HERE/scripts/fetch-cjk-font.sh")
cp "$CJK_FONT" "$(dirname "$CJK_FONT")/DroidSansFallback.NOTICE" "$E/Fonts/"
cp "$HERE/src/webcore/Tata.ads" "$E/"
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
OPENGPULIB=${OPENGPU_LIB:?set OPENGPU_LIB to gfopengpu.library 0.24 or newer (gfopenrtg); the window is painted through it}
[ -f "$OPENGPULIB" ] || { echo "package-browser.sh: no $OPENGPULIB" >&2; exit 2; }
cp "$OPENGPULIB" "$D/Libs/gfopengpu.library"
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
cp "$CA_BUNDLE/ca-bundle.pem" "$D/S/GFOpenTLS/"
[ -f "$CA_BUNDLE/ca-bundle.LICENSE" ] && cp "$CA_BUNDLE/ca-bundle.LICENSE" "$D/S/GFOpenTLS/"
cp "$HERE/dist/GFOpenBrowser/Install" "$D/Install"
cp "$HERE/icons/GFOpenBrowser.info" "$OUT/GFOpenBrowser.info"
cp "$HERE/dist/Install-GFOpenBrowser" "$HERE/dist/Install-GFOpenBrowser.info" "$OUT/"
if [ -n "${EXTRAS:-}" ]; then cp -R "$EXTRAS" "$OUT/Extras"; fi
cp "$HERE/icons/GFOpenBrowser/GFOpenBrowser.info" "$HERE/icons/GFOpenBrowser/GFOpenBrowser.readme.info" "$D/"
cp "$HERE/openbrowser.readme" "$D/GFOpenBrowser.readme"
cp "$HERE/THIRD_PARTY_NOTICES.md" "$HERE/LICENSE" "$D/"
if command -v jlha >/dev/null 2>&1; then
    (cd "$(dirname "$OUT")" && rm -f "$(basename "$OUT").lha" && jlha ao2q "$(basename "$OUT").lha" "$(basename "$OUT")" >/dev/null)
    echo "$OUT.lha"
else
    echo "no jlha (Ubuntu: jlha-utils): the drawer is in $OUT, unpacked" >&2
fi
