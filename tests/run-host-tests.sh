#!/bin/sh
# GFOpenBrowser Lite's host tests: weblayout's HTML Lite, CSS Lite, address
# resolver, own C library calls, text converter and allocator, and its calls
# (weblayout.h, drawn by tests/host's stand-in graphics.library), and
# GFOpenMail's HTML-to-text converter, with the host's C compiler, on
# GFOpenLayout's portable C. Also the engine's malloc (the stove's
# os32compat_malloc.c) on threads, with tests/host's stand-in exec calls, and
# MallocBench, its timings against libnix's malloc (here the host's). And the
# engine's HTTP disk cache (src/webcore/ob_httpcache.c) in a scratch folder.
# And the engine's GFOpenGPU stream writer (WebKit patches 0076 and 0077) on
# gfopenrtg's CPU core, against a reference from stream.h's formulas.
#
#   OPENLAYOUT_SRC  a gfopenlayout checkout (default ../gfopenlayout)
#   OPENAMIGARTG_SRC a gfopenrtg checkout (default ../gfopenrtg)
#   HOST_CC         the compiler (default cc)
#   SANITIZE        1: with AddressSanitizer and UBSan
#
# MIT, Copyright (c) 2026 AmiCradle Team.
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
OL=${OPENLAYOUT_SRC:-"$HERE/../gfopenlayout"}
RTG=${OPENAMIGARTG_SRC:-"$HERE/../gfopenrtg"}
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
# Lite's address parser (its network calls stubbed in the test)
"$CC" $FLAGS -D_DEFAULT_SOURCE -I"$HERE/src" -I"$OM" "$HERE/src/ob_http.c" "$OM/oam_buf.c" "$WL/wl_url.c" $RT \
    "$HERE/tests/test_ob_url_parse.c" -o "$OUT/test_ob_url_parse"
# Lite's HTTP GET on a stand-in network, its allocations counted
OBM="-Dmalloc=t_malloc -Drealloc=t_realloc -Dfree=t_free"
"$CC" $FLAGS -D_DEFAULT_SOURCE -I"$HERE/src" -I"$OM" -include stdlib.h $OBM \
    -c "$HERE/src/ob_http.c" -o "$OUT/ob_http_counted.o"
"$CC" $FLAGS -include stdlib.h $OBM -c "$OM/oam_buf.c" -o "$OUT/oam_buf_counted.o"
"$CC" $FLAGS -D_DEFAULT_SOURCE -I"$HERE/src" -I"$OM" "$OUT/ob_http_counted.o" "$OUT/oam_buf_counted.o" \
    "$WL/wl_url.c" $RT "$HERE/tests/test_ob_http_fetch.c" -o "$OUT/test_ob_http_fetch"
# the engine's public suffix lookup, which lives in WebKit patch 0070
python3 "$HERE/tools/patch-files.py" "$HERE/webkit/patches/0070-amiga-public-suffixes-without-libpsl.patch" \
    "$OUT/psl" PublicSuffixAmiga.c PublicSuffixAmiga.h
"$CC" $FLAGS -I"$OUT/psl" "$OUT/psl/PublicSuffixAmiga.c" "$HERE/tests/test_public_suffixes.c" \
    -o "$OUT/test_public_suffixes"
# the window's GFOpenGPU stream writer, which lives in WebKit patches 0076,
# 0077 (paths), 0078 (clips) and 0079 (patterns, conic gradients), on gfopenrtg's CPU core (built as its tests/run.sh
# builds it)
python3 "$HERE/tools/patch-files.py" "$HERE/webkit/patches/0076-amiga-graphicscontext-opengpu.patch" \
    "$OUT/opengpu" OpenGPUPaintAmiga.c OpenGPUPaintAmiga.h
python3 "$HERE/tools/patch-files.py" "$HERE/webkit/patches/0077-amiga-opengpu-paths.patch" \
    "$OUT/opengpu" OpenGPUPaintAmiga.c OpenGPUPaintAmiga.h
python3 "$HERE/tools/patch-files.py" "$HERE/webkit/patches/0078-amiga-opengpu-clips.patch" \
    "$OUT/opengpu" OpenGPUPaintAmiga.c OpenGPUPaintAmiga.h
python3 "$HERE/tools/patch-files.py" "$HERE/webkit/patches/0079-amiga-opengpu-patterns.patch" \
    "$OUT/opengpu" OpenGPUPaintAmiga.c OpenGPUPaintAmiga.h
python3 "$HERE/tools/patch-files.py" "$HERE/webkit/patches/0081-amiga-opengpu-layers.patch" \
    "$OUT/opengpu" OpenGPUPaintAmiga.c OpenGPUPaintAmiga.h
python3 "$HERE/tools/patch-files.py" "$HERE/webkit/patches/0082-amiga-opengpu-glyphs.patch" \
    "$OUT/opengpu" OpenGPUPaintAmiga.c OpenGPUPaintAmiga.h
python3 "$HERE/tools/patch-files.py" "$HERE/webkit/patches/0082b-amiga-opengpu-shadows.patch" \
    "$OUT/opengpu" OpenGPUPaintAmiga.c OpenGPUPaintAmiga.h
# 0094's layers under an outer level's clip mask (drawn here, put on through it)
python3 "$HERE/tools/patch-files.py" "$HERE/webkit/patches/0094-amiga-cairo-out.patch" \
    "$OUT/opengpu" OpenGPUPaintAmiga.c OpenGPUPaintAmiga.h
"$CC" $FLAGS -I"$OUT/opengpu" -I"$RTG/include" -I"$RTG/library/opengpu" "$OUT/opengpu/OpenGPUPaintAmiga.c" \
    "$RTG/library/opengpu/ogpu_core.c" "$HERE/tests/test_opengpu_paint.c" -lm -o "$OUT/test_opengpu_paint"
# 0078's clips, masks, aliased shapes, copy and clear, against cairo drawing
# the same calls, where cairo's headers are there (cairo leaves the engine
# at the end of step 6; this is its reference until then)
CLIP_TEST=0
if pkg-config --exists cairo 2>/dev/null; then
    "$CC" $FLAGS -D_DEFAULT_SOURCE $(pkg-config --cflags cairo) -I"$OUT/opengpu" -I"$RTG/include" -I"$RTG/library/opengpu" \
        "$OUT/opengpu/OpenGPUPaintAmiga.c" "$RTG/library/opengpu/ogpu_core.c" "$HERE/tests/test_opengpu_clip.c" \
        $(pkg-config --libs cairo) -lm -o "$OUT/test_opengpu_clip"
    # 0079's patterns and gradients against cairo's (WebKit's cairo port's calls)
    "$CC" $FLAGS -D_DEFAULT_SOURCE $(pkg-config --cflags cairo) -I"$OUT/opengpu" -I"$RTG/include" -I"$RTG/library/opengpu" \
        "$OUT/opengpu/OpenGPUPaintAmiga.c" "$RTG/library/opengpu/ogpu_core.c" "$HERE/tests/test_opengpu_pattern.c" \
        $(pkg-config --libs cairo) -lm -o "$OUT/test_opengpu_pattern"
    # 0080's borders, lines, bullets and text lines (C++ only: the writer calls they
    # make) against WebKit's cairo calls for them
    "$CC" $FLAGS -D_DEFAULT_SOURCE $(pkg-config --cflags cairo) -I"$OUT/opengpu" -I"$RTG/include" -I"$RTG/library/opengpu" \
        "$OUT/opengpu/OpenGPUPaintAmiga.c" "$RTG/library/opengpu/ogpu_core.c" "$HERE/tests/test_opengpu_lines.c" \
        $(pkg-config --libs cairo) -lm -o "$OUT/test_opengpu_lines"
    # 0081's drawing into transparency layers' groups, cairo putting them on the window
    "$CC" $FLAGS -D_DEFAULT_SOURCE $(pkg-config --cflags cairo) -I"$OUT/opengpu" -I"$RTG/include" -I"$RTG/library/opengpu" \
        "$OUT/opengpu/OpenGPUPaintAmiga.c" "$RTG/library/opengpu/ogpu_core.c" "$HERE/tests/test_opengpu_layer.c" \
        $(pkg-config --libs cairo) -lm -o "$OUT/test_opengpu_layer"
    # 0082b's drop shadows against ShadowBlur's (its box sizes and blur, ported)
    "$CC" $FLAGS -D_DEFAULT_SOURCE $(pkg-config --cflags cairo) -I"$OUT/opengpu" -I"$RTG/include" -I"$RTG/library/opengpu" \
        "$OUT/opengpu/OpenGPUPaintAmiga.c" "$RTG/library/opengpu/ogpu_core.c" "$HERE/tests/test_opengpu_shadow.c" \
        $(pkg-config --libs cairo) -lm -o "$OUT/test_opengpu_shadow"
    CLIP_TEST=1
fi
"$OUT/test_wl_html" > "$OUT/test_wl_html.log" || { cat "$OUT/test_wl_html.log"; exit 1; }
tail -1 "$OUT/test_wl_html.log"
"$OUT/test_wl_css"
"$OUT/test_wl_url"
"$OUT/test_wl_libc"
"$OUT/test_wl_document"
"$OUT/test_oam_html"
"$OUT/test_ob_url_parse"
"$OUT/test_ob_http_fetch"
rm -rf "$OUT/httpcache"
"$OUT/test_ob_httpcache" "$OUT/httpcache"
"$OUT/test_public_suffixes" "$HERE/data/public-suffixes" "$HERE/tests/public-suffix-cases.txt"
"$OUT/test_opengpu_paint"
# a test's last line only, but its whole log and a failed run when it fails (a pipe into tail hid the failure).
last_line() { "$1" > "$1.log" 2>&1 || { cat "$1.log"; exit 1; }; tail -1 "$1.log"; }
if [ "$CLIP_TEST" = 1 ]; then "$OUT/test_opengpu_clip"; last_line "$OUT/test_opengpu_pattern"; last_line "$OUT/test_opengpu_lines"; last_line "$OUT/test_opengpu_layer"; last_line "$OUT/test_opengpu_shadow"; else echo "test_opengpu_clip, test_opengpu_pattern, test_opengpu_lines, test_opengpu_layer, test_opengpu_shadow: skipped (no cairo headers)"; fi
# fewer operations under the sanitizers, which are slower
R=200000
[ "${SANITIZE:-0}" = 1 ] && R=50000
"$OUT/test_os32_malloc" $R
"$OUT/test_os32_malloc_debug" $R
"$OUT/mallocbench" 1000000
