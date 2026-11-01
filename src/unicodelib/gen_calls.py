#!/usr/bin/env python3
# unicode.library: makes the call table from calls.txt.
# Copyright (c) 2026 AmigaChrome Team. MIT.
#
#   gen_calls.py CALLS OUT_DIR [OWN]
#
# OUT_DIR/ul_table.c   the library's table: entry N points at ICU's Nth call,
#                      or at our own code's (uo_<call>) when OWN (own.txt)
#                      names that call
# OUT_DIR/ul_count.h   UL_CALLS, the number of entries
# OUT_DIR/ul_stubs.s   for programs: one routine per call under ICU's own
#                      name, which jumps through the table the program got
#                      from UnicodeTable() (ul_client.c); until then the
#                      table is a boot table whose entries open the library
#
# A stub only uses a0, which the m68k calling convention leaves to the
# callee: the arguments stay on the stack, a1 keeps a structure result's
# address, and the jump returns straight to the caller.
import os
import re
import sys

SUFFIX = "_78"


def read_calls(path):
    names = []
    for line in open(path):
        line = line.split("#", 1)[0].strip()
        if not line:
            continue
        if not re.fullmatch(r"(u[a-z0-9]*|UCNV)_[A-Za-z0-9_]+", line):
            sys.exit(f"gen_calls.py: {path}: not an ICU C call: {line!r}")
        if line in names:
            sys.exit(f"gen_calls.py: {path}: {line} twice")
        names.append(line)
    return names


def main():
    calls, out = sys.argv[1], sys.argv[2]
    names = read_calls(calls)
    own = set(read_calls(sys.argv[3])) if len(sys.argv) > 3 else set()
    for n in sorted(own - set(names)):
        sys.exit(f"gen_calls.py: {sys.argv[3]}: {n} is not one of calls.txt's")
    os.makedirs(out, exist_ok=True)
    with open(os.path.join(out, "ul_count.h"), "w") as f:
        f.write(f"/* made by gen_calls.py from calls.txt */\n#define UL_CALLS {len(names)}\n")
    with open(os.path.join(out, "ul_table.c"), "w") as f:
        f.write("/* made by gen_calls.py from calls.txt: unicode.library's table */\n")
        f.write("#include \"ul_count.h\"\n\n")
        target = [f"uo_{n}" if n in own else f"{n}{SUFFIX}" for n in names]
        for t in target:
            f.write(f"extern char {t}[];\n")
        f.write("\nvoid *const ul_table[UL_CALLS] = {\n")
        for n, t in zip(names, target):
            f.write(f"    {t},{'   /* ours */' if n in own else ''}\n")
        f.write("};\n")
    with open(os.path.join(out, "ul_stubs.s"), "w") as f:
        f.write("| made by gen_calls.py from calls.txt: ICU's calls, through unicode.library's table\n")
        f.write("\t.text\n")
        for i, n in enumerate(names):
            f.write(f"\t.globl\t_{n}{SUFFIX}\n_{n}{SUFFIX}:\n")
            f.write(f"\tmove.l\t___ul_table,a0\n\tmove.l\t({i * 4},a0),a0\n\tjmp\t(a0)\n")
        # Until the library is open, the table is this one: each entry opens
        # it (ul_client.c), then goes round its call's routine again.
        for i, n in enumerate(names):
            f.write(f".Lboot{i}:\n\tbsr.w\tulBoot\n\tbra.w\t_{n}{SUFFIX}\n")
        f.write("ulBoot:\n\tmovem.l\td0-d1/a0-a1,-(sp)\n\tjsr\t___ul_bootstrap\n")
        f.write("\tmovem.l\t(sp)+,d0-d1/a0-a1\n\trts\n")
        f.write("\t.data\n\t.balign\t4\n\t.globl\t___ul_boot_table\n___ul_boot_table:\n")
        for i in range(len(names)):
            f.write(f"\t.long\t.Lboot{i}\n")
    print(f"gen_calls.py: {len(names)} calls, {len(own)} of them our own code")


main()
