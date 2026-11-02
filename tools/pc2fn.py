#!/usr/bin/env python3
# GFOpenBrowser (DalsinAI/gfopenbrowser). Copyright (c) 2026 AmiCradle Team. MIT licence, see LICENSE.
"""pc2fn.py NMFILE MAIN_RUNTIME_ADDR ADDR... : map runtime code addresses to
functions using `nm -n` of the hunk executable (text symbols only; C++
names that demangle without a parameter list are variables such as s_info,
which the hunk format puts in the code, and are skipped)."""
import subprocess, sys
nm, mainrt, addrs = sys.argv[1], int(sys.argv[2], 16), [int(x, 16) for x in sys.argv[3:]]
cand = []
for line in open(nm):
    t = line.split()
    if len(t) != 3 or t[1] not in "Tt":
        continue
    cand.append((int(t[0], 16), t[2]))
def demangle_all(ns):
    try:
        out = subprocess.run(["c++filt", "-_"], input="\n".join(ns) + "\n",
                             capture_output=True, text=True).stdout.split("\n")
        if len(out) >= len(ns):
            return out[:len(ns)]
    except Exception:
        pass
    return list(ns)
plain = demangle_all([n for _, n in cand])
syms = [(a, n) for (a, n), d in zip(cand, plain)
        if not (n.startswith("__Z") and "(" not in d)]
syms.sort()
main = next(a for a, n in syms if n == "_main")
base = mainrt - main
names = {}
def demangle(n):
    try:
        return subprocess.run(["c++filt", "-_"], input=n, capture_output=True, text=True).stdout.strip()
    except Exception:
        return n
import bisect
keys = [a for a, _ in syms]
for a in addrs:
    off = a - base
    i = bisect.bisect_right(keys, off) - 1
    if i < 0 or off > keys[-1] + 0x100000:
        print(f"{a:#010x}  (outside jsc text)")
        continue
    s, n = syms[i]
    print(f"{a:#010x}  {demangle(n)[:150]} +{off - s:#x}")
