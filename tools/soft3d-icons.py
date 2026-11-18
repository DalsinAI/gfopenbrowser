#!/usr/bin/env python3
# GFOpenBrowser (DalsinAI/gfopenbrowser)
# Copyright (c) 2026 AmiCradle Team. MIT licence, see LICENSE.
"""GFOpenBrowser's Workbench icons in the Soft 3D style: its drawer, the program, its readme and the Install icon.

    tools/soft3d-icons.py --gfopenup DIR --guest DIR [ROOT]   the four .info files and icons/art/*.png
    tools/soft3d-icons.py --gfopenup DIR --sheet OUT.png      the preview sheet

Drawn after AmiCradle's icon guide (amicradle docs/howto/12-draw-icons.md, Part 2), as GFOffice's icons are
(gfoffice tools/icons/gfoffice_icons_art.py): each icon is one object, drawn flat on a 512 square, then tilted,
given its thickness, lit, edged and shadowed by the Soft 3D kit (gfopenup tools/soft3d_drives_art.py), so it sits
with the drives and the drawer, project and tool kinds. Chosen, the kit lifts it with the blue glow and one thing
in the picture moves. Two drawings each: about five features for 64 px, about three for 32 px. No text, no logos.

  GFOpenBrowser (drawer)   the Soft 3D drawer with a plate on its front: the globe
  GFOpenBrowser (program)  a globe, the web, and the pointer that follows a link; chosen, the pointer goes in
  GFOpenBrowser.readme     the Soft 3D page of writing with a small globe at its foot
  Install-GFOpenBrowser    an open tray and the globe coming down into it on the blue arrow; chosen, it lands

Each .info keeps what the icon it replaces says besides its pictures: its type, default tool, tool types,
stack and drawer window. The kit comes from a gfopenup checkout and the .info writer from an amicradle-guest
checkout (icons/openicons); neither is needed to build or package the browser, only to draw the icons again.
"""
from __future__ import annotations

import argparse
import sys
from pathlib import Path

d3 = kinds = None                       # the Soft 3D kit, from a gfopenup checkout (load_kit)

EDGE, SLATE, GREY, SILVER, PAPER = (22, 26, 36), (70, 76, 90), (130, 138, 152), (196, 202, 212), (246, 248, 251)
NAVY, BLUE, SKY = (40, 64, 120), (11, 94, 215), (74, 163, 255)
GREEN, MINT = (30, 120, 70), (52, 170, 156)
INK = (120, 140, 176)

# icon name -> the file it is, from the repository's root
FILES = {"drawer": "icons/GFOpenBrowser.info",
         "program": "icons/GFOpenBrowser/GFOpenBrowser.info",
         "readme": "icons/GFOpenBrowser/GFOpenBrowser.readme.info",
         "install": "dist/Install-GFOpenBrowser.info"}
LABELS = {"drawer": "GFOpenBrowser drawer", "program": "GFOpenBrowser", "readme": "readme", "install": "Install"}
NAMES = list(FILES)


def load_kit(gfopenup: Path):
    global d3, kinds
    sys.path.insert(0, str(gfopenup / "tools"))
    import soft3d_drives_art as _d3                                             # noqa: E402
    import soft3d_kinds_art as _kinds                                           # noqa: E402
    d3, kinds = _d3, _kinds


PLATE = [92, 250, 420, 462]             # the plate on the drawer's front flap, over its handle


def framed(f, b, r, col, top=None, edge=EDGE, w=6):
    """a light shape on its dark frame, 6 units wider (the guide's rule 6)"""
    f.rr([b[0] - w, b[1] - w, b[2] + w, b[3] + w], r + w, edge)
    f.rr(b, r, col, top)


def globe(f, cx, cy, r, small, frame=True, land=True):
    """the web: a blue globe, its land, the equator and one meridian"""
    if frame:
        f.ell([cx - r - 6, cy - r - 6, cx + r + 6, cy + r + 6], EDGE)
    f.ell([cx - r, cy - r, cx + r, cy + r], BLUE, SKY)
    k = r / 100.0
    if land:                                                                     # two masses, inside the globe
        f.poly([(cx - 62 * k, cy - 58 * k), (cx - 10 * k, cy - 74 * k), (cx + 8 * k, cy - 36 * k),
            (cx - 24 * k, cy - 8 * k), (cx - 66 * k, cy - 20 * k)], MINT)
        f.poly([(cx + 14 * k, cy + 8 * k), (cx + 66 * k, cy - 6 * k), (cx + 70 * k, cy + 36 * k),
            (cx + 34 * k, cy + 68 * k), (cx + 8 * k, cy + 44 * k)], GREEN)
    w = (16 if small else 7) * k
    line = d3.light(SKY, 0.55)
    f.line([(cx - r + 4 * k, cy), (cx + r - 4 * k, cy)], w, line)
    f.ring([cx - r * 0.46, cy - r, cx + r * 0.46, cy + r], w, line)
    if not small:
        f.ring([cx - r, cy - r, cx + r, cy + r], w, d3.light(SKY, 0.3))


def pointer(f, x, y, s):
    """the Workbench pointer, its point at x, y"""
    pts = [(x + px * s, y + py * s) for px, py in
           ((0, 0), (0, 150), (40, 114), (70, 176), (104, 160), (74, 100), (124, 96))]
    f.line(pts + pts[:2], 22 * s, EDGE)                                           # its dark frame
    f.poly(pts, PAPER)


def draw_program(f, small, chosen=False):
    """a globe and the pointer; chosen, the pointer goes in to the link it follows"""
    globe(f, 236, 236, 206, small)
    px, py = (250, 250) if chosen else (318, 318)
    pointer(f, px, py, 1.35 if small else 1.1)
    return 34


def draw_drawer(f, small, chosen=False):
    thick = kinds.draw_drawer(f, small, chosen)
    framed(f, PLATE, 16, PAPER, (255, 255, 255))
    globe(f, 256, 356, 92, small, land=not small)
    return thick


def draw_readme(f, small, chosen=False):
    thick = kinds.draw_project(f, small, chosen)
    globe(f, 318, 400, 84 if small else 70, small, land=not small)
    return thick


def draw_install(f, small, chosen=False):
    """an open tray and the globe coming down into it on the blue arrow; chosen, it lands"""
    drop = 70 if chosen else 0
    f.rr([36, 300, 476, 486], 26, d3.dark(SILVER, 0.25), d3.light(SILVER, 0.2))         # the tray
    f.rr([86, 268, 426, 420], 14, SLATE)                                             # its open inside
    r = 112 if small else 104
    cy = 112 + drop
    w = 120 if small else 96
    f.rr([256 - w // 2, cy + r - 30, 256 + w // 2, 300 + drop], 12, BLUE, d3.light(BLUE, 0.4))   # the arrow
    f.poly([(256 - w - 30, 290 + drop), (256 + w + 30, 290 + drop), (256, 400 + drop)], d3.light(BLUE, 0.1))
    globe(f, 256, cy, r, small, land=not small)
    f.rr([36, 400, 476, 486], 26, SILVER, d3.light(SILVER, 0.5))                     # the tray's front
    if not small:
        f.rr([196, 436, 316, 456], 10, GREY)                                         # its handle
    return 36


DRAW = {"drawer": draw_drawer, "program": draw_program, "readme": draw_readme, "install": draw_install}


# ---- rendering, as soft3d_kinds_art renders a kind ---------------------------------------------------

def render(name, S, chosen, small):
    from PIL import Image
    f = d3.Flat(S)
    thick = DRAW[name](f, small, chosen)
    im = d3.solid(f.img, thick, S, chosen)
    if chosen:                                                                     # lifted 2 px at 64
        up = Image.new("RGBA", (S, S))
        up.alpha_composite(im, (0, 0), (0, int(16 * S / 512)))
        im = up
    return im


_CACHE = {}


def frames(name, size, chosen):
    """NAME at rest or chosen, size x size, hard alpha: both frames cut to one square, as the kit's frames()"""
    from PIL import ImageChops
    big = size >= 48
    key = (name, big)
    if key not in _CACHE:
        S = 512 if big else 256
        pair = [render(name, S, c, not big) for c in (False, True)]
        m = ImageChops.lighter(pair[0].getchannel("A"), pair[1].getchannel("A")).point(lambda v: 255 if v >= 60 else 0)
        x0, y0, x1, y1 = m.getbbox()
        side = int(max(x1 - x0, y1 - y0) * 1.06)
        cx, cy = (x0 + x1) // 2, (y0 + y1) // 2
        _CACHE[key] = [p.crop((cx - side // 2, cy - side // 2, cx - side // 2 + side, cy - side // 2 + side))
                       for p in pair]
    im = d3.hard(_CACHE[key][1 if chosen else 0], size)
    return im if big else d3.edged(im)


# ---- the .info files ---------------------------------------------------------------------------------

def info_bytes(name, guest: Path, old: bytes):
    """NAME's icon file: an OS 3.5 GlowIcon, two 64 px frames of up to 255 colours each (median cut, no dither,
    colour 0 see-through under 110 alpha) and the 4 colour classic picture for OS 3.1. What OLD, the icon it
    replaces, says besides its pictures is kept."""
    import numpy as np
    sys.path.insert(0, str(guest / "icons" / "openicons"))
    from openicons import info as infofmt                                         # noqa: E402
    from openicons.render import planar_from                                      # noqa: E402
    images, planar = [], []
    for chosen in (False, True):
        fr = frames(name, 64, chosen)
        q = fr.convert("RGB").quantize(colors=255, method=0, dither=0)
        idx = np.array(q, np.int32) + 1
        idx[np.array(fr)[..., 3] < 110] = 0
        pal = q.getpalette()[:255 * 3]
        pal = [(0, 0, 0)] + [tuple(pal[i:i + 3]) for i in range(0, len(pal), 3)]
        pal = pal[:int(idx.max()) + 1]
        images.append((idx.reshape(-1).tolist(), pal))
        planar.append(planar_from(idx, pal))
    was = infofmt.read_info(old)
    icon = dict(type=was["type"], width=64, height=64, planar=planar, planar_depth=2,
                colour=dict(images=images, transparent=0, frameless=True),
                default_tool=was.get("default_tool"), tooltypes=list(was.get("tooltypes") or []),
                stack=was.get("stack", 0))
    if was.get("drawer"):
        icon["drawer"] = dict(was["drawer"], flags=0, viewmodes=0)
    data = infofmt.write_info(icon)
    errs = infofmt.validate(icon, data)
    if errs:
        raise infofmt.InfoError(f"{name}: " + "; ".join(errs))
    return data


def write_all(root: Path, guest: Path):
    art = root / "icons" / "art"
    art.mkdir(parents=True, exist_ok=True)
    for name in NAMES:
        for chosen, state in ((False, ""), (True, "-selected")):
            frames(name, 64, chosen).save(art / f"{name}{state}.png", optimize=True)
        path = root / FILES[name]
        path.write_bytes(info_bytes(name, guest, path.read_bytes()))
    sheet(root / "icons" / "preview.png")
    print(f"{len(NAMES)} icons in {root}")


# ---- the preview sheet -------------------------------------------------------------------------------

def sheet(out: Path):
    from PIL import Image, ImageDraw
    rows = [("64 px", 64, False, False, 1), ("64 px chosen", 64, True, False, 1), ("32 px", 32, False, False, 1),
            ("32 px chosen", 32, True, False, 1), ("32 px, 16 colours", 32, False, True, 1),
            ("64 px at 2x", 64, False, False, 2), ("64 px chosen at 2x", 64, True, False, 2),
            ("32 px at 3x", 32, False, False, 3)]
    cell, left, top = 176, 170, 40
    hs = [max(n * z, 40) + 16 for _, n, _, _, z in rows]
    block = top + sum(hs)
    width = left + cell * len(NAMES) + 20
    im = Image.new("RGBA", (width, 2 * (block + 16)), (255, 255, 255, 255))
    dr = ImageDraw.Draw(im)
    f, fb = d3.font(13), d3.font(15)
    y0 = 0
    for bg, ink in (((232, 234, 238), (30, 34, 44)), ((43, 47, 54), (230, 232, 236))):
        dr.rectangle([0, y0, im.width, y0 + block], fill=bg)
        for i, name in enumerate(NAMES):
            x = left + i * cell + cell // 2
            lab = LABELS[name]
            dr.text((x - dr.textlength(lab, font=fb) / 2, y0 + 12), lab, font=fb, fill=ink)
        y = y0 + top
        for (lab, n, chosen, chip, z), h in zip(rows, hs):
            dr.text((16, y + h // 2 - 8), lab, font=f, fill=ink)
            for i, name in enumerate(NAMES):
                p = frames(name, n, chosen)
                if chip:
                    p = d3.chipset(p)
                p = d3.blow(p, z)
                im.alpha_composite(p, (left + i * cell + (cell - p.width) // 2, y + (h - p.height) // 2))
            y += h
        y0 += block + 16
    im.convert("RGB").save(out)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("out", type=Path, nargs="?", default=Path(__file__).resolve().parents[1])
    ap.add_argument("--gfopenup", type=Path, required=True, help="a gfopenup checkout (the Soft 3D kit)")
    ap.add_argument("--guest", type=Path, help="an amicradle-guest checkout (icons/openicons), for the .info files")
    ap.add_argument("--sheet", action="store_true")
    a = ap.parse_args(argv)
    load_kit(a.gfopenup)
    if a.sheet:
        sheet(a.out)
        return 0
    if not a.guest:
        ap.error("--guest is needed to write the .info files")
    write_all(a.out, a.guest)
    return 0


if __name__ == "__main__":
    sys.exit(main())
