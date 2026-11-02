#!/usr/bin/env python3
# GFOpenBrowser (DalsinAI/gfopenbrowser). Copyright (c) 2026 AmiCradle Team. MIT licence, see LICENSE.
"""pc2obj.py MAP NMFILE MAIN_RUNTIME_ADDR ADDR... : find the object a runtime
code address falls in (from the link map) and the nearest symbol inside that
object, local (static) ones included, by running nm on the object itself."""
import bisect, os, re, subprocess, sys
NM = os.path.join(os.environ.get("OS32_GCC16", os.path.expanduser("~/AmiCradle/stoves/os32-gcc16")), "prefix/bin/m68k-amigaos-nm")
mp, nmfile, mainrt, addrs = sys.argv[1], sys.argv[2], int(sys.argv[3], 16), [int(x, 16) for x in sys.argv[4:]]
main = next(int(l.split()[0], 16) for l in open(nmfile) if l.split()[-1:] == ["_main"])
base = mainrt - main
objs = []
lines = open(mp, errors="replace").read().splitlines()
for i, l in enumerate(lines):
    t = l.split()
    if not t or not t[0].startswith(".text"):
        continue
    if len(t) == 1 and i + 1 < len(lines):
        t = t + lines[i + 1].split()
    if len(t) >= 4 and t[1].startswith("0x"):
        try:
            objs.append((int(t[1], 16), int(t[2], 16), t[3]))
        except ValueError:
            pass
objs.sort()
cache = {}
def syms_of(obj):
    if obj in cache:
        return cache[obj]
    m = re.match(r"(.*)\((.*)\)$", obj)
    try:
        if m:
            out = subprocess.run([NM, "-n", "-C", m.group(1)], capture_output=True, text=True).stdout
            # restrict to the member
            res, cur = [], None
            for l in out.splitlines():
                if l.endswith(":"):
                    cur = l[:-1]
                    continue
                if cur == m.group(2):
                    res.append(l)
            out = "\n".join(res)
        else:
            out = subprocess.run([NM, "-n", "-C", obj], capture_output=True, text=True).stdout
    except Exception:
        out = ""
    s = []
    for l in out.splitlines():
        p = l.split(None, 2)
        if len(p) == 3 and p[1] in "Tt":
            s.append((int(p[0], 16), p[2]))
    cache[obj] = sorted(s)
    return cache[obj]
starts = [o[0] for o in objs]
for a in addrs:
    off = a - base
    i = bisect.bisect_right(starts, off) - 1
    if i < 0 or off >= objs[i][0] + objs[i][1]:
        print(f"{a:#010x}  (not in jsc text)")
        continue
    ostart, size, obj = objs[i]
    rel = off - ostart
    s = syms_of(obj)
    k = bisect.bisect_right([x for x, _ in s], rel) - 1
    name = s[k][1] if k >= 0 else "?"
    print(f"{a:#010x}  {os.path.basename(obj)[:60]}  {name[:140]} +{rel - (s[k][0] if k >= 0 else 0):#x}")
