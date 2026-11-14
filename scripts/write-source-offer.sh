#!/bin/sh
# GFOpenBrowser (DalsinAI/gfopenbrowser)
# Copyright (c) 2026 AmiCradle Team. MIT licence, see LICENSE.
#
# Write SOURCE.txt for the package: which programs in it carry LGPL code,
# and where the complete source of this very build is (this repository at
# its commit, WebKit at webkit/UPSTREAM's pin with webkit/patches, every
# other input in scripts/deps.lock). package-browser.sh puts it in the
# drawer beside LICENSE, with the licence texts in Licences/.
#
#   write-source-offer.sh FILE
#
#   OB_COMMIT   this repository's commit (default: git rev-parse HEAD)
#   OB_VERSION  the version (default: the one in openbrowser.readme)
# A tree with uncommitted changes is refused: its source is not at any
# commit. OB_ALLOW_DIRTY=1 writes it anyway, marked "not a release".
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
die() { echo "write-source-offer: $*" >&2; exit 1; }
[ $# -eq 1 ] || die "usage: write-source-offer.sh FILE"

commit=${OB_COMMIT:-$(git -C "$HERE" rev-parse HEAD 2>/dev/null || true)}
printf '%s' "$commit" | grep -Eq '^[0-9a-f]{40}$' || die "no commit for this tree (set OB_COMMIT)"
dirty=
if [ -z "${OB_COMMIT:-}" ] && [ -n "$(git -C "$HERE" status --porcelain --untracked-files=no 2>/dev/null)" ]; then
    [ "${OB_ALLOW_DIRTY:-0}" = 1 ] || die "the tree has uncommitted changes; commit them (or OB_ALLOW_DIRTY=1 for a test package)"
    dirty=1
fi
version=${OB_VERSION:-$(sed -n 's/^Version: *//p' "$HERE/openbrowser.readme" | head -1)}
wk=$(cut -d' ' -f2 "$HERE/webkit/UPSTREAM")
printf '%s' "$wk" | grep -Eq '^[0-9a-f]{40}$' || die "webkit/UPSTREAM has no pin"
npatch=$(ls "$HERE/webkit/patches"/*.patch 2>/dev/null | wc -l | tr -d ' ')
R=https://github.com/DalsinAI/gfopenbrowser

{
echo "GFOpenBrowser ${version:-(no version)}: where its source is"
echo "============================================================"
[ -z "$dirty" ] || { echo; echo "NOT A RELEASE: built from a tree with changes not in $commit."; }
cat <<TXT

Two programs in this package carry code under the GNU Lesser General
Public License (LGPL) version 2 or 2.1, from WebKit
(https://github.com/WebKit/WebKit):

  Libs/WebBrowser/WebBrowserEngine     WebCore, JavaScriptCore, WTF, bmalloc
  Libs/JavaScript/JavaScriptEngine     JavaScriptCore, WTF, bmalloc

Each WebKit file keeps its own licence, LGPL or BSD-style, as its header
says. The licence texts are in Licences/LGPL-2.txt and LGPL-2.1.txt. The
rest of GFOpenBrowser is MIT (LICENSE). webbrowser.library and the other
libraries here hold no LGPL code: webbrowser.library starts
WebBrowserEngine as a program of its own and talks to it through a port.

The complete source of this build:

  GFOpenBrowser   $R
                  commit $commit
                  ($R/tree/$commit)
  WebKit          https://github.com/WebKit/WebKit
                  commit $wk,
                  with the $npatch patches in webkit/patches/ of the
                  commit above applied in number order
  everything else scripts/deps.lock (each input's repository and commit)
                  and scripts/tarballs.lock (each upstream tarball's URL
                  and sha256), in the commit above

scripts/build-pack.sh in that commit builds this package from those
sources alone (see README.md, "Building"). The source is offered from the
same place as this package, at no charge: the same GitHub repository
holds this release and its source. If that ever stops working, ask in the
repository's issues ($R/issues) and the AmiCradle Team will send the
complete corresponding source, for at least three years from this
release, for no more than the cost of sending it.

Other licences in this package:
  Licences/ICU-LICENSE.txt           unicode.library and Unicode/icudt78b.dat (ICU 78.3)
  Licences/Liberation-OFL-1.1.txt    Libs/WebBrowser/Fonts/Liberation*.ttf
  Licences/DejaVu-LICENSE.txt        Libs/WebBrowser/Fonts/DejaVu*.ttf
  S/GFOpenTLS/ca-bundle.LICENSE      the CA list (Mozilla, MPL 2.0)
  THIRD_PARTY_NOTICES.md             everything else, file by file
TXT
} > "$1"
