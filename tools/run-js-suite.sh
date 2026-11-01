#!/bin/sh
# GFOpenBrowser (DalsinAI/openamigabrowser)
# Copyright (c) 2026 AmigaChrome Team. MIT licence, see LICENSE.
#
# For an AmiCradle test instance whose DH1 is a host folder and whose
# S:User-Startup runs DH1:Probe/runner.script (see tools/runner.script).
#   INSTANCE_ROOT    the AmiCradle instance folder (required)
#   AMICRADLE_ROOT the AmiCradle install (default $HOME/AmiCradle)
#   RUNTIME_PORT     the instance's runtime port (required)
# Run jsc on DH1:Probe/a*.js test files in one instance job; one line per test.
# usage: run-js-suite.sh <jsc binary> <timeout-seconds> test1 test2 ...   (names without .js)
set -eu
BIN=$1; TMO=$2; shift 2
I=${INSTANCE_ROOT:?set INSTANCE_ROOT}
PORT=${RUNTIME_PORT:?set RUNTIME_PORT}
AC=${AMICRADLE_ROOT:-$HOME/AmiCradle}
D=$I/devices/harddisks/DH1/Probe
lifecycle() {
    (cd "$AC" && python3 -c "
import os, sys; sys.path.insert(0,'scripts')
from pathlib import Path
import runtime_lifecycle as r
os.environ.update({k: v for k, v in (x.split('=', 1) for x in '${PROBE_ENV:-JIT_NOX87=1}'.split())})
print(r.$1_instance_runtime(Path('.'),Path('$I'),$PORT))")
}
if [ -f "$D/running.script" ] || [ -f "$D/job.script" ]; then
    echo "runner busy: restarting the instance"; lifecycle stop; rm -f "$D/running.script" "$D/job.script"; lifecycle start
fi
cp "$BIN" "$D/jscs"
echo "staged jscs sha256 $(sha256sum "$BIN" | cut -c1-64)"
R=$D/suite-results.txt
[ -f "$R" ] && mv "$R" "$R.prev-$(date -u +%Y%m%dT%H%M%SZ)"
{
    echo 'FailAt 1000'; echo 'Stack 4194304'; echo 'CD DH1:Probe'; echo 'SetEnv JSC_STDERR_TO_STDOUT 1'
    echo 'Echo "SUITE_START" >DH1:Probe/suite-results.txt'
    for t in "$@"; do
        echo "Echo \"== $t\" >>DH1:Probe/suite-results.txt"
        echo "DH1:Probe/jscs DH1:Probe/$t.js >>DH1:Probe/suite-results.txt"
        echo "Echo \"rc=\$RC\" >>DH1:Probe/suite-results.txt"
    done
    echo 'Echo "SUITE_DONE" >>DH1:Probe/suite-results.txt'
    echo 'UnSetEnv JSC_STDERR_TO_STDOUT'
} > "$D/job.tmp"
mv "$D/job.tmp" "$D/job.script"
start=$(date +%s)
while ! grep -q SUITE_DONE "$R" 2>/dev/null; do
    sleep 3
    [ $(( $(date +%s) - start )) -ge "$TMO" ] && { echo "TIMEOUT after ${TMO}s"; break; }
done
grep -v "^ACJSC_RUN\|^ACJSC_MAIN\|^ACJSC_EXIT" "$R"
