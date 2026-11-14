#!/bin/sh
# GFOpenBrowser (DalsinAI/gfopenbrowser)
# Copyright (c) 2026 AmiCradle Team. MIT licence, see LICENSE.
#
# Put every upstream tarball in scripts/tarballs.lock into one folder, each
# checked against its sha256.
#
#   fetch-tarballs.sh TARBALLS_DIR
#
#   LOCK           the pin file                  (default scripts/tarballs.lock)
#   TARBALLS_FROM  a folder to copy files from before downloading (an older
#                  tarballs folder; the only source for a file with URL -)
#   DRY_RUN        1: print what would be done, change nothing
#
# A file already there with the right sha256 is kept; one with another
# sha256 is refused (never overwritten: move it aside). A download goes to
# FILE.part and is renamed only once its sha256 matches.
set -eu

HERE=$(cd "$(dirname "$0")/.." && pwd)
LOCK=${LOCK:-"$HERE/scripts/tarballs.lock"}
FROM=${TARBALLS_FROM:-}
DRY_RUN=${DRY_RUN:-0}

die() { echo "fetch-tarballs: $*" >&2; exit 1; }

sha256() {
    if command -v sha256sum >/dev/null 2>&1; then sha256sum < "$1" | cut -d' ' -f1
    else shasum -a 256 < "$1" | cut -d' ' -f1; fi
}

[ $# -eq 1 ] || die "usage: fetch-tarballs.sh TARBALLS_DIR"
TB=$1
[ -f "$LOCK" ] || die "no pin file: $LOCK"

n=0 seen=" "
while read -r file sum url rest; do
    case "$file" in ''|'#'*) continue ;; esac
    n=$((n + 1))
    [ -n "$sum" ] && [ -n "$url" ] || die "$LOCK: $file: needs FILE SHA256 URL"
    printf '%s' "$sum" | grep -Eq '^[0-9a-f]{64}$' || die "$LOCK: $file: not a sha256: $sum"
    printf '%s' "$file" | grep -Eq '^[A-Za-z0-9._+-]+$' || die "$LOCK: bad file name: $file"
    case "$url" in -|https://*) ;; *) die "$LOCK: $file: URL must be https:// or -" ;; esac
    case "$seen" in *" $file "*) die "$LOCK: $file is listed twice" ;; esac
    seen="$seen$file "
done < "$LOCK"
[ "$n" -gt 0 ] || die "$LOCK: no files"

[ "$DRY_RUN" = 1 ] || mkdir -p "$TB"
missing=
while read -r file sum url rest; do
    case "$file" in ''|'#'*) continue ;; esac
    t="$TB/$file"
    if [ "$DRY_RUN" = 1 ]; then echo "$file $sum ${url}"; continue; fi
    if [ -f "$t" ]; then
        [ "$(sha256 "$t")" = "$sum" ] || die "$file: $t has another sha256; move it aside"
        echo "$file: there already"
        continue
    fi
    if [ -n "$FROM" ] && [ -f "$FROM/$file" ]; then
        [ "$(sha256 "$FROM/$file")" = "$sum" ] || die "$file: $FROM/$file has another sha256"
        cp "$FROM/$file" "$t.part" && mv "$t.part" "$t"
        echo "$file: copied from $FROM"
        continue
    fi
    if [ "$url" = - ]; then
        missing="$missing $file"
        echo "$file: no URL; put it in TARBALLS_FROM (sha256 $sum)" >&2
        continue
    fi
    curl -fsSL --retry 3 -o "$t.part" "$url" </dev/null || { rm -f "$t.part"; die "$file: cannot download $url"; }
    got=$(sha256 "$t.part")
    [ "$got" = "$sum" ] || { rm -f "$t.part"; die "$file: $url gave sha256 $got, not $sum"; }
    mv "$t.part" "$t"
    echo "$file: downloaded"
done < "$LOCK"
[ -z "$missing" ] || die "missing:$missing"
