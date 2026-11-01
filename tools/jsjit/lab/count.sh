#!/bin/bash
# The AmiCradle Team. Counts the 68k instructions a jsc run executes under
# qemu-m68k (needs qemu's -d exec logging; no plugins).
#   count.sh jsc test.js [on]     -> "instructions translation-blocks"
# With "on", JSC_useM68KTemplateJIT=1. TOP=file also writes hot blocks.
here=$(cd "$(dirname "$0")" && pwd)
[ -x "$here/count_insns" ] || gcc -O2 -o "$here/count_insns" "$here/count_insns.c" || exit 1
f=$(mktemp -u); mkfifo "$f"
[ "$3" = on ] && export JSC_useM68KTemplateJIT=1
"$here/count_insns" < "$f" &
qemu-m68k -cpu m68040 -L /usr/m68k-linux-gnu -d in_asm,exec,nochain -D "$f" "$1" "$2" > /dev/null 2>&1
wait; rm -f "$f"
