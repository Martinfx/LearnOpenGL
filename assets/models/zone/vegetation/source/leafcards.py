#!/usr/bin/env python3
"""Leaf cards for the street trees and bushes (original, CC0), drawn leaf by leaf.

    python3 -I leafcards.py            -> cards/leaves_street.png, cards/leaves_street_dark.png

The older broadleaf cards were renders of a tropical scan: hundreds of tiny olive leaves that blur into yellow
speckle a few metres away, so a whole crown read as stains. Trees of a Central European town street (maple, lime,
elm, ash, poplar) have big leaves, a clear dark-green mass and lighter edges where the sun hits single leaves.

Atlas 2 x 2 (cell 512 px), drawn at 4x and filtered down for clean alpha edges:
  0  maple sprigs (five-lobed leaves) radiating from the centre: a rosette for cards that face out of the crown
  1  lime / elm sprigs (heart-shaped, serrated) radiating from the centre
  2  mixed dense cluster (both kinds), the filler of the inner crown
  3  sparse sprays hanging from the top edge, for the ragged outline and bushes
Each leaf has its own shade, a lighter midrib, a darker half (folded along the rib) and is darker the deeper it lies
in the cluster (drawn first). The `_dark` copy is the shaded inner crown: darker and a little bluer.
"""
import math
import os

import numpy as np
from PIL import Image, ImageDraw, ImageFilter

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, 'cards')
CELL = 512
SS = 4                                   # supersampling
C = CELL * SS
GREENS = [(70, 100, 36), (82, 110, 40), (62, 92, 33), (92, 118, 46), (68, 98, 44), (76, 106, 34), (100, 122, 52)]


def leaf_outline(kind, length, rng):
    """Outline of one leaf in its own frame: petiole joint at (0, 0), tip at (0, length) (y = along the leaf)."""
    pts = []
    if kind == 'maple':
        lobes = 5
        for k in range(181):
            th = -math.pi * 0.95 + k * (1.9 * math.pi / 180)          # around, the notch at the petiole
            ph = (th / (2 * math.pi) * lobes + 0.5) % 1.0
            lobe = 1.0 - abs(ph * 2.0 - 1.0)                              # triangle: pointed lobe tips, deep notches
            r = length * (0.55 + 0.45 * lobe ** 1.2) * (1.0 - 0.22 * abs(th) / math.pi)
            r *= 1.0 + 0.04 * (1 if k % 4 < 2 else -1)                 # serration
            pts.append((r * math.sin(th) * 0.55, r * math.cos(th) * 0.55 + length * 0.45))
    else:                                                               # lime / elm: heart base, pointed tip, teeth
        n = 60
        side = []
        for i in range(n + 1):
            s = i / n
            w = length * 0.42 * math.sin(math.pi * min(1.0, s * 1.08)) ** 0.75 * (1.0 - 0.25 * s)
            w *= 1.0 + 0.06 * (1 if i % 3 == 0 else 0)                 # teeth
            side.append((w, s * length))
        pts = [(x, y) for x, y in side] + [(-x, y) for x, y in reversed(side)]
        asym = rng.uniform(0.85, 1.15)                                  # lime leaves are lopsided
        pts = [(x * (asym if x > 0 else 1.0), y) for x, y in pts]
    return pts


def draw_leaf(img, shade_img, x, y, ang, length, kind, depth, rng):
    """Leaf at (x, y) pointing along `ang`; `depth` 0 (deep in the cluster) .. 1 (on top) sets the shade."""
    base = np.array(GREENS[int(rng.integers(len(GREENS)))], np.float32)
    base *= rng.uniform(0.9, 1.1) * (0.62 + 0.38 * depth)
    if rng.random() < 0.04:
        base = base * 0.6 + np.array([150, 130, 50]) * 0.4                # an early yellow leaf
    ca, sa = math.cos(ang), math.sin(ang)

    def tf(px, py):
        return (x + px * ca - py * sa, y + px * sa + py * ca)
    pts = leaf_outline(kind, length, rng)
    poly = [tf(px, py) for px, py in pts]
    d = ImageDraw.Draw(img)
    col = tuple(int(c) for c in np.clip(base, 0, 255))
    d.polygon(poly, fill=col + (255,))
    # the far half of the leaf (folded along the rib) a little darker
    half = [tf(px, py) for px, py in pts if px <= 0] + [tf(0, length), tf(0, 0)]
    if len(half) > 3:
        dark = tuple(int(c * 0.82) for c in col) + (255,)
        d.polygon(half, fill=dark)
    # midrib and a few side veins, lighter
    vein = tuple(int(min(255, c * 1.25 + 12)) for c in col) + (255,)
    wv = max(1, int(length * 0.03))
    d.line([tf(0, -length * 0.25), tf(0, length * 0.92)], fill=vein, width=wv)
    for k in range(1, 4):
        t = k / 4.5
        for sgn in (-1, 1):
            d.line([tf(0, length * t), tf(sgn * length * 0.24, length * (t + 0.16))], fill=vein, width=max(1, wv // 2))
    # sun on the upper leaves: a soft light patch toward the tip
    sd = ImageDraw.Draw(shade_img)
    if depth > 0.6:
        sd.ellipse([tf(-length * 0.15, length * 0.5)[0] - length * 0.18, tf(0, length * 0.55)[1] - length * 0.18,
                    tf(-length * 0.15, length * 0.5)[0] + length * 0.18, tf(0, length * 0.55)[1] + length * 0.18],
                   fill=int(40 * depth))


def twig(img, pts, width):
    ImageDraw.Draw(img).line(pts, fill=(58, 48, 36, 255), width=width, joint='curve')


def sprig(img, shade, x0, y0, ang, length, kind, depth0, rng, leaf_len):
    """A twig from (x0, y0) along `ang` with alternating leaves, smaller toward the tip."""
    n = int(rng.integers(5, 9))
    pts = [(x0, y0)]
    a = ang
    for i in range(n):
        a += rng.uniform(-0.18, 0.18)
        pts.append((pts[-1][0] + math.cos(a) * length / n, pts[-1][1] + math.sin(a) * length / n))
    twig(img, pts, max(2, int(leaf_len * 0.05)))
    for i in range(1, n + 1):
        px, py = pts[i]
        da = math.atan2(pts[i][1] - pts[i - 1][1], pts[i][0] - pts[i - 1][0])
        side = 1 if i % 2 else -1
        la = da + side * rng.uniform(0.5, 1.0) - math.pi / 2            # leaf frame: +y along the leaf
        L = leaf_len * (1.0 - 0.35 * i / n) * rng.uniform(0.85, 1.1)
        k = kind if kind != 'mixed' else ('maple' if rng.random() < 0.5 else 'lime')
        draw_leaf(img, shade, px, py, la, L, k, min(1.0, depth0 + 0.6 * i / n), rng)
    # a leaf at the tip
    k = kind if kind != 'mixed' else 'lime'
    draw_leaf(img, shade, pts[-1][0], pts[-1][1], math.atan2(pts[-1][1] - pts[-2][1], pts[-1][0] - pts[-2][0]) - math.pi / 2,
              leaf_len * 0.7, k, min(1.0, depth0 + 0.6), rng)


def cell(kind, seed):
    rng = np.random.default_rng(seed)
    img = Image.new('RGBA', (C, C), (0, 0, 0, 0))
    shade = Image.new('L', (C, C), 0)
    cx = cy = C / 2
    leaf_len = C * 0.1
    if kind in ('maple', 'lime', 'mixed'):
        layers = 4 if kind != 'mixed' else 5
        for layer in range(layers):                                     # deep sprigs first, top ones last
            m = int(rng.integers(9, 12))
            off = rng.uniform(0, 2 * math.pi)
            for j in range(m):
                a = off + j * 2 * math.pi / m + rng.uniform(-0.25, 0.25)
                r0 = rng.uniform(0.0, 0.08) * C
                L = C * rng.uniform(0.26, 0.38) * (1.0 - 0.1 * layer)
                sprig(img, shade, cx + math.cos(a) * r0, cy + math.sin(a) * r0, a, L, kind, layer / layers * 0.7, rng,
                      leaf_len * (1.1 if kind == 'maple' else 1.0))
    else:                                                               # hanging sprays from the top edge
        for j in range(15):
            x = C * (0.1 + 0.8 * j / 14) + rng.uniform(-0.04, 0.04) * C
            a = math.pi / 2 + rng.uniform(-0.4, 0.4)
            sprig(img, shade, x, C * 0.04, a, C * rng.uniform(0.5, 0.9), 'mixed', 0.2 + 0.25 * (j % 3), rng, leaf_len * 1.3)
    a = np.asarray(img, np.float32)
    s = np.asarray(shade.filter(ImageFilter.GaussianBlur(SS * 3)), np.float32)[..., None]
    a[..., :3] = np.clip(a[..., :3] + s * np.array([0.9, 0.85, 0.35]), 0, 255)
    # bleed colour into transparent texels (no dark fringe when mip-mapped), then filter down
    im = Image.fromarray(a.astype(np.uint8), 'RGBA')
    rgb = im.convert('RGB').filter(ImageFilter.MaxFilter(9))
    alpha = im.getchannel('A')
    filled = Image.composite(im.convert('RGB'), rgb, alpha)
    out = filled.resize((CELL, CELL), Image.LANCZOS).convert('RGBA')
    out.putalpha(alpha.resize((CELL, CELL), Image.LANCZOS))
    return out


def main():
    atlas = Image.new('RGBA', (CELL * 2, CELL * 2), (0, 0, 0, 0))
    for k, (kind, seed) in enumerate((('maple', 3), ('lime', 5), ('mixed', 8), ('spray', 13))):
        atlas.paste(cell(kind, seed), ((k % 2) * CELL, (k // 2) * CELL))
    # transparent texels keep the leaf colour so mip-mapping does not pull a black fringe into the edges
    a = np.asarray(atlas, np.float32)
    mean = a[a[..., 3] > 128][:, :3].mean(0)
    a[a[..., 3] == 0, :3] = mean
    Image.fromarray(a.astype(np.uint8), 'RGBA').save(os.path.join(OUT, 'leaves_street.png'))
    d = a.copy()
    d[..., :3] = d[..., :3] * np.array([0.72, 0.76, 0.8])
    Image.fromarray(np.clip(d, 0, 255).astype(np.uint8), 'RGBA').save(os.path.join(OUT, 'leaves_street_dark.png'))
    m = a[..., 3] > 128
    print('leaves_street.png mean', a[m][:, :3].mean(0).round(1), 'coverage', round(float(m.mean()), 2))


if __name__ == '__main__':
    main()
