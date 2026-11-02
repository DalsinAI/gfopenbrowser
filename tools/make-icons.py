#!/usr/bin/env python3
# GFOpenBrowser (DalsinAI/gfopenbrowser)
# Copyright (c) 2026 AmiCradle Team. MIT licence, see LICENSE.
#
# Draws GFOpenBrowser's Workbench icons in the GlowIcons style and writes them
# as OS 3.5+ colour icons (.info): a classic 4-colour image for old
# Workbenches, then a FORM ICON with the normal and the selected (glowing)
# image. AmigaOS 3.2 shows the colour images. The artwork is original.
#
#   tools/make-icons.py [OUT_DIR]      (default icons/)
#
# Writes OUT_DIR/GFOpenBrowser.info (the drawer), OUT_DIR/GFOpenBrowser/
# GFOpenBrowser.info (the program), OUT_DIR/GFOpenBrowser/GFOpenBrowser.readme.info
# and a preview.png. Needs Python 3 with numpy and Pillow.
import os
import struct
import sys

import numpy as np
from PIL import Image

W = H = 64          # the icon, sized for the new icon set (64 x 64)
DRAW = 46           # drawn in 46-unit coordinates, scaled to fill it
SS = 8              # supersampling
ART = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'icons', 'art')

# --- drawing ---------------------------------------------------------------

def grid():
    ys, xs = np.mgrid[0:H * SS, 0:W * SS].astype(np.float64)
    return (xs + 0.5) / SS * DRAW / W, (ys + 0.5) / SS * DRAW / H


X, Y = grid()


def rgb(h):
    h = h.lstrip('#')
    return np.array([int(h[i:i + 2], 16) / 255.0 for i in (0, 2, 4)])


def mix(a, b, t):
    t = np.clip(t, 0, 1)[..., None]
    return a * (1 - t) + b * t


class Canvas:
    def __init__(self):
        self.col = np.zeros((H * SS, W * SS, 3))
        self.a = np.zeros((H * SS, W * SS))

    def paint(self, mask, colour):
        """mask: bool array; colour: (h, w, 3) array or a single rgb."""
        colour = np.broadcast_to(colour, self.col.shape)
        self.col[mask] = colour[mask]
        self.a[mask] = 1.0

    def shape(self, sdf, fill, outline, width=1.0):
        """Fill sdf < 0, with an outline band of `width` pixels inside it."""
        inside = sdf < 0
        self.paint(inside, fill)
        self.paint(inside & (sdf > -width), outline)

    def image(self):
        """Down-sample to W x H RGBA, alpha cut at one half (icons have a
        single transparent colour)."""
        a = self.a.reshape(H, SS, W, SS).mean(axis=(1, 3))
        c = (self.col * self.a[..., None]).reshape(H, SS, W, SS, 3).sum(axis=(1, 3))
        c = c / np.maximum(a * SS * SS, 1e-9)[..., None]
        out = np.zeros((H, W, 4), np.uint8)
        out[..., :3] = np.clip(c * 255 + 0.5, 0, 255)
        out[..., 3] = np.where(a >= 0.5, 255, 0)
        return out


def circle(cx, cy, r):
    return np.hypot(X - cx, Y - cy) - r


def rrect(x0, y0, x1, y1, r):
    cx, cy = (x0 + x1) / 2, (y0 + y1) / 2
    hx, hy = (x1 - x0) / 2 - r, (y1 - y0) / 2 - r
    dx, dy = np.abs(X - cx) - hx, np.abs(Y - cy) - hy
    return np.hypot(np.maximum(dx, 0), np.maximum(dy, 0)) + np.minimum(np.maximum(dx, dy), 0) - r


def polygon(pts):
    """Signed distance to a convex polygon given clockwise on screen."""
    d = np.full(X.shape, -1e9)
    n = len(pts)
    for i in range(n):
        (x0, y0), (x1, y1) = pts[i], pts[(i + 1) % n]
        ex, ey = x1 - x0, y1 - y0
        l = np.hypot(ex, ey)
        # outward normal of a clockwise (screen) polygon
        nx, ny = ey / l, -ex / l
        d = np.maximum(d, (X - x0) * nx + (Y - y0) * ny)
    return d


LIGHT = np.array([-0.55, -0.65, 0.53])
LIGHT /= np.linalg.norm(LIGHT)


def globe(cv, cx, cy, r, ring=True, clip=None):
    """The GFOpenBrowser mark: a shaded blue globe with a graticule, wrapped
    in a tilted orange orbit."""
    ang = np.radians(-22)
    rx, ry, rw = r * 1.32, r * 0.44, max(2.0, r * 0.25)
    lx = (X - cx) * np.cos(ang) + (Y - cy) * np.sin(ang)
    ly = -(X - cx) * np.sin(ang) + (Y - cy) * np.cos(ang)
    # distance to the ellipse, approximated by normalised radius
    k = np.hypot(lx / rx, ly / ry)
    grad = np.hypot(lx / rx ** 2, ly / ry ** 2) / np.maximum(k, 1e-9)
    ring_sdf = np.abs((k - 1) / np.maximum(grad, 1e-9)) - rw / 2
    ring_t = (ly / ry + 1) / 2

    def draw_ring(part):
        m = part if clip is None else part & clip
        fill = mix(rgb('#ffe27a'), rgb('#e8650c'), ring_t)
        shine = (ring_sdf < -rw * 0.18) & (ly < 0.25 * ry) & (ly > -0.4 * ry)
        cv.paint(m & (ring_sdf < 0), fill)
        cv.paint(m & shine & (ring_sdf < 0), mix(rgb('#fff6c8'), rgb('#ffd24a'), ring_t))
        cv.paint(m & (ring_sdf < 0) & (ring_sdf > -min(0.7, rw * 0.22)), rgb('#7a3004'))

    if ring:
        draw_ring(ly < 0)                      # behind the globe

    sd = circle(cx, cy, r)
    inside = sd < 0
    if clip is not None:
        inside &= clip
    nx, ny = (X - cx) / r, (Y - cy) / r
    nz = np.sqrt(np.clip(1 - nx * nx - ny * ny, 0, 1))
    lam = np.clip(nx * LIGHT[0] + ny * LIGHT[1] + nz * LIGHT[2], 0, 1)
    sea = mix(rgb('#0c3f8f'), rgb('#56c4ff'), lam ** 0.9)
    # graticule on a sphere tipped towards the viewer
    tilt = np.radians(18)
    py = ny * np.cos(tilt) - nz * np.sin(tilt)
    pz = ny * np.sin(tilt) + nz * np.cos(tilt)
    lat = np.degrees(np.arcsin(np.clip(-py, -1, 1)))
    lon = np.degrees(np.arctan2(nx, pz)) + 12
    lw = 1.6 * 90 / (r * np.pi) / np.maximum(nz, 0.25)
    on_lat = np.abs(((lat + 15) % 30) - 15) < lw * 0.45
    on_lon = np.abs(((lon + 15) % 30) - 15) < lw * 0.5 / np.maximum(np.cos(np.radians(lat)), 0.2)
    lines = mix(rgb('#9fe2ff'), rgb('#e8f8ff'), lam)
    col = sea.copy()
    gm = (on_lat | on_lon)[..., None]
    col = np.where(gm, col * 0.45 + lines * 0.55, col)
    # specular highlight
    spec = np.clip(1 - np.hypot(nx + 0.38, ny + 0.42) / 0.36, 0, 1) ** 1.6
    col = mix(col, rgb('#ffffff'), spec * 0.9)
    cv.paint(inside, col)
    cv.paint(inside & (sd > -1.0), rgb('#071b45'))

    if ring:
        front = ly >= 0
        draw_ring(front)
    return sd


def art(name):
    """A picture from icons/art (64 x 64 RGBA, drawn to match the new icon
    set's isometric drawers; gfopenup's tools/open_icons_art.py), with the
    see-through cut at the same point as the drawn ones; None if it isn't there."""
    path = os.path.join(ART, name + '.png')
    if not os.path.isfile(path):
        return None
    out = np.array(Image.open(path).convert('RGBA').resize((W, H), Image.LANCZOS), np.uint8)
    solid = out[..., 3] >= 110
    out[..., 3] = np.where(solid, 255, 0)
    out[~solid, :3] = 0
    return out


def program_icon():
    if art('program') is not None:
        return art('program')
    cv = Canvas()
    globe(cv, 23, 23, 14.2)
    return cv.image()


def drawer_icon():
    if art('drawer') is not None:
        return art('drawer')
    cv = Canvas()
    # back of the drawer: its open top seen from above
    top = polygon([(10, 15), (36, 15), (41, 22), (5, 22)])
    cv.shape(top, mix(rgb('#5e4a2c'), rgb('#3a2c18'), (Y - 15) / 7), rgb('#2a1d0c'))
    # the globe sits in the drawer, cut off by the front panel
    globe(cv, 23, 15.5, 9.5, clip=Y < 23)
    # front panel
    front = rrect(4.5, 21.5, 41.5, 41.5, 2.2)
    shade = mix(rgb('#f3e6c0'), rgb('#b8975a'), (Y - 21.5) / 20 + (X - 4.5) / 120)
    cv.shape(front, shade, rgb('#3f2a10'))
    cv.paint((front < -1) & (front > -2) & (Y < 23.5), rgb('#fff8e2'))   # top lip
    # handle
    h = rrect(16.5, 28.5, 29.5, 33.5, 2.4)
    cv.shape(h, mix(rgb('#4a3416'), rgb('#7c5d2c'), (Y - 28.5) / 5), rgb('#2a1a08'), 0.9)
    cv.paint((h < 0) & (h > -0.9) & (Y > 32.4), rgb('#fff3d0'))
    return cv.image()


def readme_icon():
    cv = Canvas()
    fold = 8
    page = polygon([(8.5, 3.5), (36.5 - fold, 3.5), (36.5, 3.5 + fold), (36.5, 42.5), (8.5, 42.5)])
    paper = mix(rgb('#ffffff'), rgb('#cdd6e2'), (Y - 3.5) / 39 + (X - 8.5) / 80)
    cv.shape(page, paper, rgb('#2c3444'))
    corner = polygon([(36.5 - fold, 3.5), (36.5, 3.5 + fold), (36.5 - fold, 3.5 + fold)])
    cv.shape(corner, mix(rgb('#e8eef6'), rgb('#9aa6b8'), (X - 28.5) / 8), rgb('#2c3444'), 0.9)
    for i, y in enumerate((13, 17, 21, 25, 29)):
        x1 = (32 if i else 25) if y > 12 else 28
        if y >= 25:
            x1 = 22
        cv.paint(rrect(12.5, y, x1, y + 1.6, 0.8) < 0, rgb('#7d8aa0'))
    globe(cv, 30, 34, 7.2)
    return cv.image()


# --- selected state: the glow -----------------------------------------------

def glowing(img):
    """GlowIcons' selected look: the picture a little brighter, with a soft
    yellow-to-orange glow around its outline."""
    a = img[..., 3] > 0
    yy, xx = np.mgrid[0:H, 0:W]
    pts = np.argwhere(a)
    # distance from each pixel to the nearest opaque pixel
    d = np.full((H, W), 99.0)
    for py, px in pts:
        np.minimum(d, np.hypot(yy - py, xx - px), out=d)
    out = img.copy()
    c = out[..., :3].astype(np.float64)
    c = c + (255 - c) * 0.18
    out[..., :3] = np.clip(c, 0, 255).astype(np.uint8)
    halo = (~a) & (d <= 2.9)
    inner = rgb('#fff27a') * 255
    outer = rgb('#ff8a1c') * 255
    t = np.clip((d - 1) / 1.9, 0, 1)[..., None]
    g = inner * (1 - t) + outer * t
    out[..., :3] = np.where(halo[..., None], g, out[..., :3]).astype(np.uint8)
    out[..., 3] = np.where(halo, 255, out[..., 3])
    return out


# --- .info writing ----------------------------------------------------------

WBDISK, WBDRAWER, WBTOOL, WBPROJECT = 1, 2, 3, 4
NO_ICON_POSITION = 0x80000000
# Workbench's first four colours on OS 3.x: grey, black, white, blue
WB4 = np.array([[170, 170, 170], [0, 0, 0], [255, 255, 255], [102, 136, 187]], np.float64)


def palette_of(normal, selected):
    """One shared palette, index 0 transparent, at most 256 colours."""
    both = np.concatenate([normal, selected], axis=0)
    opaque = both[..., 3] > 0
    rgbimg = Image.fromarray(both[..., :3].copy())
    q = rgbimg.quantize(colors=255, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE)
    pal = np.array(q.getpalette()[:255 * 3], np.uint8).reshape(-1, 3)
    idx = np.array(q, np.int32) + 1
    idx[~opaque] = 0
    used = np.unique(idx)
    remap = np.zeros(256 + 1, np.int32)
    colours = [np.array([0, 0, 0], np.uint8)]
    for n, u in enumerate(u for u in used if u != 0):
        remap[u] = n + 1
        colours.append(pal[u - 1])
    idx = remap[idx]
    return np.array(colours, np.uint8), idx[:H], idx[H:]


def rle(values):
    """GlowIcons run-length coding with 8-bit values (the depth field says 8)."""
    out = bytearray()
    i, n = 0, len(values)
    while i < n:
        run = 1
        while i + run < n and run < 128 and values[i + run] == values[i]:
            run += 1
        if run >= 2:
            out.append(257 - run)          # -(run-1) as a signed byte
            out.append(values[i])
            i += run
            continue
        j = i
        while j < n and j - i < 128 and not (j + 1 < n and values[j + 1] == values[j]):
            j += 1
        if j == i:
            j = i + 1
        out.append(j - i - 1)
        out.extend(values[i:j])
        i = j
    return bytes(out)


def unrle(data, count):
    out, i = [], 0
    while len(out) < count:
        c = data[i]
        i += 1
        if c < 128:
            out.extend(data[i:i + c + 1])
            i += c + 1
        elif c > 128:
            out.extend([data[i]] * (257 - c))
            i += 1
    return out[:count]


def chunk(cid, body):
    b = cid + struct.pack('>I', len(body)) + body
    return b + (b'\0' if len(body) & 1 else b'')


def imag(idx, pal):
    data = rle([int(v) for v in idx.flatten()])
    assert unrle(data, W * H) == [int(v) for v in idx.flatten()]
    palbytes = pal.tobytes()
    hdr = struct.pack('>BBBBBBHH',
                      0,                     # transparent colour
                      len(pal) - 1,          # colours - 1
                      0x03,                  # has transparent colour, has palette
                      1,                     # image: RLE
                      0,                     # palette: uncompressed
                      8,                     # bits per value
                      len(data) - 1, len(palbytes) - 1)
    return chunk(b'IMAG', hdr + data + palbytes)


def form_icon(normal, selected):
    pal, a, b = palette_of(normal, selected)
    face = struct.pack('>BBBBH', W - 1, H - 1, 0, 0x11, len(pal) * 3 - 1)
    body = b'ICON' + chunk(b'FACE', face) + imag(a, pal) + imag(b, pal)
    return b'FORM' + struct.pack('>I', len(body)) + body


def planar(img):
    """The classic image: 2 bitplanes in Workbench's first four colours."""
    c = img[..., :3].astype(np.float64)
    d = ((c[:, :, None, :] - WB4[None, None]) ** 2).sum(-1)
    idx = d[..., 1:].argmin(-1) + 1        # never the background grey...
    idx = np.where(img[..., 3] > 0, idx, 0)  # ...except where transparent
    words = (W + 15) // 16
    out = bytearray()
    for plane in range(2):
        for y in range(H):
            row = 0
            for x in range(words * 16):
                bit = (idx[y, x] >> plane) & 1 if x < W else 0
                row = (row << 1) | int(bit)
            out += row.to_bytes(words * 2, 'big')
    hdr = struct.pack('>hhhhhIBBI', 0, 0, W, H, 2, 1, 3, 0, 0)
    return hdr + bytes(out)


def bstr(s):
    s = s.encode('latin-1') + b'\0'
    return struct.pack('>I', len(s)) + s


def info(kind, normal, selected, default_tool=None, tooltypes=(), stack=0, at=None):
    is_drawer = kind in (WBDISK, WBDRAWER)
    gadget = struct.pack('>IhhhhHHHIIIIIHI',
                         0, 0, 0, W, H,
                         0x0004,                # GADGIMAGE, highlight by complement
                         0x0003,                # RELVERIFY | GADGIMMEDIATE
                         0x0001,                # BOOLGADGET
                         1, 0, 0, 0, 0, 0,
                         1)                     # UserData: revision 1 (OS 2+)
    head = struct.pack('>HH', 0xE310, 1) + gadget + struct.pack(
        '>BBIIIIIII', kind, 0,
        1 if default_tool else 0,
        1 if tooltypes else 0,
        *(at or (NO_ICON_POSITION, NO_ICON_POSITION)),
        1 if is_drawer else 0,
        0, stack)
    out = head
    if is_drawer:
        newwin = struct.pack('>hhhhBBIIIIIIIhhhhH',
                             120, 80, 366, 152, 255, 255, 0, 0, 0, 0, 0, 0, 0,   # sized to its two icons
                             90, 40, -1, -1, 1)   # WBENCHSCREEN
        out += newwin + struct.pack('>ii', 0, 0)
    out += planar(normal)
    if default_tool:
        out += bstr(default_tool)
    if tooltypes:
        out += struct.pack('>I', (len(tooltypes) + 1) * 4)
        for t in tooltypes:
            out += bstr(t)
    if is_drawer:
        out += struct.pack('>IH', 0, 0)         # DrawerData2: default view
    return out + form_icon(normal, selected)


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    outdir = sys.argv[1] if len(sys.argv) > 1 else os.path.join(here, '..', 'icons')
    os.makedirs(os.path.join(outdir, 'GFOpenBrowser'), exist_ok=True)
    icons = [
        ('GFOpenBrowser.info', WBDRAWER, drawer_icon(), 'drawer', {}),
        ('GFOpenBrowser/GFOpenBrowser.info', WBTOOL, program_icon(), 'program', {'stack': 65536, 'at': (60, 12)}),
        ('GFOpenBrowser/GFOpenBrowser.readme.info', WBPROJECT, readme_icon(), None,
         {'default_tool': 'SYS:Utilities/MultiView', 'at': (220, 12)}),
    ]
    previews = []
    for name, kind, img, pic, extra in icons:
        # the selected image: the art's second frame (NAME-selected.png, the
        # drawer pulled open, the globe turned), glowing, as gfopenup's
        # ICONS.md asks of every GFOpen app; else the picture itself, glowing
        second = art(pic + '-selected') if pic and art(pic) is not None else None
        sel = glowing(second if second is not None else img)
        with open(os.path.join(outdir, name), 'wb') as f:
            f.write(info(kind, img, sel, **extra))
        previews.append((img, sel))
    # preview: each icon normal then selected, on Workbench grey, 4x
    pad, scale = 8, 4
    sheet = Image.new('RGB', ((W * 2 + pad) * len(previews) + pad, H + 2 * pad), (170, 170, 170))
    for i, (n, s) in enumerate(previews):
        x = pad + i * (W * 2 + pad)
        for j, im in enumerate((n, s)):
            sheet.paste(Image.fromarray(im[..., :3]), (x + j * W, pad), Image.fromarray(im[..., 3]))
    sheet = sheet.resize((sheet.width * scale, sheet.height * scale), Image.NEAREST)
    sheet.save(os.path.join(outdir, 'preview.png'))


if __name__ == '__main__':
    main()
