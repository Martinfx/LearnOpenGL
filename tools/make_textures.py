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

def save_gray(name, g):
    Image.fromarray(np.clip(g * 255, 0, 255).astype(np.uint8), "L").save(f"{out}/{name}.png", optimize=True)

def save_normal(name, height, strength):
    # OpenGL convention (+Y up / green = up in the image), tileable central differences.
    dx = (np.roll(height, -1, 1) - np.roll(height, 1, 1)) * 0.5 * strength
    dy = (np.roll(height, -1, 0) - np.roll(height, 1, 0)) * 0.5 * strength
    n = np.stack([-dx, dy, np.ones_like(height)], -1)
    n /= np.linalg.norm(n, axis=-1, keepdims=True)
    save(name, n * 0.5 + 0.5)

# Concrete
n = tileable_noise(6, 4); fine = tileable_noise(3, 64)
c = 0.55 + (n - 0.5) * 0.35 + (fine - 0.5) * 0.12
save("concrete", np.stack([c * 0.98, c * 0.97, c * 0.94], -1))
save_normal("concrete_normal", n * 0.6 + fine * 0.4, 7.0)
save_gray("concrete_roughness", 0.85 + (fine - 0.5) * 0.2)

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
# Height: bricks raised, mortar recessed, slightly bevelled edges.
edge = np.minimum(np.minimum(fx, 1 - fx) / 0.06, np.minimum(fy, 1 - fy) / 0.12)
h = np.clip(edge, 0, 1) * 0.8 + n * 0.2
save_normal("bricks_normal", h, 6.0)
save_gray("bricks_roughness", np.where(mortar, 0.95, 0.75 + (n - 0.5) * 0.2))

# Metal floor plate: diamond tread + seams
n = tileable_noise(5, 8)
u = (x * 16) % 1.0; v = (y * 16) % 1.0
d1 = np.abs((u + v) % 1.0 - 0.5); d2 = np.abs((u - v) % 1.0 - 0.5)
tread = np.where(((np.floor(x * 16) + np.floor(y * 16)) % 2 == 0), d1, d2) < 0.08
seam = ((x * 2) % 1.0 < 0.008) | ((y * 2) % 1.0 < 0.008)
m = 0.45 + (n - 0.5) * 0.2 + tread * 0.18 - seam * 0.25
save("metal_plate", np.stack([m * 0.95, m * 0.98, m * 1.02], -1))
h = tread * 1.0 - seam * 1.0 + n * 0.1
save_normal("metal_plate_normal", h, 3.0)
save_gray("metal_plate_roughness", 0.35 + (n - 0.5) * 0.3 + tread * 0.1 + seam * 0.4)

# Prototype grid: light grey with 1/4 and 1 tile lines (for blockout)
g = np.full((N, N), 0.78)
g[((x * 4) % 1.0 < 0.006) | ((y * 4) % 1.0 < 0.006)] = 0.62
g[(x < 0.012) | (y < 0.012) | (x > 0.988) | (y > 0.988)] = 0.40
save("prototype_grid", np.stack([g, g * 1.01, g * 1.04], -1))
