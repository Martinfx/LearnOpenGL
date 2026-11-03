#!/usr/bin/env python3
"""Textures for the anomaly effects (procedural, CC0, made for this project).

  arc.png               64x256   lightning arc strip: white core + soft glow across x, full length in y
  acid_sheen.png        512x512  bright vein pattern (caustics) of a glossy sludge surface, round fade
  anomaly_marks_2x2.png 1024x1024 ground marks under anomalies, 2x2 tiles (row-major):
                        0 scorch (burnt ground, cracks), 1 acid sludge, 2 dry ground (radiation), 3 pressed soil (crusher)

Run: python3 -I assets/textures/particles/source/anomaly_textures.py   (needs numpy, Pillow)
"""
import os
import sys

import numpy as np
from PIL import Image

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
rng = np.random.default_rng(20261103)


def save(name, rgba):
    img = Image.fromarray(np.clip(rgba * 255.0 + 0.5, 0, 255).astype(np.uint8), "RGBA")
    img.save(os.path.join(OUT, name), optimize=True)
    print("wrote", name, img.size)


def value_noise(n, cells, seed):
    """Tileable smooth value noise, n x n pixels, `cells` lattice cells across."""
    r = np.random.default_rng(seed)
    lattice = r.random((cells, cells))
    x = np.arange(n) / n * cells
    i0 = np.floor(x).astype(int) % cells
    i1 = (i0 + 1) % cells
    f = x - np.floor(x)
    f = f * f * (3 - 2 * f)
    a = lattice[np.ix_(i0, i0)]
    b = lattice[np.ix_(i0, i1)]
    c = lattice[np.ix_(i1, i0)]
    d = lattice[np.ix_(i1, i1)]
    fy = f[:, None]
    fx = f[None, :]
    return (a * (1 - fx) + b * fx) * (1 - fy) + (c * (1 - fx) + d * fx) * fy


def fbm(n, base, octaves, seed, gain=0.5):
    total = np.zeros((n, n))
    amp, norm = 1.0, 0.0
    for o in range(octaves):
        total += value_noise(n, base * 2 ** o, seed + o) * amp
        norm += amp
        amp *= gain
    return total / norm


def voronoi_edges(n, points, seed, warp=0.0):
    """Distance F2 - F1 to random feature points (tileable): small along cell borders.
    `warp` (pixels) bends the cells with noise so the borders wander like real cracks."""
    r = np.random.default_rng(seed)
    p = r.random((points, 2)) * n
    yy, xx = np.mgrid[0:n, 0:n].astype(float)
    if warp > 0:
        xx = (xx + (fbm(n, 6, 4, seed + 7) - 0.5) * 2 * warp) % n
        yy = (yy + (fbm(n, 6, 4, seed + 9) - 0.5) * 2 * warp) % n
    f1 = np.full((n, n), 1e9)
    f2 = np.full((n, n), 1e9)
    for px, py in p:
        dx = np.abs(xx - px)
        dy = np.abs(yy - py)
        dx = np.minimum(dx, n - dx)
        dy = np.minimum(dy, n - dy)
        d = np.sqrt(dx * dx + dy * dy)
        f2 = np.where(d < f1, f1, np.minimum(f2, d))
        f1 = np.minimum(f1, d)
    return f2 - f1


def radial(n):
    yy, xx = np.mgrid[0:n, 0:n].astype(float)
    c = (n - 1) / 2.0
    return np.sqrt((xx - c) ** 2 + (yy - c) ** 2) / (n / 2.0)


def smoothstep(e0, e1, x):
    t = np.clip((x - e0) / (e1 - e0), 0, 1)
    return t * t * (3 - 2 * t)


def make_arc():
    w, h = 64, 256
    x = (np.arange(w) + 0.5) / w * 2 - 1  # -1..1 across
    y = (np.arange(h) + 0.5) / h * 2 - 1
    across = np.exp(-(x / 0.07) ** 2) + 0.45 * np.exp(-(x / 0.32) ** 2)
    along = 1.0 - smoothstep(0.82, 1.0, np.abs(y))
    a = np.clip(along[:, None] * across[None, :], 0, 1)
    rgb = np.ones((h, w, 3))
    save("arc.png", np.dstack([rgb, a]))


def make_sheen():
    n = 512
    edges = voronoi_edges(n, 70, 11, warp=28)
    warp = fbm(n, 4, 4, 21)
    v = np.exp(-((edges + warp * 6.0) / 7.0) ** 2)          # bright veins along cell borders
    v *= 0.55 + 0.45 * fbm(n, 6, 3, 31)
    v += 0.25 * smoothstep(0.62, 0.9, fbm(n, 8, 4, 41))      # oily blotches
    a = np.clip(v, 0, 1) * (1.0 - smoothstep(0.55, 1.0, radial(n)))
    save("acid_sheen.png", np.dstack([np.ones((n, n, 3)), a]))


def blotch(n, seed, edge_noise=0.35):
    """Irregular round patch mask 0..1 with a ragged edge."""
    r = radial(n)
    ragged = r + (fbm(n, 5, 5, seed) - 0.5) * edge_noise * 2
    return 1.0 - smoothstep(0.55, 0.95, ragged)


def make_marks():
    n = 512
    tiles = []
    # 0 scorch: black-brown burnt ground, ash, glowing-free cracks radiating out
    m = blotch(n, 101)
    noise = fbm(n, 8, 5, 102)
    cracks = np.exp(-(voronoi_edges(n, 40, 103, warp=30) / 1.8) ** 2) * smoothstep(0.35, 0.6, fbm(n, 5, 3, 105)) * smoothstep(0.95, 0.3, radial(n))
    base = np.array([0.05, 0.045, 0.04])[None, None, :] * (0.6 + 0.8 * noise[..., None])
    ash = np.array([0.32, 0.31, 0.29])[None, None, :]
    ash_amt = smoothstep(0.6, 0.85, fbm(n, 12, 4, 104))[..., None] * 0.35
    rgb = base * (1 - ash_amt) + ash * ash_amt
    rgb *= (1 - 0.7 * cracks[..., None])
    a = np.clip(m * (0.75 + 0.25 * noise) + cracks * m * 0.3, 0, 1)
    tiles.append(np.dstack([rgb, a]))
    # 1 acid sludge: dark olive mud, crusted yellow-green rim
    m = blotch(n, 201, 0.25)
    noise = fbm(n, 6, 5, 202)
    r = radial(n) + (fbm(n, 5, 5, 201) - 0.5) * 0.5
    rim = np.exp(-((r - 0.78) / 0.09) ** 2)
    mud = np.array([0.10, 0.12, 0.04])[None, None, :] * (0.7 + 0.6 * noise[..., None])
    crust = np.array([0.42, 0.45, 0.12])[None, None, :] * (0.6 + 0.6 * fbm(n, 16, 3, 203)[..., None])
    rgb = mud * (1 - rim[..., None]) + crust * rim[..., None]
    a = np.clip(m * 0.95, 0, 1)
    tiles.append(np.dstack([rgb, a]))
    # 2 dry ground: sun-bleached yellow-brown dead grass and dust, patchy
    m = blotch(n, 301, 0.5) * (0.55 + 0.45 * smoothstep(0.3, 0.7, fbm(n, 6, 4, 302)))
    straw = fbm(n, 64, 2, 303)
    rgb = np.array([0.36, 0.30, 0.17])[None, None, :] * (0.65 + 0.7 * straw[..., None])
    rgb = rgb * (0.85 + 0.3 * fbm(n, 10, 4, 304)[..., None])
    a = np.clip(m * 0.8, 0, 1)
    tiles.append(np.dstack([rgb, a]))
    # 3 pressed soil: darker ring of flattened earth with swirling streaks
    yy, xx = np.mgrid[0:n, 0:n].astype(float)
    c = (n - 1) / 2.0
    ang = np.arctan2(yy - c, xx - c)
    rr = radial(n)
    swirl = 0.5 + 0.5 * np.sin(ang * 9 + rr * 14 + fbm(n, 6, 4, 401) * 9) * fbm(n, 10, 3, 403)
    m = blotch(n, 402, 0.2)
    rgb = np.array([0.09, 0.08, 0.065])[None, None, :] * (0.7 + 0.5 * swirl[..., None])
    a = np.clip(m * (0.55 + 0.35 * swirl), 0, 1)
    tiles.append(np.dstack([rgb, a]))
    sheet = np.zeros((2 * n, 2 * n, 4))
    for i, t in enumerate(tiles):
        y, x = divmod(i, 2)
        sheet[y * n:(y + 1) * n, x * n:(x + 1) * n] = t
    save("anomaly_marks_2x2.png", sheet)


if __name__ == "__main__":
    which = set(sys.argv[1:]) or {"arc", "sheen", "marks"}
    if "arc" in which:
        make_arc()
    if "sheen" in which:
        make_sheen()
    if "marks" in which:
        make_marks()
