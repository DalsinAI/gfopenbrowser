#!/usr/bin/env python3
# GFOpenBrowser (DalsinAI/gfopenbrowser)
# Copyright (c) 2026 AmiCradle Team. MIT licence, see LICENSE.
"""The package's four Workbench icons, as they are committed (tools/soft3d-icons.py draws them).

    python3 tests/pack/test_icons.py

Reads each .info with its own small reader (no kit, no Pillow) and checks what Workbench needs from it:
the DiskObject's magic and type, 64 x 64, both classic pictures, the default tool, tool types and stack the
package relies on, a drawer's window, and the FORM ICON after them with a FACE of 64 x 64 and two IMAG
pictures, the chosen one differing from the one at rest. The pictures' look is for the eye: icons/preview.png.
"""
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
DRAWER, TOOL, PROJECT = 2, 3, 4
WANT = {
    "icons/GFOpenBrowser.info": dict(type=DRAWER),
    "icons/GFOpenBrowser/GFOpenBrowser.info": dict(type=TOOL, stack=65536),
    "icons/GFOpenBrowser/GFOpenBrowser.readme.info": dict(type=PROJECT, tool=True),
    "dist/Install-GFOpenBrowser.info": dict(type=PROJECT, tool="Installer",
                                            tooltypes=["APPNAME=", "MINUSER=", "DEFUSER=", "LOGFILE="]),
}
passed = failed = 0


def ok(cond, what):
    global passed, failed
    if cond:
        passed += 1
    else:
        failed += 1
        print("FAIL: " + what)


def string(b, o):
    (n,) = struct.unpack_from(">L", b, o)
    return b[o + 4:o + 4 + n].rstrip(b"\0").decode("latin-1"), o + 4 + n


def read(b):
    """the parts of a .info this test looks at; raises on a file that runs short"""
    magic, version = struct.unpack_from(">HH", b, 0)
    w, h = struct.unpack_from(">hh", b, 12)
    render, select = struct.unpack_from(">LL", b, 22)
    kind, _, deftool, tts, _cx, _cy, drawer, toolwin, stack = struct.unpack_from(">BBLLllLLl", b, 48)
    r = dict(magic=magic, version=version, width=w, height=h, type=kind, stack=stack, drawer=bool(drawer),
             classic=[], tool=None, tooltypes=[])
    o = 78 + (56 if drawer else 0)
    for present in (render, select):
        if present:
            _l, _t, iw, ih, depth = struct.unpack_from(">hhhhh", b, o)
            o += 20
            size = ((iw + 15) // 16) * 2 * ih * depth
            r["classic"].append((iw, ih, depth, b[o:o + size]))
            o += size
    if deftool:
        r["tool"], o = string(b, o)
    if tts:
        (n,) = struct.unpack_from(">L", b, o)
        o += 4
        for _ in range(n // 4 - 1):
            s, o = string(b, o)
            r["tooltypes"].append(s)
    if toolwin:
        _, o = string(b, o)
    if drawer and (struct.unpack_from(">L", b, 44)[0] & 0xFF) >= 1:
        o += 6
    r["form"] = b[o:o + 4] == b"FORM" and b[o + 8:o + 12] == b"ICON"
    r["face"], r["imag"] = None, []
    if r["form"]:
        (flen,) = struct.unpack_from(">L", b, o + 4)
        r["form_ends_file"] = o + 8 + flen == len(b)
        p, end = o + 12, o + 8 + flen
        while p + 8 <= end:
            cid, clen = b[p:p + 4], struct.unpack_from(">L", b, p + 4)[0]
            body = b[p + 8:p + 8 + clen]
            if cid == b"FACE":
                r["face"] = (body[0] + 1, body[1] + 1)
            elif cid == b"IMAG":
                r["imag"].append(body)
            p += 8 + clen + (clen & 1)
    return r


for name, want in WANT.items():
    path = ROOT / name
    ok(path.is_file(), f"{name} is there")
    if not path.is_file():
        continue
    try:
        r = read(path.read_bytes())
    except (struct.error, IndexError) as e:
        ok(False, f"{name} reads as a .info ({e})")
        continue
    ok(r["magic"] == 0xE310 and r["version"] == 1, f"{name}: DiskObject magic and version")
    ok(r["type"] == want["type"], f"{name}: type {want['type']} (got {r['type']})")
    ok((r["width"], r["height"]) == (64, 64), f"{name}: 64 x 64 (got {r['width']} x {r['height']})")
    ok(len(r["classic"]) == 2 and all(c[:3] == (64, 64, 2) for c in r["classic"]),
       f"{name}: two classic pictures, 64 x 64 in 4 colours")
    ok(len(r["classic"]) == 2 and r["classic"][0][3] != r["classic"][1][3] and any(r["classic"][0][3]),
       f"{name}: the classic pictures are drawn, and the chosen one differs")
    ok(r["drawer"] == (want["type"] == DRAWER), f"{name}: a drawer window only on the drawer")
    if "stack" in want:
        ok(r["stack"] == want["stack"], f"{name}: stack {want['stack']} (got {r['stack']})")
    if want.get("tool") is True:
        ok(bool(r["tool"]), f"{name}: has a default tool")
    elif want.get("tool"):
        ok(r["tool"] == want["tool"], f"{name}: default tool {want['tool']} (got {r['tool']})")
    for t in want.get("tooltypes", []):
        ok(any(s.strip().startswith(t) for s in r["tooltypes"]), f"{name}: tool type {t} (got {r['tooltypes']})")
    ok(r["form"] and r.get("form_ends_file"), f"{name}: a FORM ICON follows and ends the file")
    ok(r["face"] == (64, 64), f"{name}: FACE 64 x 64 (got {r['face']})")
    ok(len(r["imag"]) == 2 and r["imag"][0] != r["imag"][1], f"{name}: two IMAG pictures, the chosen one differs")

print(f"icons: {passed} passed, {failed} failed")
sys.exit(1 if failed else 0)
