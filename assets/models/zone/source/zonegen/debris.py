"""Small ground debris for scattering as terrain foliage (paintable with the Foliage brush): pieces of the pale
sand-lime brick the Skrunda-1 blocks are built of, plaster and concrete chunks with rebar, broken glass, tin cans,
bottles and dead twigs. Each model is a little cluster (0.3 - 0.9 m across), low poly, origin at the bottom
centre, no collision (the player walks over it)."""
import math

import numpy as np

from .mats import PALETTE
from .mesh import Mesh

PALETTE['bottle_glass'] = dict(tex=None, color=(0.05, 0.16, 0.07), rough=0.12, metal=0.0, alpha=0.8)
PALETTE['can_tin'] = dict(PALETTE['rust'], tile=0.3)


def rot(ax=0.0, ay=0.0, az=0.0):
    """Rotation matrix from angles in degrees (X, then Y, then Z)."""
    a, b, c = (math.radians(v) for v in (ax, ay, az))
    rx = np.array([[1, 0, 0], [0, math.cos(a), -math.sin(a)], [0, math.sin(a), math.cos(a)]])
    ry = np.array([[math.cos(b), 0, math.sin(b)], [0, 1, 0], [-math.sin(b), 0, math.cos(b)]])
    rz = np.array([[math.cos(c), -math.sin(c), 0], [math.sin(c), math.cos(c), 0], [0, 0, 1]])
    return rz @ ry @ rx


def _piece(m, mat, x, z, size, tilt, yaw, rng, lift=0.0):
    """A box resting on the ground: tilted by up to `tilt` degrees, raised so its lowest corner touches y = 0."""
    sx, sy, sz = size
    r = rot(rng.uniform(-tilt, tilt), yaw, rng.uniform(-tilt, tilt))
    corners = np.array([[i * sx / 2, j * sy / 2, k * sz / 2] for i in (-1, 1) for j in (-1, 1) for k in (-1, 1)])
    low = (corners @ r.T)[:, 1].min()
    m.push((x, -low + lift, z), r)
    m.box(mat, (-sx / 2, -sy / 2, -sz / 2), (sx / 2, sy / 2, sz / 2))
    m.pop()


def silicate_bricks(seed=1):
    rng = np.random.default_rng(seed)
    m = Mesh(PALETTE)
    for _ in range(rng.integers(3, 6)):
        length = 0.25 if rng.random() < 0.6 else rng.uniform(0.09, 0.15)     # whole bricks and halves
        _piece(m, 'silicate', rng.uniform(-0.3, 0.3), rng.uniform(-0.3, 0.3), (length, 0.065, 0.12), 12,
               rng.uniform(0, 360), rng)
    return m


def plaster_chunks(seed=2):
    rng = np.random.default_rng(seed)
    m = Mesh(PALETTE)
    for _ in range(rng.integers(6, 10)):
        s = rng.uniform(0.05, 0.22)
        _piece(m, rng.choice(['plaster', 'plaster_peel', 'silicate_dirty']), rng.uniform(-0.35, 0.35),
               rng.uniform(-0.35, 0.35), (s, rng.uniform(0.012, 0.03), s * rng.uniform(0.5, 1.0)), 8, rng.uniform(0, 360), rng)
    return m


def concrete_chunks(seed=3):
    rng = np.random.default_rng(seed)
    m = Mesh(PALETTE)
    for _ in range(rng.integers(4, 7)):
        s = rng.uniform(0.08, 0.28)
        _piece(m, 'concrete', rng.uniform(-0.35, 0.35), rng.uniform(-0.35, 0.35),
               (s, s * rng.uniform(0.4, 0.8), s * rng.uniform(0.6, 1.0)), 25, rng.uniform(0, 360), rng)
    a = rng.uniform(0, math.pi)
    m.cylinder('rust', (-0.3 * math.cos(a), 0.02, -0.3 * math.sin(a)), (0.35 * math.cos(a), 0.09, 0.35 * math.sin(a)),
               0.006, seg=5, caps=False)
    return m


def glass_shards(seed=4):
    rng = np.random.default_rng(seed)
    m = Mesh(PALETTE)
    for _ in range(rng.integers(6, 10)):
        cx, cz, s = rng.uniform(-0.3, 0.3), rng.uniform(-0.3, 0.3), rng.uniform(0.04, 0.16)
        a = rng.uniform(0, 2 * math.pi, 3)
        r = s * rng.uniform(0.5, 1.0, 3)
        y = 0.004
        pts = [(cx + r[i] * math.cos(a[0] + i * 2.1 + a[1] * 0.2), y, cz + r[i] * math.sin(a[0] + i * 2.1 + a[1] * 0.2))
               for i in range(3)]
        m.poly('glass', pts[::-1], normal=(0, 1, 0))
    return m


def tin_cans(seed=5):
    rng = np.random.default_rng(seed)
    m = Mesh(PALETTE)
    for _ in range(rng.integers(2, 4)):
        x, z, yaw = rng.uniform(-0.25, 0.25), rng.uniform(-0.25, 0.25), rng.uniform(0, 360)
        squash = rng.uniform(0.6, 1.0)
        m.push((x, 0.037 * squash, z), rot(0, yaw, 0) @ np.diag([1.0, squash, 1.0]))
        m.cylinder('can_tin', (-0.055, 0, 0), (0.055, 0, 0), 0.037, seg=8)
        m.pop()
    return m


def bottles(seed=6):
    rng = np.random.default_rng(seed)
    m = Mesh(PALETTE)
    for _ in range(rng.integers(1, 3)):
        x, z, yaw = rng.uniform(-0.25, 0.25), rng.uniform(-0.25, 0.25), rng.uniform(0, 360)
        m.push((x, 0.035, z), rot(0, yaw, 90))     # lying on its side
        prof = [(0.0, -0.13), (0.035, -0.13), (0.035, 0.05), (0.014, 0.1), (0.012, 0.15), (0.0, 0.15)]
        m.lathe('bottle_glass', prof, seg=8, inside=False)
        m.pop()
    return m


def twigs(seed=7):
    rng = np.random.default_rng(seed)
    m = Mesh(PALETTE)
    for _ in range(rng.integers(6, 11)):
        a = rng.uniform(0, math.pi)
        ln = rng.uniform(0.25, 0.9)
        cx, cz = rng.uniform(-0.3, 0.3), rng.uniform(-0.3, 0.3)
        d = (math.cos(a) * ln / 2, 0, math.sin(a) * ln / 2)
        r = rng.uniform(0.004, 0.012)
        m.cylinder(str(rng.choice(['wood_dark', 'wood'])), (cx - d[0], r + rng.uniform(0, 0.02), cz - d[2]),
                   (cx + d[0], r, cz + d[2]), r, seg=4, caps=False)
    return m


MODELS = {'silicate_bricks': silicate_bricks, 'plaster_chunks': plaster_chunks, 'concrete_chunks': concrete_chunks,
          'glass_shards': glass_shards, 'tin_cans': tin_cans, 'bottles': bottles, 'twigs': twigs}
