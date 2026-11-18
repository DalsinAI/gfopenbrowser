#!/bin/sh
# WebKit patch 0123: a WebSocket with no channel fails, it does not assert.
# The Amiga port gives pages no WebSocket channel (EmptySocketProvider,
# patch 0071), and WebSocket::connect() met RELEASE_ASSERT(m_channel): a
# page that did new WebSocket(...) stopped the whole browser (18 November
# 2026). This checks the patch's guard as it is written: it stands before
# the assertion, it is the Amiga port's only, and it fails the connection
# the way a blocked one fails. The page tests/pages/websocket-fails.html
# shows the events in the lab.
# MIT, Copyright (c) 2026 AmiCradle Team.
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
P="$ROOT/webkit/patches/0123-amiga-websocket-no-channel-fails.patch"
pass=0 fail=0
ok() { if eval "$1"; then pass=$((pass + 1)); else fail=$((fail + 1)); echo "FAIL: $2"; fi; }

ok '[ -f "$P" ]' "the patch is there"
ok 'grep -q "^+++ b/Source/WebCore/Modules/websockets/WebSocket.cpp" "$P"' "it changes WebSocket.cpp"
ok '[ "$(grep -c "^diff --git" "$P")" = 1 ]' "and nothing else"

# the added lines, in order, without the leading +
added=$(grep "^+" "$P" | grep -v "^+++" | cut -c2- | grep -v "^ *//" | tr -s " " | tr -d "\n")
want='#if PLATFORM(AMIGA) if (!m_channel) { context->addConsoleMessage(MessageSource::JS, MessageLevel::Error, "WebSocket is not available in this browser yet"_s); queueTaskKeepingObjectAlive(*this, TaskSource::WebSocket, [](auto& socket) { socket.m_state = CLOSED; socket.dispatchErrorEventIfNeeded(); socket.dispatchEvent(CloseEvent::create(false, ThreadableWebSocketChannel::CloseEventCodeAbnormalClosure, emptyString())); socket.stop(); }); return { }; }#endif'
ok '[ "$added" = "$want" ]' "the guard is: no channel, a console message, then error, close 1006 and stop in a task, return (got: $added)"

# the guard stands after the channel is made and before the assertion
order=$(awk '/m_channel = ThreadableWebSocketChannel::create/ { c = NR }
             /^\+ *if \(!m_channel\)/ { g = NR }
             /^ *RELEASE_ASSERT\(m_channel\);/ { a = NR }
             END { print (c && g && a && c < g && g < a) ? "ok" : "no" }' "$P")
ok '[ "$order" = ok ]' "the guard is between the channel's making and RELEASE_ASSERT(m_channel)"
ok '! grep -q "^-[^-]" "$P"' "the patch takes no line away (the assertion stays for a port with a provider)"

# each hunk counts its lines
bad=$(awk '/^@@ / { if (c && (k != n || j != m)) print "x"; split($2, o, ","); split($3, a, ","); m = o[2]; n = a[2]; k = 0; j = 0; c = 1; next }
           c && /^-- $/ { if (k != n || j != m) print "x"; c = 0 }
           c && /^\+/ { k++ } c && /^-/ { j++ } c && /^ / { k++; j++ }' "$P")
ok '[ -z "$bad" ]' "each hunk counts its lines"

# the patch is listed, and the lab page is there
ok 'grep -q "0123-amiga-websocket-no-channel-fails.patch" "$ROOT/webkit/README.md"' "webkit/README.md lists the patch"
ok 'grep -q "new WebSocket" "$ROOT/tests/pages/websocket-fails.html"' "tests/pages/websocket-fails.html makes a WebSocket"

echo "websocket-no-channel: $pass passed, $fail failed"
[ "$fail" = 0 ]
