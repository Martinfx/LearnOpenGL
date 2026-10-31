"""Tiny mesh builder for procedurally generated zone models (CC0, part of the engine repo).

Everything is in metres, +Y up. Each material gets its own primitive in the glTF.
UVs are box-mapped in world metres divided by the material's tile size, so textures
keep the same texel density on every model (see assets/SCALE.md).
"""
import math
import numpy as np


def rot_y(deg):
    a = math.radians(deg)
    c, s = math.cos(a), math.sin(a)
    return np.array([[c, 0, s], [0, 1, 0], [-s, 0, c]], dtype=np.float64)


def rot_x(deg):
    a = math.radians(deg)
    c, s = math.cos(a), math.sin(a)
    return np.array([[1, 0, 0], [0, c, -s], [0, s, c]], dtype=np.float64)


def rot_z(deg):
    a = math.radians(deg)
    c, s = math.cos(a), math.sin(a)
    return np.array([[c, -s, 0], [s, c, 0], [0, 0, 1]], dtype=np.float64)


class Prim:
    def __init__(self):
        self.pos, self.nrm, self.uv, self.idx = [], [], [], []

    def count(self):
        return len(self.pos)


class Mesh:
    """Collects triangles per material. A transform stack places sub-parts."""

    def __init__(self, materials):
        self.materials = materials          # name -> dict(tile=metres, ...)
        self.prims = {}
        self.stack = [(np.eye(3), np.zeros(3))]

    # ---- transform stack -------------------------------------------------
    def push(self, offset=(0, 0, 0), rot=None):
        m, t = self.stack[-1]
        r = np.eye(3) if rot is None else rot
        self.stack.append((m @ r, t + m @ np.asarray(offset, dtype=np.float64)))

    def pop(self):
        self.stack.pop()

    def _xf(self, p):
        m, t = self.stack[-1]
        return m @ np.asarray(p, dtype=np.float64) + t

    def _xn(self, n):
        m, _ = self.stack[-1]
        v = m @ np.asarray(n, dtype=np.float64)
        return v / (np.linalg.norm(v) + 1e-12)

    def prim(self, mat):
        if mat not in self.materials:
            raise KeyError(mat)
        return self.prims.setdefault(mat, Prim())

    # ---- primitives ------------------------------------------------------
    def poly(self, mat, pts, normal=None, uvs=None, smooth_normals=None):
        """Convex polygon (fan triangulated), counter-clockwise seen from the front."""
        P = [self._xf(p) for p in pts]
        if normal is None:
            n = np.cross(P[1] - P[0], P[2] - P[0])
            if np.linalg.norm(n) < 1e-12 and len(P) > 3:
                n = np.cross(P[2] - P[0], P[3] - P[0])
            n = n / (np.linalg.norm(n) + 1e-12)
        else:
            n = self._xn(normal)
        tile = self.materials[mat].get('tile', 2.0)
        pr = self.prim(mat)
        base = pr.count()
        for i, p in enumerate(P):
            pr.pos.append(p)
            pr.nrm.append(self._xn(smooth_normals[i]) if smooth_normals is not None else n)
            if uvs is not None:
                pr.uv.append(np.asarray(uvs[i], dtype=np.float64))
            else:
                pr.uv.append(box_uv(p, n, tile))
        for i in range(1, len(P) - 1):
            pr.idx += [base, base + i, base + i + 1]

    def quad(self, mat, a, b, c, d, **kw):
        self.poly(mat, [a, b, c, d], **kw)

    def box(self, mat, mn, mx, faces='xXyYzZ', mats=None):
        """Axis-aligned box in the current frame. faces: subset of x (-X), X (+X), y, Y, z, Z."""
        x0, y0, z0 = mn
        x1, y1, z1 = mx
        if x1 - x0 <= 1e-6 or y1 - y0 <= 1e-6 or z1 - z0 <= 1e-6:
            return
        f = {
            'x': ([x0, y0, z0], [x0, y0, z1], [x0, y1, z1], [x0, y1, z0]),
            'X': ([x1, y0, z1], [x1, y0, z0], [x1, y1, z0], [x1, y1, z1]),
            'y': ([x0, y0, z0], [x1, y0, z0], [x1, y0, z1], [x0, y0, z1]),
            'Y': ([x0, y1, z1], [x1, y1, z1], [x1, y1, z0], [x0, y1, z0]),
            'z': ([x1, y0, z0], [x0, y0, z0], [x0, y1, z0], [x1, y1, z0]),
            'Z': ([x0, y0, z1], [x1, y0, z1], [x1, y1, z1], [x0, y1, z1]),
        }
        for k in faces:
            m = mats.get(k, mat) if mats else mat
            self.poly(m, f[k])

    def box_c(self, mat, center, size, **kw):
        c = np.asarray(center, dtype=np.float64)
        h = np.asarray(size, dtype=np.float64) * 0.5
        self.box(mat, c - h, c + h, **kw)

    def oriented_box(self, mat, a, b, w, h):
        """Beam of width w (horizontal) and height h (vertical) from point a to point b."""
        a = np.asarray(a, float)
        b = np.asarray(b, float)
        d = b - a
        L = np.linalg.norm(d)
        if L < 1e-6:
            return
        fwd = d / L
        up = np.array([0.0, 1.0, 0.0])
        if abs(fwd @ up) > 0.95:
            up = np.array([1.0, 0.0, 0.0])
        side = np.cross(up, fwd)
        side /= np.linalg.norm(side)
        up = np.cross(fwd, side)
        R = np.stack([side, up, fwd], axis=1)
        self.push(a, R)
        self.box(mat, (-w / 2, -h / 2, 0), (w / 2, h / 2, L))
        self.pop()

    def cylinder(self, mat, a, b, r, seg=10, caps=True, r2=None):
        """Cylinder (or cone frustum with r2) between points a and b, smooth sides."""
        a = np.asarray(a, float)
        b = np.asarray(b, float)
        d = b - a
        L = np.linalg.norm(d)
        if L < 1e-6:
            return
        fwd = d / L
        ref = np.array([0.0, 1.0, 0.0]) if abs(fwd[1]) < 0.9 else np.array([1.0, 0.0, 0.0])
        u = np.cross(ref, fwd)
        u /= np.linalg.norm(u)
        v = np.cross(fwd, u)
        r2 = r if r2 is None else r2
        tile = self.materials[mat].get('tile', 2.0)
        circ = 2 * math.pi * max(r, r2)
        for i in range(seg):
            t0 = 2 * math.pi * i / seg
            t1 = 2 * math.pi * (i + 1) / seg
            d0 = math.cos(t0) * u + math.sin(t0) * v
            d1 = math.cos(t1) * u + math.sin(t1) * v
            p = [a + d0 * r, a + d1 * r, b + d1 * r2, b + d0 * r2]
            uu0, uu1 = circ * i / seg / tile, circ * (i + 1) / seg / tile
            self.poly(mat, p, uvs=[(uu0, 0), (uu1, 0), (uu1, L / tile), (uu0, L / tile)],
                      smooth_normals=[d0, d1, d1, d0])
        if caps:
            ring_a = [a + (math.cos(2 * math.pi * i / seg) * u + math.sin(2 * math.pi * i / seg) * v) * r for i in range(seg)]
            ring_b = [b + (math.cos(2 * math.pi * i / seg) * u + math.sin(2 * math.pi * i / seg) * v) * r2 for i in range(seg)]
            self.poly(mat, ring_a[::-1])
            self.poly(mat, ring_b)

    def lathe(self, mat, profile, seg=32, arc=(0.0, 360.0), cap_inner=False, inside=True, outside=True):
        """Surface of revolution around +Y. profile: list of (radius, y) from bottom to top.
        arc limits the sweep (degrees) to cut openings. inside/outside choose which side faces out."""
        tile = self.materials[mat].get('tile', 2.0)
        a0, a1 = math.radians(arc[0]), math.radians(arc[1])
        n = max(2, int(round(seg * (a1 - a0) / (2 * math.pi))))
        angs = [a0 + (a1 - a0) * i / n for i in range(n + 1)]
        # arc length along profile for v
        vlen = [0.0]
        for i in range(1, len(profile)):
            vlen.append(vlen[-1] + math.hypot(profile[i][0] - profile[i - 1][0], profile[i][1] - profile[i - 1][1]))
        for j in range(len(profile) - 1):
            (r0, y0), (r1, y1) = profile[j], profile[j + 1]
            dr, dy = r1 - r0, y1 - y0
            ln = math.hypot(dr, dy) + 1e-12
            nr, ny = dy / ln, -dr / ln  # outward normal in (r, y)
            for i in range(n):
                ta, tb = angs[i], angs[i + 1]
                ca, sa, cb, sb = math.cos(ta), math.sin(ta), math.cos(tb), math.sin(tb)
                p = [(r0 * ca, y0, r0 * sa), (r0 * cb, y0, r0 * sb), (r1 * cb, y1, r1 * sb), (r1 * ca, y1, r1 * sa)]
                nn = [(nr * ca, ny, nr * sa), (nr * cb, ny, nr * sb), (nr * cb, ny, nr * sb), (nr * ca, ny, nr * sa)]
                ua = ta * max(r0, r1) / tile
                ub = tb * max(r0, r1) / tile
                uv = [(ua, vlen[j] / tile), (ub, vlen[j] / tile), (ub, vlen[j + 1] / tile), (ua, vlen[j + 1] / tile)]
                if outside:
                    # counter-clockwise from outside: reverse order
                    self.poly(mat, p[::-1], uvs=uv[::-1], smooth_normals=nn[::-1])
                if inside:
                    self.poly(mat, p, uvs=uv, smooth_normals=[tuple(-c for c in q) for q in nn])

    # ---- export helpers --------------------------------------------------
    def bounds(self):
        allp = [p for pr in self.prims.values() for p in pr.pos]
        a = np.array(allp)
        return a.min(axis=0), a.max(axis=0)

    def triangles(self):
        return sum(len(pr.idx) // 3 for pr in self.prims.values())


def box_uv(p, n, tile):
    ax = int(np.argmax(np.abs(n)))
    if ax == 0:
        u, v = p[2] * (-1 if n[0] > 0 else 1), p[1]
    elif ax == 1:
        u, v = p[0], p[2] * (-1 if n[1] > 0 else 1)
    else:
        u, v = p[0] * (1 if n[2] > 0 else -1), p[1]
    return np.array([u / tile, v / tile])
