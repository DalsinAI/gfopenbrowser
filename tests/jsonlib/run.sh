#!/bin/sh
# Copyright (c) 2026 AmiCradle Team. MIT licence (see LICENSE at the repository root).
# json.library's core (src/jsonlib/json_core.cpp), lean C++: built with the
# host's C++ compiler (with AddressSanitizer and UBSan) and with an m68k
# cross G++ for the 68040 at -O2 and -Os, checked with nm to call nothing
# but libgcc and to hold no C++ runtime and no data of its own, and run
# over the cases tests/jsonlib/json_ref.py writes: on the host, and under
# qemu-m68k for the 68040 builds.
#   sh tests/jsonlib/run.sh
#   HOST_CXX, HOST_CC   host compilers (default c++, cc)
#   M68K_PREFIX         cross tools (default m68k-linux-gnu-)
#   QEMU_M68K           (default qemu-m68k)
#   OPENSDK_SRC         an openamigasdk checkout: also check the SDK headers
set -eu
here=$(cd "$(dirname "$0")" && pwd)
src=$here/../../src/jsonlib
hcxx=${HOST_CXX:-c++}
hcc=${HOST_CC:-cc}
prefix=${M68K_PREFIX:-m68k-linux-gnu-}
qemu=${QEMU_M68K:-qemu-m68k}
tmp=$(mktemp -d); trap 'rm -rf "$tmp"' EXIT
fail=0

python3 -I "$here/json_ref.py" gen "$tmp/cases"

LEAN="-std=gnu++23 -fno-exceptions -fno-rtti -fno-threadsafe-statics
  -fno-asynchronous-unwind-tables -fno-unwind-tables -nostdlib++
  -fno-lto -fno-builtin -fno-tree-loop-distribute-patterns -Wall -Wextra -Werror"

run_cases() {   # name, program, runner...
    name=$1; shift
    if "$@" "$tmp/cases/script.txt" > "$tmp/out.txt" &&
       res=$(python3 -I "$here/json_ref.py" check "$tmp/cases" "$tmp/out.txt"); then
        echo "ok   $name: $(echo "$res" | tail -1)"
    else
        echo "FAIL $name:"; echo "${res:-}" | cut -c1-300 | head -40
        fail=1
    fi
}

# shellcheck disable=SC2086
"$hcxx" $LEAN -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -c "$src/json_core.cpp" -o "$tmp/core.o"
"$hcc" -std=gnu99 -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -Wall -Wextra -Werror -I"$src" \
    "$here/json_test.c" "$tmp/core.o" -fsanitize=address,undefined -lstdc++ -o "$tmp/t"
run_cases "host, sanitizers" "$tmp/t"

# the shell's vector table follows sfd/json_lib.sfd; with OPENSDK_SRC (an
# openamigasdk checkout), so do the SDK headers, and the shell and JSONTest
# compile against GFOpenNDK (with an m68k cross GCC, as plain C: REG is a
# plain parameter there, the Amiga GCC's register arguments aside)
if res=$(python3 -I "$src/check_vectors.py"); then echo "ok   $res"; else echo "FAIL $res"; fail=1; fi
if [ -n "${OPENSDK_SRC:-}" ]; then
    ndk=$OPENSDK_SRC/OpenNDK/Include_H
    if python3 "$OPENSDK_SRC/OpenNDK/tools/ndkgen.py" --check --out "$src/sdk" "$src/sfd/json_lib.sfd" >/dev/null &&
       "${prefix}gcc" -m68040 -Os -std=gnu99 -D'REG(r,decl)=decl' -fsyntax-only -ffreestanding -nostdinc \
           -Wall -Wextra -Werror -Wno-unused-parameter -I"$ndk" -I"$src/sdk/Include_H" -I"$src" "$src/openjson_lib.c" &&
       "${prefix}gcc" -m68040 -Os -std=gnu99 -fsyntax-only -Wall -Wextra -Werror -Wno-pointer-sign -Wno-format \
           -I"$ndk" -I"$src/sdk/Include_H" "$src/tests/JSONTest.c"; then
        echo "ok   the SDK headers follow the SFD; the shell and JSONTest compile against GFOpenNDK"
    else
        echo "FAIL the SDK headers or the GFOpenNDK compile"; fail=1
    fi
fi

# JSONTest, the Amiga self-test, over the core (JSON_HOSTED)
jsontest() {    # name, runner...
    name=$1; shift
    if "$@" > "$tmp/jt.txt" && grep -q ' 0 failed$' "$tmp/jt.txt"; then
        echo "ok   $name: $(tail -1 "$tmp/jt.txt")"
    else
        echo "FAIL $name:"; grep -v '^ok' "$tmp/jt.txt" | head -20; fail=1
    fi
}
"$hcc" -DJSON_HOSTED -std=gnu99 -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -Wall -Wextra -Werror \
    -Wno-pointer-sign -Wno-unused-function -Wno-format -I"$here" -I"$src" -I"$src/sdk/Include_H" \
    "$src/tests/JSONTest.c" "$tmp/core.o" -lstdc++ -o "$tmp/jt"
jsontest "JSONTest hosted, host" "$tmp/jt"

if ! command -v "${prefix}g++" >/dev/null; then
    echo "skipped the 68040 builds: no ${prefix}g++"
    exit $fail
fi
for opt in -O2 -Os; do
    # shellcheck disable=SC2086
    "${prefix}g++" $LEAN $opt -m68040 -fno-delete-null-pointer-checks -c "$src/json_core.cpp" -o "$tmp/core68.o"
    und=$("${prefix}nm" -u "$tmp/core68.o" | awk '{print $NF}' |
          grep -v '^__\(mul\|div\|udiv\|mod\|umod\|ashl\|ashr\|lshr\)[sd]i3$' | tr '\n' ' ' || true)
    [ -z "$und" ] || { echo "FAIL $opt: the core calls $und"; fail=1; }
    bad=$("${prefix}nm" "$tmp/core68.o" | awk '{print $NF}' |
          grep -E '^(__cxa_|__gxx_personality|_ZTI|_ZTS|_ZTV|_GLOBAL__sub_I_|_ZGV|_Znw|_Zna|_ZdlPv|_ZdaPv)' |
          tr '\n' ' ' || true)
    [ -z "$bad" ] || { echo "FAIL $opt: C++ runtime in the core: $bad"; fail=1; }
    data=$("${prefix}size" -A "$tmp/core68.o" | awk '$1 ~ /^\.(data|bss|init_array|ctors)/ && $2 > 0 {print $1}' |
           tr '\n' ' ' || true)
    [ -z "$data" ] || { echo "FAIL $opt: the core keeps state in $data"; fail=1; }
    echo "     $opt core: $("${prefix}size" "$tmp/core68.o" | awk 'NR==2 {print $1 " bytes of code and constants"}')"
    "${prefix}gcc" -std=gnu99 $opt -m68040 -static -Wall -Wextra -Werror -I"$src" \
        "$here/json_test.c" "$tmp/core68.o" -o "$tmp/t68"
    "${prefix}gcc" -DJSON_HOSTED -std=gnu99 $opt -m68040 -static -Wall -Wextra -Werror -Wno-pointer-sign -Wno-format \
        -Wno-unused-function -I"$here" -I"$src" -I"$src/sdk/Include_H" "$src/tests/JSONTest.c" "$tmp/core68.o" -o "$tmp/jt68"
    if command -v "$qemu" >/dev/null; then
        run_cases "68040 $opt under qemu" "$qemu" -cpu m68040 "$tmp/t68"
        jsontest "JSONTest hosted, 68040 $opt under qemu" "$qemu" -cpu m68040 "$tmp/jt68"
    else
        echo "ok   68040 $opt: built (no $qemu to run it)"
    fi
done
exit $fail
