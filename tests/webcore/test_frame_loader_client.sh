#!/bin/sh
# AmigaFrameLoaderClient's history calls: HistoryController calls the
# client's restoreViewState() from restoreScrollPositionAndViewState(), so
# the client must never call back into the HistoryController's restore calls
# (that recursion overflowed the engine's stack on Reload, Back and Forward).
# Every client call that WebCore makes from inside a HistoryController restore
# is checked here.
#
# MIT, Copyright (c) 2026 AmiCradle Team.
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
CLIENT="$HERE/src/webcore/AmigaFrameLoaderClient.cpp"
fail=0

# The body of one client call, from its declaration to its closing brace.
body() {
    awk -v name="$1" '
        index($0, "void " name "(") { on = 1 }
        on { print; depth += gsub(/\{/, "{"); depth -= gsub(/\}/, "}"); if (depth <= 0 && /\}/) exit }
    ' "$CLIENT"
}

for call in restoreViewState; do
    text=$(body "$call")
    if [ -z "$text" ]; then
        echo "FAIL $call: not found in AmigaFrameLoaderClient.cpp"
        fail=1
        continue
    fi
    if printf '%s\n' "$text" | grep -q 'restoreScrollPositionAndViewState'; then
        echo "FAIL $call calls back into restoreScrollPositionAndViewState (endless recursion)"
        fail=1
    else
        echo "ok   $call does not call back into the HistoryController"
    fi
done

exit $fail
