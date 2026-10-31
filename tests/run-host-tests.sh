#!/bin/sh
# OpenBrowser Lite's host tests: weblayout's HTML Lite, CSS Lite, address
# resolver, own C library calls, text converter and allocator, and its calls
# (weblayout.h, drawn by tests/host's stand-in graphics.library), and
# OpenMail's HTML-to-text converter, with the host's C compiler, on
# OpenLayout's portable C. Also the engine's malloc (the stove's
# os32compat_malloc.c) on threads, with tests/host's stand-in exec calls, and
# MallocBench, its timings against libnix's malloc (here the host's). And the
# engine's HTTP disk cache (src/webcore/ob_httpcache.c) in a scratch folder.
# And the engine's OpenGPU stream writer (WebKit patch 0076) on
# openamigartg's CPU core, against a reference from stream.h's formulas.
#
#   OPENLAYOUT_SRC  an openamigalayout checkout (default ../openamigalayout)
#   OPENAMIGARTG_SRC an openamigartg checkout (default ../openamigartg)
#   HOST_CC         the compiler (default cc)
#   SANITIZE        1: with AddressSanitizer and UBSan
#
# MIT, Copyright (c) 2026 AmigaChrome Team.
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
OL=${OPENLAYOUT_SRC:-"$HERE/../openamigalayout"}
RTG=${OPENAMIGARTG_SRC:-"$HERE/../openamigartg"}
CC=${HOST_CC:-cc}
OUT=${OUT:-"$HERE/build/host-tests"}
WL="$HERE/src/weblayout"
FLAGS="-O2 -std=c99 -Wall -Wextra -Werror -pedantic"
[ "${SANITIZE:-0}" = 1 ] && FLAGS="$FLAGS -g -fsanitize=address,undefined -fno-sanitize-recover=all"
mkdir -p "$OUT"
RT="$WL/wl_mem.c $WL/wl_libc.c"     # weblayout's allocator and its own C library calls
"$CC" $FLAGS -I"$WL" -I"$OL/include" "$WL/wl_html.c" "$WL/wl_css.c" $RT "$OL/src/openlayout.c" \
    "$HERE/tests/test_wl_html.c" -o "$OUT/test_wl_html"
"$CC" $FLAGS -I"$WL" -I"$OL/include" "$WL/wl_css.c" $RT "$OL/src/openlayout.c" \
    "$HERE/tests/test_wl_css.c" -o "$OUT/test_wl_css"
"$CC" $FLAGS -I"$WL" "$WL/wl_url.c" $RT "$HERE/tests/test_wl_url.c" -o "$OUT/test_wl_url"
"$CC" $FLAGS -I"$WL" "$WL/wl_text.c" $RT "$HERE/tests/test_wl_libc.c" -o "$OUT/test_wl_libc"
OM="$HERE/third_party/openmail"     # strncasecmp: POSIX
"$CC" $FLAGS -D_DEFAULT_SOURCE -I"$HERE/tests/host" -I"$WL" -I"$OM" -I"$OL/include" \
    "$WL/wl_document.c" "$WL/wl_paint.c" "$WL/wl_html.c" "$WL/wl_css.c" "$WL/wl_url.c" \
    "$WL/wl_text.c" $RT "$OL/src/openlayout.c" \
    "$HERE/tests/test_wl_document.c" -o "$OUT/test_wl_document"
"$CC" $FLAGS -D_DEFAULT_SOURCE -I"$OM" "$OM/oam_html.c" "$OM/oam_buf.c" \
    "$HERE/tests/test_oam_html.c" -o "$OUT/test_oam_html"
# the stove's allocator: its aligned_alloc and the like renamed, so the host's stay
OS32M="-D_DEFAULT_SOURCE -Daligned_alloc=os32_aligned_alloc -Dmemalign=os32_memalign \
    -Dposix_memalign=os32_posix_memalign -I$HERE/tests/host"
"$CC" $FLAGS $OS32M "$HERE/tests/test_os32_malloc.c" "$HERE/tests/host/exec_tasks.c" \
    -pthread -o "$OUT/test_os32_malloc"
"$CC" $FLAGS $OS32M -DOS32_MALLOC_DEBUG "$HERE/tests/test_os32_malloc.c" \
    "$HERE/tests/host/exec_tasks.c" -pthread -o "$OUT/test_os32_malloc_debug"
"$CC" $FLAGS $OS32M "$HERE/tools/mallocbench.c" "$HERE/stove/compat/os32compat_malloc.c" \
    "$HERE/tests/host/exec_tasks.c" -pthread -o "$OUT/mallocbench"
# opendir(), stat() and setenv(): POSIX
"$CC" $FLAGS -D_DEFAULT_SOURCE -I"$HERE/src/webcore" "$HERE/src/webcore/ob_httpcache.c" \
    "$HERE/tests/test_ob_httpcache.c" -o "$OUT/test_ob_httpcache"
# the engine's public suffix lookup, which lives in WebKit patch 0070
python3 "$HERE/tools/patch-files.py" "$HERE/webkit/patches/0070-amiga-public-suffixes-without-libpsl.patch" \
    "$OUT/psl" PublicSuffixAmiga.c PublicSuffixAmiga.h
"$CC" $FLAGS -I"$OUT/psl" "$OUT/psl/PublicSuffixAmiga.c" "$HERE/tests/test_public_suffixes.c" \
    -o "$OUT/test_public_suffixes"
# the window's OpenGPU stream writer, which lives in WebKit patch 0076, on
# openamigartg's CPU core (built as its tests/run.sh builds it)
python3 "$HERE/tools/patch-files.py" "$HERE/webkit/patches/0076-amiga-graphicscontext-opengpu.patch" \
    "$OUT/opengpu" OpenGPUPaintAmiga.c OpenGPUPaintAmiga.h
"$CC" $FLAGS -I"$OUT/opengpu" -I"$RTG/include" -I"$RTG/library/opengpu" "$OUT/opengpu/OpenGPUPaintAmiga.c" \
    "$RTG/library/opengpu/ogpu_core.c" "$HERE/tests/test_opengpu_paint.c" -lm -o "$OUT/test_opengpu_paint"
"$OUT/test_wl_html" > "$OUT/test_wl_html.log" || { cat "$OUT/test_wl_html.log"; exit 1; }
tail -1 "$OUT/test_wl_html.log"
"$OUT/test_wl_css"
"$OUT/test_wl_url"
"$OUT/test_wl_libc"
"$OUT/test_wl_document"
"$OUT/test_oam_html"
rm -rf "$OUT/httpcache"
"$OUT/test_ob_httpcache" "$OUT/httpcache"
"$OUT/test_public_suffixes" "$HERE/data/public-suffixes" "$HERE/tests/public-suffix-cases.txt"
"$OUT/test_opengpu_paint"
# fewer operations under the sanitizers, which are slower
R=200000
[ "${SANITIZE:-0}" = 1 ] && R=50000
"$OUT/test_os32_malloc" $R
"$OUT/test_os32_malloc_debug" $R
"$OUT/mallocbench" 1000000
