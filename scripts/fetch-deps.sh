#!/bin/sh
# GFOpenBrowser (DalsinAI/gfopenbrowser)
# Copyright (c) 2026 AmiCradle Team. MIT licence, see LICENSE.
#
# Check out every build input in scripts/deps.lock at its pinned commit, and
# apply webkit/patches to WebKit in number order, so that a build starts
# from the repositories alone (no tree from an older machine).
#
#   fetch-deps.sh [DEPS_DIR]          (default ../ob-deps beside this repository)
#
#   LOCK          the pin file                  (default scripts/deps.lock)
#   PATCHES_DIR   WebKit's patches              (default webkit/patches)
#   ONLY          space-separated names: fetch only these
#   DRY_RUN       1: print what would be fetched, change nothing
#
# Each input goes to DEPS_DIR/NAME. One already there at its pin is left as
# it is (WebKit's patches included, checked by the stamp file
# DEPS_DIR/webkit/.ob-patches, the patches' sha256). One at another commit,
# or with local changes, is refused: the script never throws work away, so
# move it aside yourself. It fetches the one commit (--depth 1), so a pin
# must be a commit the server still has.
set -eu

HERE=$(cd "$(dirname "$0")/.." && pwd)
DEPS=${1:-"$HERE/../ob-deps"}
LOCK=${LOCK:-"$HERE/scripts/deps.lock"}
PATCHES_DIR=${PATCHES_DIR:-"$HERE/webkit/patches"}
DRY_RUN=${DRY_RUN:-0}
ONLY=${ONLY:-}

die() { echo "fetch-deps: $*" >&2; exit 1; }

sha256() {
    if command -v sha256sum >/dev/null 2>&1; then sha256sum | cut -d' ' -f1
    else shasum -a 256 | cut -d' ' -f1; fi
}

[ -f "$LOCK" ] || die "no pin file: $LOCK"

# The lock's lines, checked before anything is fetched: NAME URL COMMIT [sparse=...].
check_lock() {
    n=0
    seen=" "
    while read -r name url commit rest; do
        case "$name" in ''|'#'*) continue ;; esac
        n=$((n + 1))
        [ -n "$url" ] && [ -n "$commit" ] || die "$LOCK: $name: needs NAME URL COMMIT"
        printf '%s' "$commit" | grep -Eq '^[0-9a-f]{40}$' || die "$LOCK: $name: commit is not a full sha: $commit"
        printf '%s' "$name" | grep -Eq '^[A-Za-z0-9._-]+$' || die "$LOCK: bad name: $name"
        case "$seen" in *" $name "*) die "$LOCK: $name is listed twice" ;; esac
        seen="$seen$name "
    done < "$LOCK"
    [ "$n" -gt 0 ] || die "$LOCK: no inputs"
}

wanted() {
    [ -z "$ONLY" ] && return 0
    for w in $ONLY; do [ "$w" = "$1" ] && return 0; done
    return 1
}

patches_stamp() {
    ls "$PATCHES_DIR"/*.patch >/dev/null 2>&1 || { echo none; return; }
    cat "$PATCHES_DIR"/*.patch | sha256
}

apply_patches() {
    d=$1
    for p in "$PATCHES_DIR"/*.patch; do
        [ -f "$p" ] || continue
        git -C "$d" apply --whitespace=nowarn "$p" || die "webkit: $(basename "$p") does not apply"
    done
    patches_stamp > "$d/.ob-patches"
}

fetch_one() {
    name=$1 url=$2 commit=$3 sparse=$4
    d="$DEPS/$name"
    if [ "$DRY_RUN" = 1 ]; then
        echo "$name $commit $url${sparse:+ sparse=$sparse} -> $d"
        return
    fi
    if [ -d "$d/.git" ]; then
        have=$(git -C "$d" rev-parse HEAD 2>/dev/null || echo none)
        [ "$have" = "$commit" ] || die "$name: $d is at $have, not the pin $commit; move it aside"
        if [ "$name" = webkit ]; then
            [ -f "$d/.ob-patches" ] && [ "$(cat "$d/.ob-patches")" = "$(patches_stamp)" ] ||
                die "webkit: $d is at the pin but its patches are not this repository's; move it aside"
        else
            [ -z "$(git -C "$d" status --porcelain)" ] || die "$name: $d has local changes; move it aside"
        fi
        echo "$name: at $commit already"
        return
    fi
    [ -e "$d" ] && die "$name: $d exists and is not a checkout; move it aside"
    mkdir -p "$d"
    git -C "$d" init -q
    git -C "$d" remote add origin "$url"
    if [ -n "$sparse" ]; then
        git -C "$d" config extensions.partialClone origin 2>/dev/null || true
        git -C "$d" sparse-checkout set --cone $(printf '%s' "$sparse" | tr ',' ' ')
    fi
    git -C "$d" fetch -q --depth 1 ${sparse:+--filter=blob:none} origin "$commit" ||
        die "$name: cannot fetch $commit from $url"
    git -C "$d" -c advice.detachedHead=false checkout -q FETCH_HEAD
    [ "$(git -C "$d" rev-parse HEAD)" = "$commit" ] || die "$name: checked out the wrong commit"
    [ "$name" = webkit ] && apply_patches "$d"
    echo "$name: $commit"
}

check_lock
[ "$DRY_RUN" = 1 ] || mkdir -p "$DEPS"
while read -r name url commit rest; do
    case "$name" in ''|'#'*) continue ;; esac
    wanted "$name" || continue
    sparse=
    case "$rest" in sparse=*) sparse=${rest%% *}; sparse=${sparse#sparse=} ;; esac
    fetch_one "$name" "$url" "$commit" "$sparse" </dev/null
done < "$LOCK"
