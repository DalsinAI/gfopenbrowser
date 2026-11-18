#!/bin/sh
# Large pictures decode off the page's task (18 November 2026): WebKit patch
# 0126 gives every picture the same decoding thread, and
# src/webcore/AmigaWebView.cpp turns large-picture asynchronous decoding on
# and paints with DefaultAsynchronousImageDecode. Before, every picture was
# decoded while it was drawn, and the page stood still for that long.
#
# WebCore is not built on this machine, so this reads the patch and the
# source. That pictures are drawn and the page stays alive is the lab's
# check (tests/pages/async-decode.html).
# MIT, Copyright (c) 2026 AmiCradle Team.
set -u
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
P="$ROOT/webkit/patches/0126-amiga-one-thread-decodes-pictures.patch"
V="$ROOT/src/webcore/AmigaWebView.cpp"
pass=0 fail=0
ok() { if eval "$1"; then pass=$((pass + 1)); else fail=$((fail + 1)); echo "FAIL: $2"; fi; }

ok '[ -f "$P" ]' "patch 0126 is there"
if [ -f "$P" ]; then
    ok '[ "$(grep -c "^diff --git " "$P")" -eq 2 ]' "it changes two files"
    ok 'grep -q "^diff --git a/Source/WebCore/platform/graphics/ImageFrameWorkQueue.cpp " "$P"' "ImageFrameWorkQueue.cpp"
    ok 'grep -q "^diff --git a/Source/WebCore/platform/graphics/ImageFrameWorkQueue.h " "$P"' "ImageFrameWorkQueue.h"
    ok 'grep -q "^+static WorkQueue& sharedDecodingQueue()" "$P"' "there is one shared queue"
    ok 'grep -q "^+    m_workQueue = sharedDecodingQueue();" "$P"' "start() names the shared queue"
    ok 'grep -q "^+void ImageFrameWorkQueue::decodeOneOnSharedQueue()" "$P"' "a request is a task of its own"
    ok 'grep -q "^+        if (protectedRequestQueue->dequeue(request)) {" "$P"' "the task takes one request, it does not loop"
    ok 'grep -q "^+    decodeOneOnSharedQueue();" "$P"' "dispatch() sends the task"
    ok 'grep -q "^+                    if (protectedRequestQueue.ptr() != protectedThis->m_requestQueue " "$P"' "a stopped queue is told by its request queue"
    # every added code line sits inside OS(AMIGA); upstream's lines stay for the others
    ok '[ "$(grep -c "^-[^-]" "$P")" -eq 0 ]' "no upstream line is removed"
    ok '[ "$(grep -c "^+#if OS(AMIGA)$" "$P")" -ge 6 ]' "the changes are for OS(AMIGA) only"
    ok 'grep -q "^+        while (protectedRequestQueue->dequeue(request)) {" "$P" || grep -q "^         while (protectedRequestQueue->dequeue(request)) {" "$P"' "upstream's loop stays for other platforms"
    ok 'grep -q "0126-amiga-one-thread-decodes-pictures.patch" "$ROOT/webkit/README.md"' "webkit/README.md lists the patch"
fi
ok 'grep -q "settings.setLargeImageAsyncDecodingEnabled(true);" "$V"' "large pictures decode asynchronously"
ok 'grep -q "settings.setAnimatedImageAsyncDecodingEnabled(false);" "$V"' "animated pictures stay as they were"
ok 'grep -q "view->setPaintBehavior(behavior | PaintBehavior::DefaultAsynchronousImageDecode);" "$V"' "the window paint asks for asynchronous decoding"
ok 'grep -A1 "view->setPaintBehavior(behavior | PaintBehavior::DefaultAsynchronousImageDecode);" "$V" | grep -q "view->paint(context, area);"' "around the window paint"
ok 'grep -q "            view->setPaintBehavior(behavior);" "$V"' "and the behaviour is put back"
ok '[ "$(grep -c "DefaultAsynchronousImageDecode" "$V")" -eq 1 ]' "the recording paint (paint report) stays synchronous"
# the service decoder now runs on the decoding thread too: quitting must wait for a decode in flight
M="$ROOT/src/webcore/ob_media.c"
DEC=$(sed -n '/^static int serviceDecode/,/^}/p' "$M")
CLO=$(sed -n '/^void ob_media_close/,/^}/p' "$M")
ok 'echo "$DEC" | grep -q "ObtainSemaphoreShared(&decodeLock);"' "a decode holds the lock shared"
ok '[ "$(echo "$DEC" | grep -c "ReleaseSemaphore(&decodeLock);")" -eq 1 ] && [ "$(echo "$DEC" | grep -c "return")" -eq 2 ]' "and lets go on its one way out"
ok 'echo "$DEC" | sed -n "/ObtainSemaphoreShared/,\$p" | grep -q "if (opened"' "it looks for the open device with the lock held"
ok 'echo "$CLO" | grep -q "    ObtainSemaphore(&decodeLock);"' "closing takes the lock alone"
ok '[ "$(echo "$CLO" | grep -n "ObtainSemaphore(&decodeLock)" | cut -d: -f1)" -lt "$(echo "$CLO" | grep -n "CloseDevice" | cut -d: -f1)" ]' "before the device is closed"
ok '[ "$(echo "$CLO" | grep -n "ReleaseSemaphore(&decodeLock)" | cut -d: -f1)" -gt "$(echo "$CLO" | grep -n "opened = NULL" | cut -d: -f1)" ]' "and lets go after the request is gone"
ok 'grep -q "InitSemaphore(&decodeLock);" "$M"' "the lock is set up before the decoder is given out"
ok '[ -f "$ROOT/tests/pages/async-decode.html" ] && grep -q "longest" "$ROOT/tests/pages/async-decode.html"' "the lab page is in tests/pages"

echo "test_async_decode: $pass passed, $fail failed"
[ "$fail" -eq 0 ]
