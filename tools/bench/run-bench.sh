#!/bin/sh
# GFOpenBrowser (DalsinAI/gfopenbrowser)
# Copyright (c) 2026 The AmiCradle Team. MIT licence, see LICENSE.
#
# Run a staged benchmark (tools/bench/stage.py) on an AmiCradle test
# instance, one page per job through DH1:Probe/runner.script (see
# tools/runner.script), and collect the logs and pictures.
#   INSTANCE_ROOT    the AmiCradle instance folder (required)
#   RUNTIME_PORT     the instance's runtime port (required)
#   AMICRADLE_ROOT the AmiCradle install (default $HOME/AmiCradle)
#   BENCH_DRAWER     the drawer in DH1 that stage.py filled (default Bench)
#   BENCH_TIMEOUT    seconds a page may take before the instance is
#                    restarted and the next page started (default 900)
#   PROBE_ENV        as for run-probe.sh (default JIT_NOX87=1)
# usage: run-bench.sh OUT_DIR [page-id ...]   (default: every staged page)
# OUT_DIR gets logs/, png/, bench-manifest.txt, stage-info.txt and host.log.
set -eu
OUT=${1:?usage: run-bench.sh OUT_DIR [page-id ...]}; shift
I=${INSTANCE_ROOT:?set INSTANCE_ROOT}
PORT=${RUNTIME_PORT:?set RUNTIME_PORT}
AC=${AMICRADLE_ROOT:-$HOME/AmiCradle}
DRAWER=${BENCH_DRAWER:-Bench}
TMO=${BENCH_TIMEOUT:-900}
DH1=$I/devices/harddisks/DH1
B=$DH1/$DRAWER
P=$DH1/Probe
[ -f "$B/OBBench" ] || { echo "nothing staged in $B (run tools/bench/stage.py --out $B)"; exit 2; }
[ -d "$P" ] || { echo "no $P: the instance needs DH1:Probe/runner.script (tools/runner.script)"; exit 2; }
lifecycle() {
    (cd "$AC" && python3 -c "
import os, sys; sys.path.insert(0,'scripts')
from pathlib import Path
import runtime_lifecycle as r
os.environ.update({k: v for k, v in (x.split('=', 1) for x in '${PROBE_ENV:-JIT_NOX87=1}'.split())})
print(r.$1_instance_runtime(Path('.'),Path('$I'),$PORT))")
}
restart() {
    echo "restarting the instance"
    lifecycle stop || true
    rm -f "$P/running.script" "$P/job.script"
    lifecycle start
}
if [ -f "$P/running.script" ] || [ -f "$P/job.script" ]; then
    echo "runner busy"; restart
fi
[ $# -gt 0 ] || set -- $(cut -d' ' -f1 "$B/bench-manifest.txt")
mkdir -p "$OUT/logs" "$OUT/png"
cp "$B/bench-manifest.txt" "$B/stage-info.txt" "$OUT/"
HOST=$OUT/host.log
echo "OBBENCH_HOST_START $(date -u +%Y-%m-%dT%H:%M:%SZ) instance=$I" >> "$HOST"
for id in "$@"; do
    [ -f "$B/jobs/$id" ] || { echo "$id: not staged"; continue; }
    L=$B/logs/$id.log
    rm -f "$L" "$B/png/$id.png"
    printf 'FailAt 1000\nCD DH1:%s\nExecute jobs/%s\n' "$DRAWER" "$id" > "$P/job.tmp"
    mv "$P/job.tmp" "$P/job.script"
    start=$(date +%s)
    status=done
    while ! grep -q "^OBBENCH_END" "$L" 2>/dev/null; do
        sleep 3
        if [ $(( $(date +%s) - start )) -ge "$TMO" ]; then status=timeout; break; fi
    done
    wall=$(( $(date +%s) - start ))
    if [ "$status" = timeout ]; then
        echo "OBBENCH_TIMEOUT host_s=$wall" >> "$L"
        restart
    else
        # let the job's script finish before the next one is dropped in
        while [ -f "$P/running.script" ] && [ $(( $(date +%s) - start )) -lt "$TMO" ]; do sleep 1; done
    fi
    echo "OBBENCH_HOST wall_s=$wall" >> "$L"
    echo "OBBENCH_HOST id=$id status=$status wall_s=$wall" >> "$HOST"
    echo "$id: $status in ${wall}s"
    cp "$L" "$OUT/logs/"
    [ -f "$B/png/$id.png" ] && cp "$B/png/$id.png" "$OUT/png/"
done
echo "OBBENCH_HOST_DONE $(date -u +%Y-%m-%dT%H:%M:%SZ)" >> "$HOST"
echo "Results in $OUT: python3 tools/bench/report.py $OUT"
