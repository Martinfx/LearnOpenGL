"""Low-poly geometric vegetation (no alpha test needed): birches, dead trees, bushes and grass tufts.
Placeholders until alpha-tested foliage lands in the renderer; cheap enough to scatter by the hundred."""
import math
import numpy as np
from .mesh import Mesh, rot_y, rot_x, rot_z
from .mats import PALETTE


def _ico():
    t = (1 + 5 ** 0.5) / 2
    v = [(-1, t, 0), (1, t, 0), (-1, -t, 0), (1, -t, 0), (0, -1, t), (0, 1, t), (0, -1, -t), (0, 1, -t),
         (t, 0, -1), (t, 0, 1), (-t, 0, -1), (-t, 0, 1)]
    f = [(0, 11, 5), (0, 5, 1), (0, 1, 7), (0, 7, 10), (0, 10, 11), (1, 5, 9), (5, 11, 4), (11, 10, 2), (10, 7, 6),
         (7, 1, 8), (3, 9, 4), (3, 4, 2), (3, 2, 6), (3, 6, 8), (3, 8, 9), (4, 9, 5), (2, 4, 11), (6, 2, 10), (8, 6, 7), (9, 8, 1)]
    v = [np.array(p, float) / np.linalg.norm(p) for p in v]
    return v, f


def _subdivide(v, f):
    cache = {}
    v = list(v)

    def mid(a, b):
        k = (min(a, b), max(a, b))
        if k not in cache:
            p = v[a] + v[b]
            v.append(p / np.linalg.norm(p))
            cache[k] = len(v) - 1
        return cache[k]
    nf = []
    for a, b, c in f:
        ab, bc, ca = mid(a, b), mid(b, c), mid(c, a)
        nf += [(a, ab, ca), (b, bc, ab), (c, ca, bc), (ab, bc, ca)]
    return v, nf


ICO0 = _ico()
ICO1 = _subdivide(*ICO0)


def blob(m, mat, center, radius, rng, squash=0.75, detail=1, jitter=0.25):
    v, f = ICO1 if detail else ICO0
    c = np.asarray(center, float)
    scale = np.array([radius, radius * squash, radius])
    disp = [1.0 + rng.uniform(-jitter, jitter) for _ in v]
    P = [c + p * scale * d for p, d in zip(v, disp)]
    for a, b, cc in f:
        m.poly(mat, [P[a], P[b], P[cc]], smooth_normals=[v[a], v[b], v[cc]])


def leaf_cluster(m, mat, center, radius, rng, n=70, leaf=0.32, squash=0.7):
    """Cloud of small double-sided leaf cards (opaque, diamond shaped) inside an ellipsoid."""
    c = np.asarray(center, float)
    for _ in range(n):
        d = rng.normal(size=3)
        d /= np.linalg.norm(d)
        p = c + d * radius * rng.uniform(0.35, 1.0) ** 0.5 * np.array([1, squash, 1])
        a, b = rng.uniform(0, 2 * math.pi), rng.uniform(-0.6, 0.6)
        u = np.array([math.cos(a), b, math.sin(a)])
        u /= np.linalg.norm(u)
        v = np.cross(u, rng.normal(size=3))
        v /= np.linalg.norm(v) + 1e-9
        s = leaf * rng.uniform(0.6, 1.2)
        q = [p - u * s, p - v * s * 0.45, p + u * s, p + v * s * 0.45]
        n_out = d * 0.6 + np.array([0, 0.4, 0])   # normals point out of the crown: soft shading
        m.poly(mat, q, uvs=[(0, 0), (0.1, 0), (0.1, 0.1), (0, 0.1)], smooth_normals=[n_out] * 4)
        m.poly(mat, q[::-1], uvs=[(0, 0.1), (0.1, 0.1), (0.1, 0), (0, 0)], smooth_normals=[n_out] * 4)


def _branch(m, mat, a, b, r0, r1, seg):
    m.cylinder(mat, a, b, r0, seg, caps=False, r2=r1)


def birch(seed=0, height=11.0, lod=0, dead=False):
    """Young birch: slim white trunk, a few upward branches, crown of leafy blobs (none when dead)."""
    rng = np.random.default_rng(seed)
    m = Mesh(PALETTE)
    seg = 7 if lod == 0 else 5
    lean = (rng.uniform(-4, 4), rng.uniform(-4, 4))
    m.push((0, 0, 0), rot_x(lean[0]) @ rot_z(lean[1]))
    r = height * 0.016
    pts = [np.array([0.0, 0.0, 0.0])]
    for i in range(1, 5):
        pts.append(pts[-1] + np.array([rng.uniform(-0.25, 0.25), height / 4, rng.uniform(-0.25, 0.25)]))
    for i in range(4):
        _branch(m, 'bark_birch', pts[i], pts[i + 1], r * (1 - i * 0.22), r * (1 - (i + 1) * 0.22) + 0.02, seg)
    tips = []
    nb = 7 if lod == 0 else 4
    for k in range(nb):
        y = height * rng.uniform(0.35, 0.85)
        i = min(3, int(y / (height / 4)))
        t = (y - pts[i][1]) / (height / 4)
        base = pts[i] * (1 - t) + pts[i + 1] * t
        a = rng.uniform(0, 2 * math.pi)
        ln = height * rng.uniform(0.18, 0.3)
        tip = base + np.array([math.cos(a) * ln * 0.7, ln * 0.7, math.sin(a) * ln * 0.7])
        _branch(m, 'bark_birch', base, tip, r * 0.35, 0.015, 5 if lod == 0 else 4)
        tips.append(tip)
    tips.append(pts[-1])
    if not dead:
        for tip in tips:
            for j in range(2 if lod == 0 else 1):
                c = tip + rng.uniform(-0.9, 0.9, 3) * np.array([1, 0.6, 1])
                mat = 'leaves' if rng.random() < 0.7 else 'leaves_dark'
                if lod == 0:
                    leaf_cluster(m, mat, c, rng.uniform(1.1, 1.8), rng, n=85, leaf=0.27)
                else:
                    leaf_cluster(m, mat, c, rng.uniform(1.0, 1.6), rng, n=22, leaf=0.5)
    m.pop()
    return m


def dead_tree(seed=0, height=9.0, lod=0):
    """Dead, leafless tree with forked dark branches."""
    rng = np.random.default_rng(seed)
    m = Mesh(PALETTE)
    seg = 7 if lod == 0 else 5

    def grow(p, d, r, ln, depth):
        q = p + d * ln
        _branch(m, 'bark_dead', p, q, r, r * 0.65, seg if depth < 2 else 4)
        if depth >= (3 if lod == 0 else 2) or r < 0.03:
            return
        for k in range(2 if depth else 3):
            nd = d + rng.uniform(-0.7, 0.7, 3) * np.array([1, 0.3, 1])
            nd[1] = abs(nd[1]) + 0.3
            nd /= np.linalg.norm(nd)
            grow(q, nd, r * 0.6, ln * rng.uniform(0.55, 0.75), depth + 1)
    grow(np.zeros(3), np.array([0.0, 1.0, 0.0]), height * 0.03, height * 0.45, 0)
    return m


def bush(seed=0, size=1.6, lod=0):
    rng = np.random.default_rng(seed)
    m = Mesh(PALETTE)
    for k in range(5 if lod == 0 else 2):
        c = (rng.uniform(-0.5, 0.5) * size, size * rng.uniform(0.3, 0.5), rng.uniform(-0.5, 0.5) * size)
        if lod == 0:
            leaf_cluster(m, 'leaves_dark', c, size * rng.uniform(0.35, 0.55), rng, n=45, leaf=0.22, squash=0.8)
        else:
            blob(m, 'leaves_dark', c, size * rng.uniform(0.3, 0.45), rng, squash=0.8, detail=0, jitter=0.35)
    return m


def grass_patch(seed=0, size=3.0, tufts=7, height=0.6, lod=0, dry=False):
    """Several tufts of geometric blades over size x size m (both blade sides emitted)."""
    rng = np.random.default_rng(seed)
    m = Mesh(PALETTE)
    mat = 'grass_dry' if dry else 'grass'
    for t in range(tufts):
        cx, cz = rng.uniform(-size / 2, size / 2, 2)
        for b in range(14):
            a = rng.uniform(0, 2 * math.pi)
            h = height * rng.uniform(0.5, 1.2)
            w = 0.05
            ox, oz = cx + rng.uniform(-0.15, 0.15), cz + rng.uniform(-0.15, 0.15)
            bend = rng.uniform(0.1, 0.35) * h
            dx, dz = math.cos(a), math.sin(a)
            px, pz = -dz * w, dx * w
            p0 = (ox - px, 0, oz - pz)
            p1 = (ox + px, 0, oz + pz)
            p2 = (ox + dx * bend, h, oz + dz * bend)
            n = (dx * 0.3, 0.9, dz * 0.3)
            m.poly(mat, [p0, p1, p2], uvs=[(0, 0), (0.1, 0), (0.05, -1)], smooth_normals=[n, n, n])
            m.poly(mat, [p1, p0, p2], uvs=[(0.1, 0), (0, 0), (0.05, -1)], smooth_normals=[n, n, n])
    return m
