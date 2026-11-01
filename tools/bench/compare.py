#!/usr/bin/env python3
# GFOpenBrowser (DalsinAI/openamigabrowser)
# Copyright (c) 2026 The AmigaChrome Team. MIT licence, see LICENSE.
"""Compare the Amiga's pictures of the benchmark pages with reference
pictures of the same pages at the same size (tools/bench/make-refs.py makes
them with headless Chromium) and give each page a similarity score.

  compare.py AMIGA_PNG_DIR REF_PNG_DIR [--csv compare.csv] [--diff DIR]

Pictures are matched by name (png/<id>.png). For each page:
  ssim        structural similarity of the two pictures in grey, 0..1 (1 is
              the same picture); mostly sees layout, blocks and text lines,
              and forgives small differences in font smoothing
  pixel_match the share of pixels whose colour is within 24 of the
              reference in every channel, 0..1
  score       the page's similarity score: ssim, as a percentage
Both pictures are put on white first (no transparency), and cut to the
size they share when they differ (the size is reported).

Needs Pillow and NumPy (python3 -m pip install pillow numpy).
"""
import argparse
import csv
import os
import sys

try:
    import numpy as np
    from PIL import Image
except ImportError:  # pragma: no cover
    sys.exit("compare.py needs Pillow and NumPy: python3 -m pip install pillow numpy")

TOLERANCE = 24
FIELDS = ["id", "score", "ssim", "pixel_match", "amiga_size", "ref_size", "note"]


def load_rgb(path):
    image = Image.open(path)
    image.load()
    if image.mode in ("RGBA", "LA", "P"):
        image = image.convert("RGBA")
        white = Image.new("RGBA", image.size, (255, 255, 255, 255))
        image = Image.alpha_composite(white, image)
    return np.asarray(image.convert("RGB"), dtype=np.float64)


def box_mean(a, size):
    """The mean over each size x size window (valid windows only)."""
    c = np.cumsum(np.cumsum(np.pad(a, ((1, 0), (1, 0))), axis=0), axis=1)
    s = c[size:, size:] - c[:-size, size:] - c[size:, :-size] + c[:-size, :-size]
    return s / float(size * size)


def ssim(a, b, size=7):
    """Mean structural similarity of two grey pictures (0..255) of one size,
    over size x size windows (Wang et al. 2004, uniform window)."""
    if a.shape[0] < size or a.shape[1] < size:
        return 1.0 if np.array_equal(a, b) else 0.0
    c1, c2 = (0.01 * 255) ** 2, (0.03 * 255) ** 2
    ma, mb = box_mean(a, size), box_mean(b, size)
    va = box_mean(a * a, size) - ma * ma
    vb = box_mean(b * b, size) - mb * mb
    cov = box_mean(a * b, size) - ma * mb
    s = ((2 * ma * mb + c1) * (2 * cov + c2)) / ((ma * ma + mb * mb + c1) * (va + vb + c2))
    return float(np.clip(s.mean(), 0.0, 1.0))


def grey(rgb):
    return rgb[..., 0] * 0.299 + rgb[..., 1] * 0.587 + rgb[..., 2] * 0.114


def compare_images(amiga, ref):
    """(ssim, pixel_match, note) for two RGB arrays."""
    note = ""
    if amiga.shape != ref.shape:
        h, w = min(amiga.shape[0], ref.shape[0]), min(amiga.shape[1], ref.shape[1])
        note = "sizes differ, compared %dx%d" % (w, h)
        amiga, ref = amiga[:h, :w], ref[:h, :w]
    s = ssim(grey(amiga), grey(ref))
    match = float((np.abs(amiga - ref).max(axis=2) <= TOLERANCE).mean())
    return s, match, note


def diff_image(amiga, ref):
    h, w = min(amiga.shape[0], ref.shape[0]), min(amiga.shape[1], ref.shape[1])
    d = np.abs(amiga[:h, :w] - ref[:h, :w]).max(axis=2)
    out = np.full((h, w, 3), 255, dtype=np.uint8)
    out[d > TOLERANCE] = (220, 0, 0)
    faint = (grey(ref[:h, :w]) * 0.25 + 191).astype(np.uint8)
    keep = d <= TOLERANCE
    for c in range(3):
        out[..., c][keep] = faint[keep]
    return Image.fromarray(out)


def compare_dirs(amiga_dir, ref_dir, diff_dir=None):
    rows = []
    names = sorted(n for n in os.listdir(amiga_dir) if n.endswith(".png") and not n.endswith("-dl.png"))
    for name in names:
        ident = name[:-4]
        ref_path = os.path.join(ref_dir, name)
        row = {"id": ident, "score": "", "ssim": "", "pixel_match": "", "amiga_size": "", "ref_size": "", "note": ""}
        try:
            amiga = load_rgb(os.path.join(amiga_dir, name))
        except (OSError, ValueError) as e:
            row["note"] = "Amiga picture unreadable: %s" % e
            rows.append(row)
            continue
        row["amiga_size"] = "%dx%d" % (amiga.shape[1], amiga.shape[0])
        if not os.path.exists(ref_path):
            row["note"] = "no reference"
            rows.append(row)
            continue
        ref = load_rgb(ref_path)
        row["ref_size"] = "%dx%d" % (ref.shape[1], ref.shape[0])
        s, match, note = compare_images(amiga, ref)
        row.update(score=round(s * 100, 1), ssim=round(s, 4), pixel_match=round(match, 4), note=note)
        if diff_dir:
            os.makedirs(diff_dir, exist_ok=True)
            diff_image(amiga, ref).save(os.path.join(diff_dir, ident + "-diff.png"))
        rows.append(row)
    return rows


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("amiga_dir")
    p.add_argument("ref_dir")
    p.add_argument("--csv", help="write the scores here (report.py --compare reads it)")
    p.add_argument("--diff", help="write <id>-diff.png pictures here: red where the pictures differ")
    a = p.parse_args(argv)
    rows = compare_dirs(a.amiga_dir, a.ref_dir, a.diff)
    if a.csv:
        with open(a.csv, "w", newline="", encoding="utf-8") as f:
            w = csv.DictWriter(f, fieldnames=FIELDS)
            w.writeheader()
            w.writerows(rows)
    print("%-14s %6s %7s %8s  %s" % ("page", "score", "ssim", "pixels", "note"))
    for r in rows:
        print("%-14s %6s %7s %8s  %s" % (r["id"], r["score"], r["ssim"], r["pixel_match"], r["note"]))
    return 0


if __name__ == "__main__":
    sys.exit(main())
