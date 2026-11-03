#!/usr/bin/env python3
"""Volumetric smoke with six-way light maps and a looping fire flipbook (numpy + Pillow only).

    python3 -I assets/textures/particles/source/smoke_fire.py [smoke] [fire] [wisp]

smoke: one-shot puff, 8x8 frames of 256 px, two sheets that together hold a 3D density volume lit
       from six directions (ray marched with self-shadowing, so the puff is dark where it is thick
       and glows at thin edges when lit from behind):
         smoke6_8x8_a.png  R = light from the right (+X), G = from the left (-X), B = from above (+Y),
                           A = opacity
         smoke6_8x8_b.png  R = from below (-Y), G = from the front (towards the viewer), B = from behind
       The renderer mixes the six channels by where the sun, the sky and every nearby light (the fire
       under the smoke) are relative to each particle, which is how current engines light smoke
       ("six-way lighting"). Light maps are stored with a 1/2.2 gamma (read as sRGB textures).
wisp:  smoke6_wisp_8x8_[ab].png, thinner torn wisps for the top of a column (same layout).
fire:  fire_loop_8x8.png, a seamless 64-frame loop (63 -> 0) of a wide fire with several tongues:
       blackbody colours, white-yellow core, red tips that tear off. Alpha = light coverage
       (additive: src * alpha).

Original procedural content, CC0 1.0.
"""
import os
import sys
import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.normpath(os.path.join(HERE, '..'))
ONLY = sys.argv[1:]


# ------------------------------------------------------------------ periodic value noise
class Noise3:
    """Smooth value noise on an integer lattice that repeats every `period` cells on each axis."""

    def __init__(self, seed, period=(64, 64, 64)):
        rng = np.random.default_rng(seed)
        self.period = np.array(period)
        self.grid = rng.random(tuple(period)).astype(np.float32)

    def __call__(self, x, y, z):
        px, py, pz = self.period
        xi, yi, zi = np.floor(x), np.floor(y), np.floor(z)
        fx, fy, fz = x - xi, y - yi, z - zi
        fx = fx * fx * (3 - 2 * fx)
        fy = fy * fy * (3 - 2 * fy)
        fz = fz * fz * (3 - 2 * fz)
        xi = xi.astype(np.int64)
        yi = yi.astype(np.int64)
        zi = zi.astype(np.int64)
        g = self.grid

        def at(dx, dy, dz):
            return g[(xi + dx) % px, (yi + dy) % py, (zi + dz) % pz]

        x00 = at(0, 0, 0) * (1 - fx) + at(1, 0, 0) * fx
        x10 = at(0, 1, 0) * (1 - fx) + at(1, 1, 0) * fx
        x01 = at(0, 0, 1) * (1 - fx) + at(1, 0, 1) * fx
        x11 = at(0, 1, 1) * (1 - fx) + at(1, 1, 1) * fx
        y0 = x00 * (1 - fy) + x10 * fy
        y1 = x01 * (1 - fy) + x11 * fy
        return y0 * (1 - fz) + y1 * fz


def fbm(noise, x, y, z, octaves, gain=0.5, lac=2.0):
    s, a, f, norm = 0.0, 1.0, 1.0, 0.0
    for _ in range(octaves):
        s = s + a * noise(x * f, y * f, z * f)
        norm += a
        a *= gain
        f *= lac
    return s / norm


def billow(noise, x, y, z, octaves):
    """Puffy cauliflower noise: |2n - 1| summed, inverted."""
    s, a, f, norm = 0.0, 1.0, 1.0, 0.0
    for _ in range(octaves):
        n = noise(x * f, y * f, z * f)
        s = s + a * (1.0 - np.abs(2.0 * n - 1.0))
        norm += a
        a *= 0.5
        f *= 2.03
    return s / norm


def to_gamma(c):
    return np.power(np.clip(c, 0.0, 1.0), 1.0 / 2.2)


def save(name, rgba):
    img = Image.fromarray((np.clip(rgba, 0, 1) * 255.0 + 0.5).astype(np.uint8), 'RGBA')
    img.save(os.path.join(OUT, name), optimize=True)
    print('wrote', name, img.size)


def upscale(tile, size):
    return np.asarray(Image.fromarray(tile.astype(np.float32), 'F').resize((size, size), Image.BICUBIC))


# ------------------------------------------------------------------ smoke (six-way)
def smoke_sheet(name, seed, wisp=False, N=128, D=96, tile=256, cols=8):
    """Ray marches a growing, eroding puff and writes the two six-way sheets."""
    noise = Noise3(seed)
    warp = Noise3(seed + 1)
    lin = (np.arange(N, dtype=np.float32) + 0.5) / N * 2.0 - 1.0
    depth = (np.arange(D, dtype=np.float32) + 0.5) / D * 2.0 - 1.0
    # Volume axes: [x (left->right), y (top->bottom of the image), z (front->back)].
    X, Y, Z = np.meshgrid(lin, -lin, depth, indexing='ij')
    X = X.transpose(1, 0, 2)  # rows = image y
    Y = Y.transpose(1, 0, 2)
    Z = Z.transpose(1, 0, 2)
    dz = 2.0 / D
    dxy = 2.0 / N
    frames = cols * cols
    sheet_a = np.zeros((tile * cols, tile * cols, 4), np.float32)
    sheet_b = np.zeros((tile * cols, tile * cols, 4), np.float32)
    rng = np.random.default_rng(seed + 7)
    # A few lumps make up the puff, so its silhouette is not a ball.
    lumps = [(rng.uniform(-0.25, 0.25), rng.uniform(-0.2, 0.25), rng.uniform(-0.2, 0.2), rng.uniform(0.45, 0.7))
             for _ in range(5)]
    for f in range(frames):
        t = f / (frames - 1)
        grow = 0.55 + 0.45 * (1.0 - (1.0 - t) ** 2)          # radius scale over life
        # Domain warp: rolling, curling billows that change through the life.
        wx = X * 2.2 + 3.1
        wy = Y * 2.2 - t * 1.6
        wz = Z * 2.2 + t * 0.9
        q = 0.55 * (fbm(warp, wx, wy, wz, 3) - 0.5)
        r = 0.55 * (fbm(warp, wy + 5.2, wz + 1.3, wx + 7.7, 3) - 0.5)
        sx, sy, sz = X + q, Y + r, Z + 0.5 * (q - r)
        shape = np.zeros_like(X)
        for (cx, cy, cz, rad) in lumps:
            rr = rad * grow
            d = np.sqrt((sx - cx * grow) ** 2 + (sy - cy * grow) ** 2 + (sz - cz * grow) ** 2) / rr
            shape = np.maximum(shape, 1.0 - d)
        # Fade towards the tile border so nothing is cut off.
        border = np.clip((1.0 - np.maximum(np.abs(X), np.abs(Y))) / 0.12, 0.0, 1.0)
        detail_f = 3.0 + 1.5 * t
        s0 = np.clip(shape, 0.0, 1.0)
        inside = np.clip(s0 / 0.25, 0.0, 1.0)
        inside = inside * inside * (3.0 - 2.0 * inside)
        if wisp:
            n = fbm(noise, sx * detail_f + 11.0, sy * detail_f * 0.7 - t * 2.0, sz * detail_f, 5)
            erosion = 0.1 + 0.25 * t
            dens = np.clip(s0 * 0.9 + (n - 0.5) * 1.4 - erosion, 0.0, None) ** 1.5 * inside
        else:
            n = billow(noise, sx * detail_f, sy * detail_f - t * 1.2, sz * detail_f, 5)
            erosion = 0.05 + 0.35 * t ** 1.3                  # older puffs tear apart
            dens = np.clip(s0 * 1.2 + (n - 0.6) * (0.8 + 0.9 * t) - erosion, 0.0, None) * inside
        dens = dens * border
        # Extinction (1/unit, tile = 2 units): dense at birth, thinning out.
        sigma = dens * (38.0 if not wisp else 22.0) * (1.0 - 0.55 * t) * (1.0 - t) ** 0.35
        # Light transmittance from each of the six sides (cumulative optical depth), with a
        # softer second term standing in for multiple scattering.
        def trans(od):
            return 0.75 * np.exp(-od) + 0.25 * np.exp(-od * 0.22)

        def along(axis, reverse):
            s = sigma * (dxy if axis < 2 else dz)
            if reverse:
                s = np.flip(s, axis)
            c = np.cumsum(s, axis) - 0.5 * s
            if reverse:
                c = np.flip(c, axis)
            return trans(c)

        T = {
            'right': along(1, True),    # light enters at +X (last column)
            'left': along(1, False),
            'top': along(0, False),     # row 0 is the top of the image
            'bottom': along(0, True),
            'front': along(2, False),   # z index 0 = nearest to the viewer
            'back': along(2, True),
        }
        # View: from the front, front-to-back compositing.
        sd = sigma * dz
        tv = np.exp(-(np.cumsum(sd, 2) - sd))
        w = (1.0 - np.exp(-sd)) * tv                              # contribution of each slice
        alpha = 1.0 - np.exp(-sd.sum(2))
        light = {}
        for k, tl in T.items():
            light[k] = (w * tl).sum(2) / np.maximum(alpha, 1e-4)  # scattered light per unit opacity
        # Thin edges: let the value drift to the average so the light maps stay smooth.
        mix_ = np.clip(alpha * 6.0, 0.0, 1.0)
        mean = sum(light.values()) / 6.0
        for k in light:
            light[k] = light[k] * mix_ + mean * (1.0 - mix_)
        a = np.clip(alpha * (1.0 if not wisp else 0.9), 0, 1)
        ty, tx = divmod(f, cols)
        ys, xs = ty * tile, tx * tile
        sheet_a[ys:ys + tile, xs:xs + tile, 0] = upscale(to_gamma(light['right']), tile)
        sheet_a[ys:ys + tile, xs:xs + tile, 1] = upscale(to_gamma(light['left']), tile)
        sheet_a[ys:ys + tile, xs:xs + tile, 2] = upscale(to_gamma(light['top']), tile)
        sheet_a[ys:ys + tile, xs:xs + tile, 3] = upscale(a, tile)
        sheet_b[ys:ys + tile, xs:xs + tile, 0] = upscale(to_gamma(light['bottom']), tile)
        sheet_b[ys:ys + tile, xs:xs + tile, 1] = upscale(to_gamma(light['front']), tile)
        sheet_b[ys:ys + tile, xs:xs + tile, 2] = upscale(to_gamma(light['back']), tile)
        sheet_b[ys:ys + tile, xs:xs + tile, 3] = 1.0
        print(name, 'frame', f, 'alpha max %.2f' % a.max(), flush=True)
    save(name + '_a.png', sheet_a)
    save(name + '_b.png', sheet_b)


# ------------------------------------------------------------------ fire (looping)
def blackbody(temp):
    """0..1 temperature -> linear rgb: dark red, orange, yellow, white-yellow core."""
    stops = np.array([0.0, 0.25, 0.5, 0.75, 1.0])
    cols = np.array([[0.4, 0.06, 0.01], [0.95, 0.3, 0.04], [1.0, 0.55, 0.12], [1.0, 0.8, 0.4], [1.0, 0.95, 0.8]])
    out = np.zeros(temp.shape + (3,), np.float32)
    for c in range(3):
        out[..., c] = np.interp(temp, stops, cols[:, c])
    return out


def fire_sheet(name='fire_loop_8x8', seed=31, R=256, cols=8):
    frames = cols * cols
    # Noise repeating every 16 cells in y (scroll) and z (evolution): one period per loop = seamless.
    noise = Noise3(seed, period=(64, 16, 16))
    warp = Noise3(seed + 3, period=(64, 16, 16))
    lin = (np.arange(R, dtype=np.float32) + 0.5) / R
    x = lin[None, :] * 2.0 - 1.0              # -1..1 across
    h = 1.0 - lin[:, None]                    # 0 at the bottom, 1 at the top
    sheet = np.zeros((R * cols, R * cols, 4), np.float32)
    for f in range(frames):
        t = f / frames
        sy = 16.0 * t                          # one noise period of upward scroll per loop
        sz = 16.0 * t
        # Large slow licks + fine fast flicker; the flame leans and wobbles with the warp.
        wob = (fbm(warp, x * 0 + 2.0, h * 2.0 - sy, np.full_like(h, sz) + x * 0, 3) - 0.5) * 0.35 * h
        xx = x + wob
        base_w = 0.62 * (1.0 - h) ** 0.7 + 0.04   # wide at the base, narrow at the top
        body = np.clip(1.0 - np.abs(xx) / np.maximum(base_w, 1e-3), 0.0, 1.0)
        n1 = fbm(noise, xx * 3.0 + 7.0, h * 3.0 - sy * 1.0, np.full_like(h, sz) + xx * 0.0, 4)
        n2 = fbm(noise, xx * 7.0 + 19.0, h * 6.0 - sy * 2.0, np.full_like(h, sz * 2.0) + xx * 0.0, 3)
        n = 0.65 * n1 + 0.35 * n2
        # Temperature: hottest low in the middle, torn into tongues by the noise as it rises.
        temp = body ** 0.9 * ((1.0 - 0.8 * h) + (n - 0.5) * (1.3 + 0.9 * h)) - (1.0 - n2) * 0.3 * h
        temp = np.clip(temp, 0.0, 1.0)
        # Soft rounded bottom edge (the fuel bed), nothing reaches the tile edges.
        temp *= np.clip(h / 0.12, 0.0, 1.0) ** 0.7
        temp *= np.clip((1.0 - h) / 0.08, 0.0, 1.0)
        temp *= np.clip((1.0 - np.abs(x)) / 0.1, 0.0, 1.0)
        a = np.clip((temp - 0.06) * 1.7, 0.0, 1.0) ** 1.3
        rgb = blackbody(np.clip(temp * 1.05, 0, 1))
        ty, tx = divmod(f, cols)
        tile = np.dstack([to_gamma(rgb), a])
        sheet[ty * R:(ty + 1) * R, tx * R:(tx + 1) * R] = tile
        print(name, 'frame', f, flush=True)
    save(name + '.png', sheet)


if __name__ == '__main__':
    if not ONLY or 'smoke' in ONLY:
        smoke_sheet('smoke6_8x8', 101)
    if not ONLY or 'wisp' in ONLY:
        smoke_sheet('smoke6_wisp_8x8', 202, wisp=True)
    if not ONLY or 'fire' in ONLY:
        fire_sheet()
