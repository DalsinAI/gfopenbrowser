#!/bin/sh
# GFOpenBrowser (DalsinAI/openamigabrowser)
# Copyright (c) 2026 AmigaChrome Team. MIT licence, see LICENSE.
#
# GFOpenBrowser Lite's release archive, the Amiga way: one drawer and its
# icon, ready to drag where it should live.
#
#   package-lite.sh [BUILD_DIR [OUT_DIR]]
#       BUILD_DIR   where scripts/build-openbrowser.sh put GFOpenBrowserLite
#                   (default build/os3)
#       OUT_DIR     default build/package
#
#   OUT_DIR/GFOpenBrowserLite-<version>.lha      the archive:
#       GFOpenBrowserLite.info                   the drawer's icon
#       GFOpenBrowserLite/GFOpenBrowserLite        the program
#       GFOpenBrowserLite/GFOpenBrowserLite.info   its icon
#       GFOpenBrowserLite/GFOpenBrowserLite.readme (openbrowserlite.readme)
#       GFOpenBrowserLite/GFOpenBrowserLite.readme.info
#       GFOpenBrowserLite/LICENSE
#   OUT_DIR/GFOpenBrowserLite-<version>.readme   the readme beside it (Aminet)
#   OUT_DIR/GFOpenBrowserLite-<version>.lha.sha256
#
# <version> is the readme's Version: line, and must be the program's
# $VER. Every file is dated from the last commit (SOURCE_DATE_EPOCH when
# set), so the same build packs to the same bytes. The packer is lha (on
# Ubuntu: jlha-utils, whose jlha also answers to lha); level 2 headers, as
# LhA on the Amiga reads them. Without a packer the drawer is packed as
# GFOpenBrowserLite-<version>.tar.gz instead and the script says so (exit 3).
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
BUILD=${1:-"$HERE/build/os3"}
OUT=${2:-"$HERE/build/package"}
PROG="$BUILD/GFOpenBrowserLite"
README="$HERE/openbrowserlite.readme"
mkdir -p "$OUT"
OUT=$(CDPATH= cd -- "$OUT" && pwd)
[ -f "$PROG" ] || { echo "package-lite.sh: no $PROG (run scripts/build-openbrowser.sh)" >&2; exit 2; }

VERSION=$(sed -n 's/^Version: *//p' "$README" | head -1)
[ -n "$VERSION" ] || { echo "package-lite.sh: no Version: line in $README" >&2; exit 2; }
if ! LC_ALL=C grep -a -q "\$VER: GFOpenBrowserLite $VERSION " "$PROG"; then
    echo "package-lite.sh: $PROG is not GFOpenBrowserLite $VERSION (its \$VER differs from the readme)" >&2
    exit 2
fi
NAME="GFOpenBrowserLite-$VERSION"
EPOCH=${SOURCE_DATE_EPOCH:-$(git -C "$HERE" log -1 --format=%ct)}

STAGE="$OUT/stage"
rm -rf "$STAGE"
mkdir -p "$STAGE/GFOpenBrowserLite"
D="$STAGE/GFOpenBrowserLite"
cp "$PROG" "$D/GFOpenBrowserLite"
cp "$README" "$D/GFOpenBrowserLite.readme"
cp "$HERE/LICENSE" "$D/LICENSE"
# the icon set's drawer, tool and readme icons (icons/), under Lite's names
cp "$HERE/icons/GFOpenBrowser.info" "$STAGE/GFOpenBrowserLite.info"
cp "$HERE/icons/GFOpenBrowser/GFOpenBrowser.info" "$D/GFOpenBrowserLite.info"
cp "$HERE/icons/GFOpenBrowser/GFOpenBrowser.readme.info" "$D/GFOpenBrowserLite.readme.info"
chmod 0755 "$D/GFOpenBrowserLite"
find "$STAGE" -exec touch -h -d "@$EPOCH" {} +

# the files in a fixed order (no drawer entries: LhA makes the drawer)
FILES="GFOpenBrowserLite.info
GFOpenBrowserLite/GFOpenBrowserLite
GFOpenBrowserLite/GFOpenBrowserLite.info
GFOpenBrowserLite/GFOpenBrowserLite.readme
GFOpenBrowserLite/GFOpenBrowserLite.readme.info
GFOpenBrowserLite/LICENSE"

LHA=""
for p in jlha lha; do
    if command -v "$p" >/dev/null 2>&1; then LHA=$p; break; fi
done
cp "$README" "$OUT/$NAME.readme"
if [ -z "$LHA" ]; then
    rm -f "$OUT/$NAME.tar.gz"
    # shellcheck disable=SC2086
    (cd "$STAGE" && tar --owner=0 --group=0 --numeric-owner --mtime="@$EPOCH" -cf - $FILES) |
        gzip -n -9 > "$OUT/$NAME.tar.gz"
    echo "package-lite.sh: no lha packer (install jlha-utils): packed $OUT/$NAME.tar.gz instead" >&2
    (cd "$OUT" && sha256sum "$NAME.tar.gz")
    exit 3
fi
rm -f "$OUT/$NAME.lha"
# c: create, 2: level 2 headers, q: quiet
# shellcheck disable=SC2086
(cd "$STAGE" && "$LHA" c2q "$OUT/$NAME.lha" $FILES) 2>&1 | grep -v '^Picked up JAVA_TOOL_OPTIONS' || true
[ -s "$OUT/$NAME.lha" ] || { echo "package-lite.sh: $LHA made no archive" >&2; exit 1; }
(cd "$OUT" && sha256sum "$NAME.lha" > "$NAME.lha.sha256" && cat "$NAME.lha.sha256")
