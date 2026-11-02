"""Joint decals (original, CC0): what ties hard surfaces and objects into the ground in an abandoned place.

Every joint between two surfaces in a real abandoned town carries a transition: soil, sand and leaves blown
against the kerb and lying in a strip on the asphalt, dark splash-back and damp along the foot of every wall,
ballast stones spilled from the track bed into the grass. Without them every road, wall and track reads as
an object dropped onto a carpet (docs/LEVEL_ART_GUIDE.md, "Proč to působí amatérsky").

make_atlas() paints one RGBA atlas (zone/textures/joints_albedo.png, alpha-blended like the graffiti);
quad(name) is a decal model of one design. Flat designs lie in the XZ plane facing +Y with the joint edge on
z = 0 and the strip running along X, reaching towards +Z; wall designs stand in the XY plane facing +Z with the
bottom on y = 0. The level scripts place them (assets/levels/source/add_joints.py)."""
import os

import numpy as np
from PIL import Image

from .mats import PALETTE, TEX_OUT
from .mesh import Mesh

CW, CH = 1024, 512          # cell size (px)
COLS, ROWS = 2, 2
# name: (column, row, length m, width m, flat?)
DESIGNS = {
    'kerb_soil':     (0, 0, 8.0, 1.2, True),      # on the asphalt along a kerb or a road edge
    'wall_grime':    (1, 0, 3.2, 0.9, False),     # at the foot of a wall
    'ballast_spill': (0, 1, 12.5, 1.3, True),     # from the track bed into the grass
    'verge_soil':    (1, 1, 8.0, 1.6, True),      # broken asphalt edge: soil and moss creeping on
}
PALETTE['joints'] = dict(tex='joints', decal=True, tile=1.0, color=(1, 1, 1), rough=1.0, metal=0.0)


def _noise(rng, w, h, sx, sy=None):
    """Smooth value noise 0..1 with feature size sx x sy pixels."""
    sy = sy or sx
    g = rng.random((int(h / sy) + 3, int(w / sx) + 3)).astype(np.float32)
    im = Image.fromarray((g * 255).astype(np.uint8)).resize((int((g.shape[1]) * sx), int((g.shape[0]) * sy)),
                                                            Image.BICUBIC)
    return np.asarray(im, np.float32)[:h, :w] / 255


def _fbm(rng, w, h, sx, sy, octaves=4):
    out = np.zeros((h, w), np.float32)
    amp, tot = 1.0, 0.0
    for _ in range(octaves):
        out += amp * _noise(rng, w, h, max(sx, 1.5), max(sy, 1.5))
        tot += amp
        amp *= 0.5
        sx, sy = sx / 2, sy / 2
    return out / tot


def _blobs(rng, w, h, count, rmin, rmax, density):
    """Mask of small round blobs (stones, leaves), placed with probability density(v) across the strip."""
    m = np.zeros((h, w), np.float32)
    col = np.zeros((h, w), np.float32)
    yy, xx = np.mgrid[0:h, 0:w]
    for _ in range(count):
        v = rng.random()
        if rng.random() > density(v):
            continue
        cx, cy = rng.random() * w, v * h
        r = rng.uniform(rmin, rmax)
        x0, x1 = int(max(cx - r - 1, 0)), int(min(cx + r + 2, w))
        y0, y1 = int(max(cy - r - 1, 0)), int(min(cy + r + 2, h))
        if x1 <= x0 or y1 <= y0:
            continue
        d = np.hypot((xx[y0:y1, x0:x1] - cx) / r, (yy[y0:y1, x0:x1] - cy) / (r * rng.uniform(0.6, 1.0)))
        a = np.clip((1.0 - d) * 3.0, 0, 1)
        m[y0:y1, x0:x1] = np.maximum(m[y0:y1, x0:x1], a)
        col[y0:y1, x0:x1] = np.where(a > 0.01, rng.random(), col[y0:y1, x0:x1])
    return m, col


def _design(name, rng, w, h):
    """(rgb, alpha) of one design at w x h px; v = 0 is the joint edge (kerb, wall foot, track bed)."""
    v = (np.arange(h, dtype=np.float32)[:, None] + 0.5) / h * np.ones((1, w), np.float32)
    if name == 'kerb_soil':
        edge = 0.35 + 0.45 * _fbm(rng, w, h, 160, 600)          # how far the soil reaches, wandering
        a = np.clip((edge - v) / 0.3, 0, 1) ** 1.5 * (0.4 + 0.4 * _fbm(rng, w, h, 40, 40))     # thins out, no hard rim
        leaves, lc = _blobs(rng, w, h, 1400, 4, 11, lambda t: 1.0 - t)
        base = np.array([0.4, 0.38, 0.33])            # dry dust and sand: a little lighter than the asphalt, grey-brown
        dust = _fbm(rng, w, h, 30, 30)
        rgb = base[None, None, :] * (0.88 + 0.24 * dust[..., None])
        leaf = np.stack([0.3 + 0.08 * lc, 0.26 + 0.06 * lc, 0.18 + 0.03 * lc], -1)
        rgb = rgb * (1 - leaves[..., None] * 0.9) + leaf * leaves[..., None] * 0.9
        a = np.maximum(a, leaves * 0.75 * (v < edge))
        moss = np.clip((0.12 - v) / 0.12, 0, 1) * _fbm(rng, w, h, 20, 20)        # moss right in the joint
        rgb = rgb * (1 - moss[..., None] * 0.7) + np.array([0.22, 0.28, 0.1]) * moss[..., None] * 0.7
        return rgb, np.clip(a, 0, 0.8)
    if name == 'wall_grime':
        top = 0.45 + 0.4 * _fbm(rng, w, h, 120, 400)           # splash-back height, uneven
        streak = _fbm(rng, w, h, 6, 200)                         # vertical rain streaks
        a = np.clip((top - v) / top, 0, 1) ** 1.3 * (0.55 + 0.45 * streak) + 0.25 * np.clip((0.12 - v) / 0.12, 0, 1)
        rgb = np.ones((h, w, 3), np.float32) * np.array([0.11, 0.1, 0.07])
        green = _fbm(rng, w, h, 50, 30) * np.clip((0.3 - v) / 0.3, 0, 1)
        rgb = rgb * (1 - green[..., None] * 0.5) + np.array([0.09, 0.12, 0.05]) * green[..., None] * 0.5
        return rgb, np.clip(a * 0.8, 0, 0.8)
    if name == 'ballast_spill':
        stones, sc = _blobs(rng, w, h, 26000, 3.0, 7.5, lambda t: (1.0 - t) ** 1.3)
        rgb = np.stack([0.42 + 0.24 * sc, 0.4 + 0.22 * sc, 0.37 + 0.2 * sc], -1)
        shade = 0.25 * np.clip((0.3 - v) / 0.3, 0, 1)
        a = stones * 0.95 + shade * _fbm(rng, w, h, 30, 30)
        rgb = rgb * (1 - shade[..., None])
        return rgb, np.clip(a, 0, 0.95)
    if name == 'verge_soil':
        edge = 0.4 + 0.5 * _fbm(rng, w, h, 200, 500)
        a = np.clip((edge - v) / 0.25, 0, 1) * (0.5 + 0.5 * _fbm(rng, w, h, 35, 35))
        moss = _fbm(rng, w, h, 25, 25)
        rgb = np.stack([0.27 + 0.1 * moss, 0.29 + 0.1 * moss, 0.17 + 0.04 * moss], -1)
        cracks = (np.abs(_fbm(rng, w, h, 60, 60) - 0.5) < 0.025) * np.clip((edge + 0.2 - v) / 0.2, 0, 1)
        a = np.maximum(a, cracks * 0.8)
        return rgb, np.clip(a, 0, 0.92)
    raise ValueError(name)


def make_atlas(force=False):
    path = f'{TEX_OUT}/joints_albedo.png'
    if os.path.exists(path) and not force:
        return path
    rng = np.random.default_rng(1986)
    RGB = np.zeros((ROWS * CH, COLS * CW, 3), np.float32)
    A = np.zeros((ROWS * CH, COLS * CW), np.float32)
    for name, (col, row, L, W, flat) in DESIGNS.items():
        # paint at ~200 px per metre in both directions, then squeeze into the cell (UVs stretch it back)
        w, h = int(L * 200), int(W * 200)
        rgb, a = _design(name, rng, w, h)
        a[:, :6] = 0
        a[:, -6:] = 0
        a[-4:, :] = 0
        img = Image.fromarray(np.clip(np.dstack([rgb, a]) * 255 + 0.5, 0, 255).astype(np.uint8), 'RGBA')
        img = img.resize((CW - 8, CH - 8), Image.LANCZOS)
        cell = np.asarray(img, np.float32) / 255
        y0, x0 = row * CH + 4, col * CW + 4
        RGB[y0:y0 + CH - 8, x0:x0 + CW - 8] = cell[..., :3]
        A[y0:y0 + CH - 8, x0:x0 + CW - 8] = cell[..., 3]
    Image.fromarray(np.clip(np.dstack([RGB, A]) * 255 + 0.5, 0, 255).astype(np.uint8), 'RGBA').save(path)
    return path


def quad(name):
    """Decal model of one design (see module doc)."""
    col, row, L, W, flat = DESIGNS[name]
    u0, u1 = (col * CW + 4) / (COLS * CW), ((col + 1) * CW - 4) / (COLS * CW)
    va, vb = (row * CH + 4) / (ROWS * CH), ((row + 1) * CH - 4) / (ROWS * CH)    # va: joint edge (v = 0)
    m = Mesh(PALETTE)
    if flat:
        m.poly('joints', [(-L / 2, 0, 0), (-L / 2, 0, W), (L / 2, 0, W), (L / 2, 0, 0)],
               uvs=[(u0, -va), (u0, -vb), (u1, -vb), (u1, -va)])
    else:
        m.poly('joints', [(-L / 2, 0, 0), (L / 2, 0, 0), (L / 2, W, 0), (-L / 2, W, 0)],
               uvs=[(u0, -va), (u1, -va), (u1, -vb), (u0, -vb)])
    return m
