"""Soot left by burnt-out fire (incendiary bottles): 2x2 atlas of 256 px cells, RGBA.
Dark charred middle, brown-grey ash flecks, ragged soft edge. Procedural, CC0.
Run: python3 scorch.py  (writes ../scorch.png)"""
import os
import numpy as np
from PIL import Image

CELL = 256
rng = np.random.default_rng(7)


def value_noise(size, cells, seed):
    r = np.random.default_rng(seed)
    grid = r.random((cells + 1, cells + 1))
    xs = np.linspace(0, cells, size, endpoint=False)
    x0 = xs.astype(int)
    t = xs - x0
    t = t * t * (3 - 2 * t)
    a = grid[x0][:, x0]
    b = grid[x0][:, x0 + 1]
    c = grid[x0 + 1][:, x0]
    d = grid[x0 + 1][:, x0 + 1]
    tx = t[None, :]
    ty = t[:, None]
    return (a * (1 - tx) + b * tx) * (1 - ty) + (c * (1 - tx) + d * tx) * ty


def fbm(size, seed):
    out = np.zeros((size, size))
    amp, total = 1.0, 0.0
    for o, cells in enumerate((4, 8, 16, 32, 64)):
        out += amp * value_noise(size, cells, seed * 31 + o)
        total += amp
        amp *= 0.55
    return out / total


def cell(seed):
    y, x = np.mgrid[0:CELL, 0:CELL]
    u = (x + 0.5) / CELL * 2 - 1
    v = (y + 0.5) / CELL * 2 - 1
    r = np.sqrt(u * u + v * v)
    n = fbm(CELL, seed)
    n2 = fbm(CELL, seed + 100)
    edge = 0.8 + 0.3 * (n - 0.5) * 2.2  # ragged radius
    mask = np.clip((edge - r) / 0.35, 0, 1) ** 0.9
    mask *= np.clip(0.75 + 0.6 * n2, 0, 1)
    flecks = (rng.random((CELL, CELL)) > 0.996) * (r < edge)
    char = np.clip(1.0 - r / max(edge.mean(), 0.1), 0, 1)
    base = 0.035 + 0.05 * n2  # charred black-brown
    ash = 0.08 + 0.06 * n
    lum = base * (0.6 + 0.4 * (1 - char)) + flecks * ash
    rgb = np.stack([lum * 1.08, lum * 1.0, lum * 0.92], -1)
    alpha = np.clip(mask * (0.65 + 0.35 * char) + flecks * 0.3, 0, 0.95)
    return np.concatenate([rgb, alpha[..., None]], -1)


atlas = np.zeros((CELL * 2, CELL * 2, 4))
for i in range(4):
    r, c = divmod(i, 2)
    atlas[r * CELL:(r + 1) * CELL, c * CELL:(c + 1) * CELL] = cell(11 + i)
# sRGB encode colour, alpha linear
rgb = np.clip(atlas[..., :3], 0, 1) ** (1 / 2.2)
img = np.concatenate([rgb, atlas[..., 3:]], -1)
out = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "scorch.png")
Image.fromarray((img * 255 + 0.5).astype(np.uint8), "RGBA").save(out)
print("wrote", out)
