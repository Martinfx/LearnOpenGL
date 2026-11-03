"""Road wear decals for the city generator (original procedural designs, CC0): long and transverse
asphalt cracks, crazed ("alligator") patches, a pothole rim with loose gravel and a tar repair patch.

make_atlas() paints one RGBA atlas (zone/textures/road_cracks_albedo.png, alpha-blended like the grime);
decal(name) is a flat quad in the XZ plane facing +Y, centred on the origin, 1 cm above the ground,
long side along X (the generator yaws it along the street). Collision and shadows off in the kit."""
import math
import os

import numpy as np
from PIL import Image, ImageDraw, ImageFilter

from .mats import PALETTE, TEX_OUT
from .mesh import Mesh

CELL = 512
COLS, ROWS = 4, 2
# name: (column, row, length m (X), width m (Z), kind)
DESIGNS = {
    'crack_long_a':  (0, 0, 6.0, 1.5, 'long'),
    'crack_long_b':  (1, 0, 6.0, 1.5, 'long'),
    'crack_cross':   (2, 0, 4.0, 1.0, 'cross'),
    'crack_branch':  (3, 0, 3.0, 3.0, 'branch'),
    'crack_net_a':   (0, 1, 2.6, 2.6, 'net'),
    'crack_net_b':   (1, 1, 2.6, 2.6, 'net'),
    'pothole_rim':   (2, 1, 2.6, 2.6, 'pothole'),
    'tar_patch':     (3, 1, 3.2, 2.2, 'patch'),
}
PALETTE['road_cracks'] = dict(tex='road_cracks', decal=True, tile=1.0, color=(1, 1, 1), rough=1.0, metal=0.0)


def _walk(rng, x0, y0, x1, y1, step, wobble):
    """A jagged polyline from (x0, y0) to (x1, y1)."""
    n = max(2, int(math.hypot(x1 - x0, y1 - y0) / step))
    pts = []
    for i in range(n + 1):
        f = i / n
        j = 0 if i in (0, n) else rng.normal(0, wobble)
        nx, ny = -(y1 - y0), (x1 - x0)
        ln = math.hypot(nx, ny) or 1
        pts.append((x0 + (x1 - x0) * f + nx / ln * j, y0 + (y1 - y0) * f + ny / ln * j))
    return pts


def _crack(draw, rng, pts, width, branches=0, depth=0):
    """Draw a crack line, tapering, with side branches."""
    for i in range(len(pts) - 1):
        t = i / max(1, len(pts) - 2)
        w = max(1, int(width * (1.0 - 0.6 * abs(t - 0.5) * 2) * rng.uniform(0.7, 1.2)))
        draw.line([pts[i], pts[i + 1]], fill=255, width=w)
    for _ in range(branches):
        k = int(rng.integers(1, len(pts) - 1))
        x, y = pts[k]
        a = rng.uniform(0, 2 * math.pi)
        ln = rng.uniform(20, 70) / (depth + 1)
        sub = _walk(rng, x, y, x + math.cos(a) * ln, y + math.sin(a) * ln, 6, 2.5)
        _crack(draw, rng, sub, max(1, width * 0.55), branches=1 if depth < 1 else 0, depth=depth + 1)


def _design(kind, rng, w, h):
    """Returns (mask of the dark crack 0..1, mask of the stain/edge 0..1), size w x h pixels."""
    crack = Image.new('L', (w, h), 0)
    stain = Image.new('L', (w, h), 0)
    dc, ds = ImageDraw.Draw(crack), ImageDraw.Draw(stain)
    m = 0.06
    if kind == 'long':
        pts = _walk(rng, w * m, h * rng.uniform(0.35, 0.65), w * (1 - m), h * rng.uniform(0.35, 0.65), 10, 4)
        _crack(dc, rng, pts, 6, branches=6)
    elif kind == 'cross':
        pts = _walk(rng, w * m, h * 0.5, w * (1 - m), h * rng.uniform(0.4, 0.6), 8, 3)
        _crack(dc, rng, pts, 5, branches=3)
    elif kind == 'branch':
        cx, cy = w / 2, h / 2
        for k in range(4):
            a = k * math.pi / 2 + rng.uniform(-0.5, 0.5)
            r = rng.uniform(0.3, 0.45) * w
            _crack(dc, rng, _walk(rng, cx, cy, cx + math.cos(a) * r, cy + math.sin(a) * r, 8, 3), 5, branches=2)
    elif kind == 'net':
        # Alligator cracking: a jittered grid of cells, each edge a jagged line; a dark worn stain under it.
        n = 7
        g = [[(w * (m + (1 - 2 * m) * i / n) + rng.normal(0, 8), h * (m + (1 - 2 * m) * j / n) + rng.normal(0, 8))
              for j in range(n + 1)] for i in range(n + 1)]
        cx, cy = w / 2, h / 2
        for i in range(n + 1):
            for j in range(n + 1):
                for di, dj in ((1, 0), (0, 1)):
                    if i + di > n or j + dj > n:
                        continue
                    a, b = g[i][j], g[i + di][j + dj]
                    r = math.hypot((a[0] + b[0]) / 2 - cx, (a[1] + b[1]) / 2 - cy) / (w * 0.5)
                    if r < 0.98 and rng.random() < 1.35 - r:
                        _crack(dc, rng, _walk(rng, *a, *b, 6, 2), 4)
        ds.ellipse([w * 0.12, h * 0.12, w * 0.88, h * 0.88], fill=110)
    elif kind == 'pothole':
        # Broken rim round a hollow: dark wet edge, crumbled asphalt, radial cracks out of it.
        cx, cy = w / 2, h / 2
        ring = []
        for k in range(28):
            a = 2 * math.pi * k / 28
            r = w * 0.27 * rng.uniform(0.85, 1.15)
            ring.append((cx + math.cos(a) * r, cy + math.sin(a) * r))
        ds.polygon(ring, fill=230)
        dc.line(ring + [ring[0]], fill=255, width=7)
        for k in range(7):
            a = rng.uniform(0, 2 * math.pi)
            r0, r1 = w * 0.28, w * rng.uniform(0.38, 0.47)
            _crack(dc, rng, _walk(rng, cx + math.cos(a) * r0, cy + math.sin(a) * r0, cx + math.cos(a) * r1, cy + math.sin(a) * r1, 6, 2), 4, 1)
        for _ in range(140):  # loose gravel round the rim
            a, r = rng.uniform(0, 2 * math.pi), w * rng.uniform(0.2, 0.42)
            x, y, s = cx + math.cos(a) * r, cy + math.sin(a) * r, rng.uniform(1.5, 4)
            dc.ellipse([x - s, y - s, x + s, y + s], fill=int(rng.uniform(120, 220)))
    elif kind == 'patch':
        # A tar repair patch gone brittle, its outline cracked open.
        poly = [(w * m + rng.uniform(0, 20), h * m + rng.uniform(0, 20)), (w * (1 - m) - rng.uniform(0, 20), h * m + rng.uniform(0, 20)),
                (w * (1 - m) - rng.uniform(0, 20), h * (1 - m) - rng.uniform(0, 20)), (w * m + rng.uniform(0, 20), h * (1 - m) - rng.uniform(0, 20))]
        ds.polygon(poly, fill=200)
        for i in range(4):
            if rng.random() < 0.8:
                _crack(dc, rng, _walk(rng, *poly[i], *poly[(i + 1) % 4], 8, 1.5), 4)
        _crack(dc, rng, _walk(rng, w * 0.3, h * 0.5, w * 0.7, h * 0.45, 8, 3), 3, branches=2)
    crack = crack.filter(ImageFilter.GaussianBlur(0.8))
    stain = stain.filter(ImageFilter.GaussianBlur(10))
    return np.asarray(crack, np.float32) / 255, np.asarray(stain, np.float32) / 255


def make_atlas(force=False):
    path = f'{TEX_OUT}/road_cracks_albedo.png'
    if os.path.exists(path) and not force:
        return path
    rgba = np.zeros((CELL * ROWS, CELL * COLS, 4), np.float32)
    for k, (name, (col, row, wm, hm, kind)) in enumerate(DESIGNS.items()):
        rng = np.random.default_rng(1000 + k)
        s = min(CELL / wm, CELL / hm)
        w, h = int(wm * s), int(hm * s)
        crack, stain = _design(kind, rng, w, h)
        x0, y0 = col * CELL + (CELL - w) // 2, row * CELL + (CELL - h) // 2
        # The crack: near-black with a hint of moss deep in it; the stain: dark, damp asphalt / tar.
        moss = np.clip(rng.random((h, w)) * 0.6, 0, 1)
        r = 0.05 + 0.03 * moss
        g = 0.055 + 0.06 * moss
        b = 0.04 + 0.01 * moss
        a = np.maximum(crack * 0.95, stain * 0.55)
        mix = np.where(a > 0, crack * 0.95 / np.maximum(a, 1e-3), 0)
        sr, sg, sb = (0.09, 0.085, 0.08) if kind != 'patch' else (0.06, 0.06, 0.065)
        rgba[y0:y0 + h, x0:x0 + w, 0] = r * mix + sr * (1 - mix)
        rgba[y0:y0 + h, x0:x0 + w, 1] = g * mix + sg * (1 - mix)
        rgba[y0:y0 + h, x0:x0 + w, 2] = b * mix + sb * (1 - mix)
        rgba[y0:y0 + h, x0:x0 + w, 3] = a
    os.makedirs(TEX_OUT, exist_ok=True)
    Image.fromarray(np.clip(rgba * 255 + 0.5, 0, 255).astype(np.uint8), 'RGBA').save(path)
    return path


def decal(name, lod=0):
    """Flat decal model of one design (see module doc)."""
    make_atlas()
    col, row, wm, hm, _ = DESIGNS[name]
    k = min(CELL / wm, CELL / hm)
    w, h = wm * k / CELL, hm * k / CELL
    u0 = (col + (1 - w) / 2) / COLS
    u1 = (col + (1 + w) / 2) / COLS
    v0 = (row + (1 - h) / 2) / ROWS
    v1 = (row + (1 + h) / 2) / ROWS
    m = Mesh(PALETTE)
    y = 0.012
    m.poly('road_cracks', [(-wm / 2, y, hm / 2), (wm / 2, y, hm / 2), (wm / 2, y, -hm / 2), (-wm / 2, y, -hm / 2)],
           uvs=[(u0, -v1), (u1, -v1), (u1, -v0), (u0, -v0)])
    return m
