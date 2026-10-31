"""Generates the sample tileable textures in assets/textures (original work, CC0)."""
import sys, numpy as np
from PIL import Image

N = 512
rng = np.random.default_rng(7)
out = sys.argv[1]

def tileable_noise(octaves=5, base=4):
    acc = np.zeros((N, N)); amp = 1.0; total = 0
    for o in range(octaves):
        cells = base * 2 ** o
        g = rng.random((cells, cells))
        # bilinear upsample with wrap -> tileable
        x = np.arange(N) * cells / N
        i0 = np.floor(x).astype(int); f = x - i0; i1 = (i0 + 1) % cells
        f = f * f * (3 - 2 * f)
        rows = g[i0][:, i0] * (1 - f)[None, :] + g[i0][:, i1] * f[None, :]
        rows2 = g[i1][:, i0] * (1 - f)[None, :] + g[i1][:, i1] * f[None, :]
        layer = rows * (1 - f)[:, None] + rows2 * f[:, None]
        acc += layer * amp; total += amp; amp *= 0.5
    return acc / total

def save(name, rgb):
    Image.fromarray(np.clip(rgb * 255, 0, 255).astype(np.uint8)).save(f"{out}/{name}.png", optimize=True)

# Concrete
n = tileable_noise(6, 4); fine = tileable_noise(3, 64)
c = 0.55 + (n - 0.5) * 0.35 + (fine - 0.5) * 0.12
save("concrete", np.stack([c * 0.98, c * 0.97, c * 0.94], -1))

# Bricks: 8 rows x 4 bricks, offset every other row, mortar lines
y, x = np.mgrid[0:N, 0:N] / N
rows = 8; cols = 4
r = np.floor(y * rows).astype(int)
xs = x * cols + (r % 2) * 0.5
fx = xs - np.floor(xs); fy = y * rows - r
mortar = (fx < 0.03) | (fx > 0.97) | (fy < 0.06) | (fy > 0.94)
brick_id = (r * 131 + (np.floor(xs).astype(int) % cols) * 17) % 97
tone = 0.85 + (brick_id / 97.0) * 0.3
n = tileable_noise(5, 8)
base = np.stack([0.62 * tone, 0.30 * tone, 0.22 * tone], -1) * (0.8 + 0.4 * n[..., None])
mort = np.stack([0.72, 0.70, 0.66], -1) * (0.85 + 0.3 * n[..., None])
save("bricks", np.where(mortar[..., None], mort, base))

# Metal floor plate: diamond tread + seams
n = tileable_noise(5, 8)
u = (x * 16) % 1.0; v = (y * 16) % 1.0
d1 = np.abs((u + v) % 1.0 - 0.5); d2 = np.abs((u - v) % 1.0 - 0.5)
tread = np.where(((np.floor(x * 16) + np.floor(y * 16)) % 2 == 0), d1, d2) < 0.08
seam = ((x * 2) % 1.0 < 0.008) | ((y * 2) % 1.0 < 0.008)
m = 0.45 + (n - 0.5) * 0.2 + tread * 0.18 - seam * 0.25
save("metal_plate", np.stack([m * 0.95, m * 0.98, m * 1.02], -1))

# Prototype grid: light grey with 1/4 and 1 tile lines (for blockout)
g = np.full((N, N), 0.78)
g[((x * 4) % 1.0 < 0.006) | ((y * 4) % 1.0 < 0.006)] = 0.62
g[(x < 0.012) | (y < 0.012) | (x > 0.988) | (y > 0.988)] = 0.40
save("prototype_grid", np.stack([g, g * 1.01, g * 1.04], -1))
