#!/usr/bin/env python3
# GFOpenBrowser (DalsinAI/openamigabrowser)
# Copyright (c) 2026 AmigaChrome Team. MIT licence, see LICENSE.
#
# Checks assembled templates (an object holding LLIntAssembly.h and
# LLIntTemplates.h, see README.md): every PC-relative branch or address in a
# template stays inside it, and every hole in the descriptor table points at
# the 0x55555555 placeholder.
#
# usage: check_templates.py nm.txt objdump-d.txt objdump-s-data.txt objdump-s-text.txt
import re
import sys


def load_dump(path):
    mem = {}
    for line in open(path):
        m = re.match(r'^ ([0-9a-f]{4,8}) ((?:[0-9a-f]{2,8} ?){1,4})', line)
        if not m:
            continue
        addr = int(m.group(1), 16)
        for word in m.group(2).split():
            for k in range(0, len(word), 2):
                mem[addr] = int(word[k:k + 2], 16)
                addr += 1
    return mem


def be(mem, addr, n):
    v = 0
    for k in range(n):
        v = (v << 8) | mem[addr + k]
    return v


syms = {}
for line in open(sys.argv[1]):
    parts = line.split()
    if len(parts) == 3:
        syms[parts[2]] = int(parts[0], 16)
ranges = {}
for name, addr in syms.items():
    if name.startswith("Ltj_start_"):
        op = name[len("Ltj_start_"):]
        ranges[op] = (addr, syms["Ltj_end_" + op])
starts = sorted((s, e, op) for op, (s, e) in ranges.items())

bad = 0
current = None
idx = 0
for line in open(sys.argv[2]):
    m = re.match(r'^\s+([0-9a-f]+):\s+(\S+)\s*(.*)$', line)
    if not m:
        continue
    addr = int(m.group(1), 16)
    while idx < len(starts) and starts[idx][1] <= addr:
        idx += 1
    if idx >= len(starts) or not (starts[idx][0] <= addr < starts[idx][1]):
        continue
    s, e, op = starts[idx]
    mnem, args = m.group(2), m.group(3)
    target = None
    if re.match(r'^(b[a-z]{1,3}[swl]?|db[a-z]+|fb[a-z]+)$', mnem) and mnem not in ("bfextu", "bfins"):
        t = re.search(r'\b([0-9a-f]+) <', args)
        if t:
            target = int(t.group(1), 16)
    elif "%pc@(" in args:
        t = re.search(r'%pc@\(([0-9a-f]+)', args)
        target = int(t.group(1), 16)
    if target is not None and not (s <= target <= e):
        print("OUTSIDE %s: %x %s %s" % (op, addr, mnem, args))
        bad += 1

data = load_dump(sys.argv[3])
text = load_dump(sys.argv[4])
holes = 0
for name, addr in syms.items():
    if not name.startswith("_jsc_m68k_tpl_"):
        continue
    op = name[len("_jsc_m68k_tpl_"):]
    start = ranges[op][0]
    n = be(data, addr + 8, 4)
    for k in range(n):
        off = be(data, addr + 12 + 6 * k, 2)
        if be(text, start + off, 4) != 0x55555555:
            print("BAD HOLE %s #%d at +%d" % (op, k, off))
            bad += 1
        holes += 1
print("%d templates, %d holes, %d problems" % (len(ranges), holes, bad))
sys.exit(1 if bad else 0)
