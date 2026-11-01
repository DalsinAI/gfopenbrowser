#!/bin/sh
# GFOpenBrowser (DalsinAI/openamigabrowser)
# Copyright (c) 2026 AmigaChrome Team. MIT licence, see LICENSE.
#
# For an AmiCradle test instance whose DH1 is a host folder and whose
# S:User-Startup runs DH1:Probe/runner.script (see tools/runner.script).
#   INSTANCE_ROOT    the AmiCradle instance folder (required)
#   AMICRADLE_ROOT the AmiCradle install (default $HOME/AmiCradle)
#   RUNTIME_PORT     the instance's runtime port (required)
# Run one probe on the test instance through DH1:Probe/runner.script.
# usage: run-probe.sh <local-binary> <guest-name> <timeout-seconds> [stack] [args...]
# Restarts the instance first if a previous job is still running (hung).
# Restarts use PROBE_ENV (default JIT_NOX87=1: the AC090 JIT's x87 FPU path loops in ICU's
# uhash code on 4 Oct 2026; JIT without x87, or the interpreter, runs it correctly).
set -eu
BIN=$1; NAME=$2; TMO=$3; STACK=${4:-1048576}; shift 4 2>/dev/null || shift $#
ARGS="$*"
I=${INSTANCE_ROOT:?set INSTANCE_ROOT}
PORT=${RUNTIME_PORT:?set RUNTIME_PORT}
AC=${AMICRADLE_ROOT:-$HOME/AmiCradle}
DH1=$I/devices/harddisks/DH1/Probe
lifecycle() {
    (cd "$AC" && python3 -c "
import sys;sys.path.insert(0,'scripts')
from pathlib import Path
import os, runtime_lifecycle as r
os.environ.update({k: v for k, v in (x.split('=', 1) for x in '${PROBE_ENV:-JIT_NOX87=1}'.split()) })
p=Path('$I')
print(r.$1_instance_runtime(Path('.'),p,$PORT))")
}
if [ -f "$DH1/running.script" ] || [ -f "$DH1/job.script" ]; then
    echo "runner busy: restarting the instance"
    lifecycle stop
    rm -f "$DH1/running.script" "$DH1/job.script"
    lifecycle start
fi
cp "$BIN" "$DH1/$NAME"
echo "staged $NAME $(stat -c %s "$BIN") bytes sha256 $(sha256sum "$BIN" | cut -c1-64)"
R="$DH1/$NAME-results.txt"
[ -f "$R" ] && mv "$R" "$R.prev-$(date -u +%Y%m%dT%H%M%SZ)"
printf 'FailAt 1000\nStack %s\nCD DH1:Probe\nSetEnv JSC_STDERR_TO_STDOUT 1\nEcho "%s_START" >DH1:Probe/%s-results.txt\nDH1:Probe/%s %s >>DH1:Probe/%s-results.txt\nEcho "rc=$RC" >>DH1:Probe/%s-results.txt\nUnSetEnv JSC_STDERR_TO_STDOUT\n' \
    "$STACK" "$NAME" "$NAME" "$NAME" "$ARGS" "$NAME" "$NAME" > "$DH1/job.tmp"
mv "$DH1/job.tmp" "$DH1/job.script"
start=$(date +%s)
while ! grep -q "rc=" "$R" 2>/dev/null; do
    sleep 3
    [ $(( $(date +%s) - start )) -ge "$TMO" ] && { echo "TIMEOUT after ${TMO}s"; break; }
done
cat "$R" 2>/dev/null || echo "(no results file)"
[ -s "$DH1/$NAME-stderr.txt" ] && { echo "--- stderr"; cat "$DH1/$NAME-stderr.txt"; }
