"""Terrain for levels built from OpenStreetMap data (Skrunda-1): a heightmap with gentle undulation,
roads cut into it with ditches, flat platforms under buildings, a pond in the wetland, and ground
layers (asphalt, paths, wheel ruts, concrete aprons, dirt, dry grass, leaf litter) as organic overlays
traced with marching squares. CC0, part of the engine repo.

The same Terrain object answers height(x, z) and the layer/grove fields, so the level script puts
buildings, vegetation and props exactly on the ground.

World frame: +X east, +Z south (as osm_to_local.py), metres.
"""
import math
import numpy as np
from .mesh import Mesh, Prim
from .mats import PALETTE

ROAD_HALF = {'primary': 4.0, 'secondary': 3.5, 'unclassified': 3.2, 'service': 2.3, 'track': 1.5, 'footway': 0.85,
             'path': 0.7}
PAVED = ('primary', 'secondary', 'unclassified', 'service')


def _smooth(t):
    t = np.clip(t, 0.0, 1.0)
    return t * t * (3 - 2 * t)


def smoothstep(e0, e1, x):
    return _smooth((x - e0) / (e1 - e0))


class Noise:
    """Tileable value noise from a 256 x 256 lattice, smooth (quintic) interpolation."""

    def __init__(self, seed):
        self.g = np.random.default_rng(seed).random((256, 256)).astype(np.float64)

    def __call__(self, x, z, scale):
        x = np.asarray(x, dtype=np.float64) / scale
        z = np.asarray(z, dtype=np.float64) / scale
        xi, zi = np.floor(x).astype(np.int64), np.floor(z).astype(np.int64)
        fx, fz = x - xi, z - zi
        ux = fx * fx * fx * (fx * (fx * 6 - 15) + 10)
        uz = fz * fz * fz * (fz * (fz * 6 - 15) + 10)
        g = self.g
        a = g[xi & 255, zi & 255]
        b = g[(xi + 1) & 255, zi & 255]
        c = g[xi & 255, (zi + 1) & 255]
        d = g[(xi + 1) & 255, (zi + 1) & 255]
        return (a * (1 - ux) + b * ux) * (1 - uz) + (c * (1 - ux) + d * ux) * uz

    def fbm(self, x, z, scale, octaves=3):
        s, amp, tot, acc = scale, 1.0, 0.0, 0.0
        for i in range(octaves):
            acc = acc + amp * self(x + 37.1 * i, z - 11.3 * i, s)
            tot += amp
            amp *= 0.5
            s *= 0.5
        return acc / tot


def _seg_dist(px, pz, ax, az, bx, bz):
    dx, dz = bx - ax, bz - az
    L2 = dx * dx + dz * dz + 1e-12
    t = np.clip(((px - ax) * dx + (pz - az) * dz) / L2, 0, 1)
    qx, qz = ax + t * dx - px, az + t * dz - pz
    return np.sqrt(qx * qx + qz * qz)


def _point_in_poly(px, pz, poly):
    inside = np.zeros(px.shape, dtype=bool)
    n = len(poly)
    for i in range(n):
        (x0, z0), (x1, z1) = poly[i], poly[(i + 1) % n]
        cond = ((z0 > pz) != (z1 > pz))
        xint = (x1 - x0) * (pz - z0) / ((z1 - z0) + 1e-12) + x0
        inside ^= cond & (px < xint)
    return inside


def _poly_dist(px, pz, poly):
    d = np.full(px.shape, 1e9)
    n = len(poly)
    for i in range(n):
        (x0, z0), (x1, z1) = poly[i], poly[(i + 1) % n]
        d = np.minimum(d, _seg_dist(px, pz, x0, z0, x1, z1))
    return d


class Terrain:
    def __init__(self, osm, x0=-330.0, z0=-320.0, size_x=576.0, size_z=576.0, res=1.0, seed=7,
                 buildings=(), pond_areas=()):
        """osm: dict from osm_to_local.py. buildings: list of (cx, cz, length, width, yaw_deg) rectangles that
        get a flat platform. pond_areas: list of polygons that become a pond."""
        self.x0, self.z0, self.res = x0, z0, res
        self.nx, self.nz = int(size_x / res) + 1, int(size_z / res) + 1
        xs = x0 + np.arange(self.nx) * res
        zs = z0 + np.arange(self.nz) * res
        self.X, self.Z = np.meshgrid(xs, zs)          # [iz, ix]
        X, Z = self.X, self.Z
        self.noise = Noise(seed)
        nz = self.noise
        # gentle in the town, rolling hills towards the forest around it
        cdist = np.sqrt((X + 80.0) ** 2 + (Z + 80.0) ** 2)
        amp = 1.4 + 3.5 * smoothstep(170.0, 330.0, cdist)
        low = amp * (nz.fbm(X, Z, 160.0, 2) - 0.5) + (0.5 + 0.8 * smoothstep(170.0, 330.0, cdist)) * (nz(X + 500, Z, 45.0) - 0.5)
        H = low + 0.14 * (nz.fbm(X - 300, Z + 100, 9.0, 2) - 0.5)
        # roads: distance per class
        self.road_d = {k: np.full(X.shape, 1e9) for k in ROAD_HALF}
        for r in osm['roads']:
            hw_key = r['tags'].get('highway')
            if hw_key not in ROAD_HALF:
                continue
            pts = r['pts']
            d = self.road_d[hw_key]
            for (ax, az), (bx, bz) in zip(pts[:-1], pts[1:]):
                m = ROAD_HALF[hw_key] + 8
                i0 = max(0, int((min(ax, bx) - m - x0) / res)); i1 = min(self.nx, int((max(ax, bx) + m - x0) / res) + 2)
                j0 = max(0, int((min(az, bz) - m - z0) / res)); j1 = min(self.nz, int((max(az, bz) + m - z0) / res) + 2)
                if i0 >= i1 or j0 >= j1:
                    continue
                sub = _seg_dist(X[j0:j1, i0:i1], Z[j0:j1, i0:i1], ax, az, bx, bz)
                d[j0:j1, i0:i1] = np.minimum(d[j0:j1, i0:i1], sub)
        road_low = low
        for k, hw in ROAD_HALF.items():
            d = self.road_d[k]
            w = smoothstep(hw + 3.5, hw + 0.4, d) * (1.0 if k in PAVED else 0.6)
            H = H * (1 - w) + road_low * w
            if k in ('primary', 'secondary', 'unclassified'):
                H = H - 0.35 * np.exp(-((d - (hw + 2.4)) / 1.3) ** 2)       # roadside ditch
        # ponds
        self.pond = []
        for poly in pond_areas:
            inside = _point_in_poly(X, Z, poly)
            dd = _poly_dist(X, Z, poly)
            depth = np.where(inside, smoothstep(0.0, 9.0, dd), 0.0)
            hmean = float(np.mean(H[inside])) if inside.any() else 0.0
            H = H - 1.8 * depth
            self.pond.append((poly, hmean - 0.65))
        # building platforms
        self.bmask = np.full(X.shape, 1e9)
        self.platforms = []
        for (cx, cz, L, W, yaw) in buildings:
            a = math.radians(yaw)
            c, s = math.cos(a), math.sin(a)
            u = (X - cx) * c + (Z - cz) * s
            v = -(X - cx) * s + (Z - cz) * c
            du = np.maximum(np.abs(u) - L / 2, 0)
            dv = np.maximum(np.abs(v) - W / 2, 0)
            dist = np.sqrt(du * du + dv * dv)
            ii, jj = int((cx - x0) / res), int((cz - z0) / res)
            hb = float(low[min(max(jj, 0), self.nz - 1), min(max(ii, 0), self.nx - 1)])
            w = smoothstep(9.0, 2.5, dist)
            H = H * (1 - w) + hb * w
            self.bmask = np.minimum(self.bmask, dist)
            self.platforms.append(hb)
        self.H = H
        # ground layer fields (> 0 means covered)
        n = nz
        self.layers = {}
        paved = np.minimum(self.road_d['unclassified'] - ROAD_HALF['unclassified'], self.road_d['service'] - ROAD_HALF['service'])
        paved = np.minimum(paved, np.minimum(self.road_d['primary'] - ROAD_HALF['primary'], self.road_d['secondary'] - ROAD_HALF['secondary']))
        edge = 0.7 * (n(X, Z + 77, 2.2) - 0.5) + 0.4 * (n(X - 9, Z, 0.9) - 0.5)
        potholes = smoothstep(0.78, 0.86, n(X + 211, Z - 53, 3.5)) * 1.6
        cracks = smoothstep(0.93, 0.97, n(X * 0.15 + 400, Z * 3.0, 1.0)) + smoothstep(0.94, 0.98, n(X * 3.0, Z * 0.15 - 90, 1.0))
        self.layers['road'] = -paved + edge - potholes - 0.9 * cracks
        self.layers['ruts'] = (ROAD_HALF['track'] - self.road_d['track']) + 0.6 * (n(X + 5, Z, 2.0) - 0.5)
        self.layers['path'] = (ROAD_HALF['footway'] - self.road_d['footway']) + 0.5 * (n(X, Z - 15, 1.6) - 0.5)
        self.layers['apron'] = np.where(self.bmask > 0.05, 1.05 - self.bmask, -1.0) + 0.5 * (n(X + 33, Z, 1.3) - 0.5) - \
            smoothstep(0.7, 0.8, n(X - 70, Z, 2.5))
        verge = np.minimum(paved, self.road_d['track'] - ROAD_HALF['track'])
        dirt = smoothstep(0.62, 0.7, n.fbm(X + 900, Z, 14.0, 2)) + smoothstep(3.0, 1.2, self.bmask) * 0.8 + \
            smoothstep(1.0, 0.2, verge) * 0.5 * n(X, Z + 3, 3.0)
        self.layers['dirt'] = dirt - 0.5 + 0.25 * (n(X + 1, Z + 1, 1.2) - 0.5)
        self.layers['grass'] = smoothstep(0.45, 0.55, n.fbm(X + 60, Z - 900, 18.0, 2)) - 0.5 + 0.3 * (n(X, Z + 9, 2.0) - 0.5)
        self.layers['dry'] = smoothstep(0.6, 0.68, n.fbm(X - 400, Z + 250, 28.0, 2)) - 0.5 + 0.3 * (n(X, Z, 2.0) - 0.5)
        self.grove = n.fbm(X + 1300, Z - 700, 22.0, 2)
        self.layers['leaves'] = smoothstep(0.6, 0.68, self.grove) - 0.5 + 0.3 * (n(X + 7, Z - 7, 1.5) - 0.5)
        # nothing grows/lies on asphalt or under buildings
        for k in ('grass', 'dirt', 'dry', 'leaves'):
            self.layers[k] = np.where(self.layers['road'] > 0, -1.0, self.layers[k])
        for k in self.layers:
            self.layers[k] = np.where(self.bmask <= 0.0, -1.0, self.layers[k])

    # ---------------------------------------------------------------- queries
    def _sample(self, A, x, z):
        fx = (np.asarray(x, dtype=np.float64) - self.x0) / self.res
        fz = (np.asarray(z, dtype=np.float64) - self.z0) / self.res
        i = np.clip(np.floor(fx).astype(int), 0, self.nx - 2)
        j = np.clip(np.floor(fz).astype(int), 0, self.nz - 2)
        tx, tz = np.clip(fx - i, 0, 1), np.clip(fz - j, 0, 1)
        a, b, c, d = A[j, i], A[j, i + 1], A[j + 1, i], A[j + 1, i + 1]
        return (a * (1 - tx) + b * tx) * (1 - tz) + (c * (1 - tx) + d * tx) * tz

    def height(self, x, z):
        return self._sample(self.H, x, z)

    def field(self, name, x, z):
        if name == 'grove':
            return self._sample(self.grove, x, z)
        if name == 'bdist':
            return self._sample(self.bmask, x, z)
        if name.startswith('road_'):
            return self._sample(self.road_d[name[5:]], x, z)
        return self._sample(self.layers[name], x, z)

    def normal(self, x, z):
        e = 0.5
        hx = self.height(x + e, z) - self.height(x - e, z)
        hz = self.height(x, z + e) - self.height(x, z - e)
        n = np.stack([-hx, np.full_like(np.asarray(hx, dtype=float), 2 * e), -hz], axis=-1)
        return n / np.linalg.norm(n, axis=-1, keepdims=True)

    # ---------------------------------------------------------------- meshing
    OVERLAYS = ('leaves', 'path', 'ruts', 'apron', 'road')

    def tile(self, x0, z0, size, step=1.0, layers=True, lod=0, base=True, names=None, lift=None):
        """Mesh of the square tile [x0, x0+size] x [z0, z0+size] in coordinates local to its centre
        (place the model at the centre). UVs are world-based so textures continue across tiles.
        base=False leaves out the ground grid (the engine terrain draws it) and keeps only the draped
        overlays in `names` (default: all) and the ponds."""
        m = Mesh(PALETTE)
        cx, cz = x0 + size / 2, z0 + size / 2
        n = int(round(size / step))
        xs = x0 + np.arange(n + 1) * step
        zs = z0 + np.arange(n + 1) * step
        GX, GZ = np.meshgrid(xs, zs)
        if base:
            GY = self.height(GX, GZ)
            N = self.normal(GX, GZ)
            self._grid_prim(m, 'ground_lush', GX, GY, GZ, N, cx, cz)
        offs = {'grass': 0.008, 'dry': 0.014, 'dirt': 0.02, 'leaves': 0.028, 'path': 0.036, 'ruts': 0.036, 'apron': 0.045, 'road': 0.05}
        mats = {'grass': 'ground_grass', 'dry': 'ground_dry', 'dirt': 'ground_dirt', 'leaves': 'ground_leaves', 'path': 'ground_path',
                'ruts': 'ground_ruts', 'apron': 'concrete_floor', 'road': 'road'}
        if layers:
            if names is None:
                names = list(offs) if lod == 0 else ['grass', 'dirt', 'road', 'path', 'ruts', 'apron']
            fstep = step          # same grid and diagonals as the ground below, so overlays never dip under it
            nf = int(round(size / fstep))
            fx = x0 + np.arange(nf + 1) * fstep
            fz = z0 + np.arange(nf + 1) * fstep
            FX, FZ = np.meshgrid(fx, fz)
            for k in names:
                F = self.field(k, FX, FZ)
                if (F > 0).any():
                    self._contour_prim(m, mats[k], FX, FZ, F, offs[k] * (lift or (1 if lod == 0 else 1.5)), cx, cz)
        # ponds: flat water inside the tile
        for poly, level in self.pond:
            px = [p[0] for p in poly]
            pz = [p[1] for p in poly]
            if max(px) < x0 or min(px) > x0 + size or max(pz) < z0 or min(pz) > z0 + size:
                continue
            ws = 2.0
            wn = int(size / ws)
            wx = x0 + (np.arange(wn) + 0.5) * ws
            wz = z0 + (np.arange(wn) + 0.5) * ws
            WX, WZ = np.meshgrid(wx, wz)
            near = _point_in_poly(WX, WZ, poly) | (_poly_dist(WX, WZ, poly) < 6.0)
            inside = near & (self.height(WX, WZ) < level + 0.15)
            pr = m.prim('water')
            for j, i in zip(*np.nonzero(inside)):
                a, b = WX[j, i] - ws / 2 - cx, WZ[j, i] - ws / 2 - cz
                base = len(pr.pos)
                for (u, v) in ((a, b), (a, b + ws), (a + ws, b + ws), (a + ws, b)):
                    pr.pos.append(np.array([u, level, v]))
                    pr.nrm.append(np.array([0.0, 1.0, 0.0]))
                    pr.uv.append(np.array([(u + cx) / 4, (v + cz) / 4]))
                pr.idx += [base, base + 1, base + 2, base, base + 2, base + 3]
        return m

    def splat(self, n):
        """RGBA8 weights (n x n, row = Z) of the engine terrain layers 0..3: lush grass (base), meadow
        grass, dry grass, dirt. Soft edges around the zero contour of each layer field."""
        px = self.x0 + (np.arange(n) + 0.5) * (self.nx - 1) * self.res / n
        pz = self.z0 + (np.arange(n) + 0.5) * (self.nz - 1) * self.res / n
        PX, PZ = np.meshgrid(px, pz)
        rest = np.ones_like(PX)
        w = []
        for k in ('dirt', 'dry', 'grass'):          # top layer first
            a = np.clip(self._sample(self.layers[k], PX, PZ) * 6.0 + 0.5, 0.0, 1.0)
            w.append(rest * a)
            rest = rest * (1.0 - a)
        W = np.stack([rest, w[2], w[1], w[0]], axis=-1)
        return np.clip(np.round(W * 255), 0, 255).astype(np.uint8)

    def _grid_prim(self, m, mat, GX, GY, GZ, N, cx, cz):
        tile = PALETTE[mat]['tile']
        nz, nx = GX.shape
        pos = np.stack([GX - cx, GY, GZ - cz], axis=-1).reshape(-1, 3)
        uv = np.stack([GX / tile, -GZ / tile], axis=-1).reshape(-1, 2)
        idx = []
        I = np.arange(nz * nx).reshape(nz, nx)
        a, b, c, d = I[:-1, :-1], I[:-1, 1:], I[1:, :-1], I[1:, 1:]
        # alternate diagonals for a less regular look
        tri = np.stack([a, c, b, b, c, d], axis=-1).reshape(-1)
        pr = m.prim(mat)
        base = len(pr.pos)
        pr.pos.extend(pos)
        pr.nrm.extend(N.reshape(-1, 3))
        pr.uv.extend(uv)
        pr.idx.extend((tri + base).tolist())

    def _contour_prim(self, m, mat, FX, FZ, F, off, cx, cz):
        """Marching squares: polygons where F > 0, vertices interpolated on cell edges, draped on the terrain."""
        tile = PALETTE[mat]['tile']
        pr = m.prim(mat)
        inside = F > 0
        nz, nx = F.shape
        c00, c10, c01, c11 = inside[:-1, :-1], inside[:-1, 1:], inside[1:, :-1], inside[1:, 1:]
        full = c00 & c10 & c01 & c11
        anyin = c00 | c10 | c01 | c11
        # full cells: two triangles each (vectorised)
        jj, ii = np.nonzero(full)
        if len(jj):
            xa, xb = FX[jj, ii], FX[jj, ii + 1]
            za, zb = FZ[jj, ii], FZ[jj + 1, ii]
            quad = [(xa, za), (xa, zb), (xb, zb), (xb, za)]
            P = np.stack([np.stack(q, axis=-1) for q in quad], axis=1)      # (n, 4, 2)
            self._emit(pr, P.reshape(-1, 2), off, cx, cz, tile, np.tile([0, 1, 3, 3, 1, 2], (len(jj), 1)) +
                       (np.arange(len(jj)) * 4)[:, None])
        # partial cells
        jj, ii = np.nonzero(anyin & ~full)
        polys = []
        for j, i in zip(jj, ii):
            corners = [(FX[j, i], FZ[j, i], F[j, i]), (FX[j, i], FZ[j + 1, i], F[j + 1, i]),
                       (FX[j, i + 1], FZ[j + 1, i + 1], F[j + 1, i + 1]), (FX[j, i + 1], FZ[j, i + 1], F[j, i + 1])]
            poly = []
            for k in range(4):
                xa_, za_, fa = corners[k]
                xb_, zb_, fb = corners[(k + 1) % 4]
                if fa > 0:
                    poly.append((xa_, za_))
                if (fa > 0) != (fb > 0):
                    t = fa / (fa - fb)
                    poly.append((xa_ + (xb_ - xa_) * t, za_ + (zb_ - za_) * t))
            if len(poly) >= 3:
                polys.append(poly)
        if polys:
            pts, tris, base = [], [], 0
            for poly in polys:
                pts.extend(poly)
                for k in range(1, len(poly) - 1):
                    tris.append((base, base + k, base + k + 1))
                base += len(poly)
            self._emit(pr, np.array(pts), off, cx, cz, tile, np.array(tris))

    def _emit(self, pr, P2, off, cx, cz, tile, tris):
        X, Z = P2[:, 0], P2[:, 1]
        Y = self.height(X, Z) + off
        N = self.normal(X, Z)
        base = len(pr.pos)
        pr.pos.extend(np.stack([X - cx, Y, Z - cz], axis=-1))
        pr.nrm.extend(N)
        pr.uv.extend(np.stack([X / tile, -Z / tile], axis=-1))
        pr.idx.extend((np.asarray(tris).reshape(-1) + base).tolist())
