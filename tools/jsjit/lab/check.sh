#!/bin/bash
# The AmiCradle Team. Runs every lab/correct*.js on a jsc host build with
# the template JIT on and off, and compares each output with node's.
#   check.sh path/to/jsc
here=$(cd "$(dirname "$0")" && pwd)
jsc=${1:?usage: check.sh path/to/jsc}
work=$(mktemp -d)
echo 'globalThis.print = (...a) => console.log(...a);' > "$work/print.js"
fail=0
for t in "$here"/correct*.js; do
    n=$(basename "$t" .js)
    node -r "$work/print.js" "$t" > "$work/$n.node" 2>&1
    for jit in 1 0; do
        JSC_useM68KTemplateJIT=$jit qemu-m68k -cpu m68040 -L /usr/m68k-linux-gnu "$jsc" "$t" > "$work/$n.$jit" 2>&1
        if cmp -s "$work/$n.node" "$work/$n.$jit"; then
            echo "ok   $n jit=$jit"
        else
            echo "FAIL $n jit=$jit"; diff "$work/$n.node" "$work/$n.$jit" | head -8; fail=1
        fi
    done
done
rm -rf "$work"
exit $fail
