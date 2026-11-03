#!/bin/sh
# src/webcore/ob_atomic.c on a 68040 under qemu-m68k, through CAS and CAS2,
# then through the Chip RAM path (every address below the raised limit).
# Needs m68k-linux-gnu-gcc and qemu-m68k. MIT, Copyright (c) 2026 AmiCradle Team.
set -eu
HERE=$(cd "$(dirname "$0")/../.." && pwd)
OUT=${TMPDIR:-/tmp}/ob-atomic-test
for chip in 0x00800000UL 0xffffffffUL; do
    m68k-linux-gnu-gcc -m68040 -O2 -static -pthread -DOB_ATOMIC_TEST -DOB_ATOMIC_CHIP_END=$chip "-DLOCK()=pthread_mutex_lock(&ob_atomic_test_lock)" \
        "-DUNLOCK()=pthread_mutex_unlock(&ob_atomic_test_lock)" -include pthread.h -include "$HERE/tests/atomic/lock.h" \
        -o "$OUT" "$HERE/tests/atomic/test_ob_atomic.c" "$HERE/src/webcore/ob_atomic.c"
    printf 'Chip RAM below %s: ' "$chip"
    qemu-m68k -cpu m68040 "$OUT"
done
