"""Concrete frame skeletons (original, CC0): houses stripped to their reinforced concrete frame.

martin's references (2026-11-03, "rozpadlé baráky jak jsem posílal"): gutted mid-rise buildings standing among
the tenements, nothing left but columns, beams and floor slabs, one side slumped into a stepped heap, slabs
hanging by their rebar, a few panels of brick infill with empty windows, rubble and slab pieces at the foot.

skeleton() builds one in local coordinates: the length runs along X centred on 0, the depth along Z (the front,
toward the street, is +Z), y = 0 is the ground. It can be walked through and climbed: a switchback stair in one
bay of the frame runs up to the highest floor that still stands there (LOD 0; LOD 1 keeps ramps for the eye).
"""
import numpy as np

from .mats import PALETTE
from .mesh import Mesh, rot_y, rot_x, rot_z
from .tenement import stair_flight_x

STOREY = 3.2
SLAB_T = 0.24
CONC = {'Y': 'concrete_dark', 'y': 'concrete', 'x': 'concrete', 'X': 'concrete', 'z': 'concrete', 'Z': 'concrete'}


def skeleton(w=24.0, d=14.0, floors=7, seed=0, lod=0, infill=0.3):
    rng = np.random.default_rng(seed)
    m = Mesh(PALETTE)
    hx, hz = w / 2, d / 2
    gx, gz = max(3, int(round(w / 6.0))), max(2, int(round(d / 5.0)))
    xs, zs = np.linspace(-hx, hx, gx + 1), np.linspace(-hz, hz, gz + 1)
    cut = int(floors * rng.uniform(0.4, 0.75))           # above this floor one side has slumped away
    side = rng.choice((-1, 1))
    # alive[f][i][j]: the slab of floor f over cell (i, j) still hangs there (floor 0 is the ground slab)
    alive = np.ones((floors + 1, gx, gz), bool)
    for f in range(1, floors + 1):
        for i in range(gx):
            u = (i + 0.5) / gx if side > 0 else 1.0 - (i + 0.5) / gx
            k = (f - cut + 1) / (floors - cut + 1)
            for j in range(gz):
                gone = f >= cut and u > 1.05 - 0.85 * k - 0.1 * (j % 2)
                if gone or rng.random() < 0.04 + 0.08 * (f > cut) or (f > 1 and not alive[f - 1, i, j] and rng.random() < 0.7):
                    alive[f, i, j] = False
    # the stair bay: at the standing end, against the back; its slabs stay
    si = 0 if side > 0 else gx - 1
    sj = 0
    top_stair = max(1, cut)
    for f in range(1, top_stair + 1):
        alive[f, si, sj] = True
    sx0 = xs[si] + 0.7
    run = int(round(STOREY / 2 / 0.18)) * 0.28
    sx1 = sx0 + run + 1.4
    sz0, sz1 = zs[sj] + 0.5, zs[sj] + 0.5 + 2.5

    # ---------------------------------------------------------------- slabs (rows of alive cells merged) and beams
    for f in range(1, floors + 1):
        y = f * STOREY
        for j in range(gz):
            i = 0
            while i < gx:
                if not alive[f, i, j]:
                    i += 1
                    continue
                e = i
                while e + 1 < gx and alive[f, e + 1, j]:
                    e += 1
                x0, x1, z0, z1 = xs[i], xs[e + 1], zs[j], zs[j + 1]
                if f < top_stair and i <= si <= e and j == sj:     # the stair opening
                    for a, b, c, dd in ((x0, sx0, z0, z1), (sx1, x1, z0, z1), (sx0, sx1, z0, sz0), (sx0, sx1, sz1, z1)):
                        if b - a > 0.05 and dd - c > 0.05:
                            m.box(None, (a, y - SLAB_T, c), (b, y, dd), mats=CONC)
                else:
                    m.box(None, (x0, y - SLAB_T, z0), (x1, y, z1), mats=CONC)
                i = e + 1
        if lod == 0:                                         # down-stand beams on the grid lines under the slab
            for j in range(gz + 1):
                for i in range(gx):
                    if (j > 0 and alive[f, i, j - 1]) or (j < gz and alive[f, i, j]):
                        m.box('concrete', (xs[i], y - SLAB_T - 0.4, zs[j] - 0.15), (xs[i + 1], y - SLAB_T, zs[j] + 0.15), faces='yzZ')
    m.box(None, (-hx - 0.3, -0.5, -hz - 0.3), (hx + 0.3, 0.05, hz + 0.3), mats=CONC)      # ground slab

    # ---------------------------------------------------------------- columns, up to the highest slab they carry
    for i in range(gx + 1):
        for j in range(gz + 1):
            cells = [(a, b) for a in (i - 1, i) for b in (j - 1, j) if 0 <= a < gx and 0 <= b < gz]
            top = max([f for f in range(floors + 1) if any(alive[f, a, b] for a, b in cells)] or [0])
            if top == 0:
                if rng.random() < 0.6:                         # a stump
                    top_y = rng.uniform(0.6, 2.4)
                else:
                    continue
            else:
                top_y = top * STOREY + (rng.uniform(0.3, 1.6) if top < floors and rng.random() < 0.5 else 0.0)
            m.box('concrete', (xs[i] - 0.2, 0.0, zs[j] - 0.2), (xs[i] + 0.2, top_y, zs[j] + 0.2),
                  faces='xXzZY')
            if lod == 0 and (top < floors or top == 0):
                for k in range(4):                             # rebar out of a broken column top
                    ox, oz = (k % 2 - 0.5) * 0.25, (k // 2 - 0.5) * 0.25
                    m.cylinder('rust', (xs[i] + ox, top_y - 0.05, zs[j] + oz),
                               (xs[i] + ox + rng.uniform(-0.25, 0.25), top_y + rng.uniform(0.3, 0.9), zs[j] + oz + rng.uniform(-0.25, 0.25)),
                               0.014, 4, caps=False)

    # ---------------------------------------------------------------- broken edges: rebar, slabs hanging by it
    for f in range(1, floors + 1):
        y = f * STOREY
        for i in range(gx):
            for j in range(gz):
                if not alive[f, i, j]:
                    continue
                for di, dj in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                    ni, nj = i + di, j + dj
                    if 0 <= ni < gx and 0 <= nj < gz and alive[f, ni, nj]:
                        continue
                    if not (0 <= ni < gx and 0 <= nj < gz) and f <= cut:
                        continue                               # an outer edge of the standing part: clean
                    if di:
                        ex = xs[i + 1] if di > 0 else xs[i]
                        a, b = zs[j], zs[j + 1]
                    else:
                        ex = None
                        ez = zs[j + 1] if dj > 0 else zs[j]
                        a, b = xs[i], xs[i + 1]
                    if lod == 0:
                        for t in np.arange(a + 0.25, b - 0.2, rng.uniform(0.3, 0.6)):
                            dn = rng.uniform(0.2, 1.3)
                            if ex is not None:
                                p0 = (ex, y - 0.12, t)
                                p1 = (ex + di * rng.uniform(0.1, 0.6), y - 0.12 - dn, t + rng.uniform(-0.2, 0.2))
                            else:
                                p0 = (t, y - 0.12, ez)
                                p1 = (t + rng.uniform(-0.2, 0.2), y - 0.12 - dn, ez + dj * rng.uniform(0.1, 0.6))
                            m.cylinder('rust', p0, p1, 0.012, 4, caps=False)
                    if (0 <= ni < gx and 0 <= nj < gz) and rng.random() < 0.3 and f > 1:
                        # the neighbour's slab broke off and hangs from this edge, tipped down
                        ang = rng.uniform(35, 70)
                        if ex is not None:
                            m.push((ex, y, (a + b) / 2), rot_z(-di * ang))
                            m.box(None, (0.0 if di > 0 else -(xs[1] - xs[0]) * 0.8, -SLAB_T, -(b - a) / 2 + 0.2),
                                  (0.0 if di < 0 else (xs[1] - xs[0]) * 0.8, 0.0, (b - a) / 2 - 0.2), mats=CONC)
                        else:
                            m.push(((a + b) / 2, y, ez), rot_x(dj * ang))
                            L = (zs[1] - zs[0]) * 0.8
                            m.box(None, (-(b - a) / 2 + 0.2, -SLAB_T, 0.0 if dj > 0 else -L),
                                  ((b - a) / 2 - 0.2, 0.0, 0.0 if dj < 0 else L), mats=CONC)
                        m.pop()

    # ---------------------------------------------------------------- brick infill with empty windows on the faces
    def panel(x0, x1, y0, y1, z, out):
        wv = x1 - x0
        if rng.random() < 0.55:                                # a window in the middle
            a, b = (x0 + x1) / 2 - 0.75, (x0 + x1) / 2 + 0.75
            m.box('st_brick_worn', (x0, y0, z - 0.12), (a, y1, z + 0.12))
            m.box('st_brick_worn', (b, y0, z - 0.12), (x1, y1, z + 0.12))
            m.box('st_brick_worn', (a, y0, z - 0.12), (b, y0 + 0.9, z + 0.12))
            m.box('st_brick_worn', (a, y0 + 2.4, z - 0.12), (b, y1, z + 0.12))
        else:
            m.box('st_brick_worn', (x0, y0, z - 0.12), (x1, y1, z + 0.12))

    for f in range(1, floors):
        y0, y1 = f * STOREY, (f + 1) * STOREY - SLAB_T
        for j, zf in ((0, -hz), (gz - 1, hz)):
            for i in range(gx):
                if alive[f, i, j] and alive[f + 1, i, j] and rng.random() < infill:
                    top = y1 if rng.random() < 0.7 else y0 + rng.uniform(0.8, 2.2)        # broken off
                    panel(xs[i] + 0.2, xs[i + 1] - 0.2, y0, top, zf, 1)
        for i, xf in ((0, -hx), (gx - 1, hx)):
            for j in range(gz):
                if alive[f, i, j] and alive[f + 1, i, j] and rng.random() < infill:
                    m.push((xf, 0, (zs[j] + zs[j + 1]) / 2), rot_y(90))
                    hw = (zs[j + 1] - zs[j]) / 2 - 0.2
                    panel(-hw, hw, y0, y1 if rng.random() < 0.7 else y0 + rng.uniform(0.8, 2.2), 0.0, 1)
                    m.pop()

    # ---------------------------------------------------------------- the stair
    for f in range(top_stair):
        y = f * STOREY + (0.05 if f == 0 else 0.0)
        h = (f + 1) * STOREY - y
        if lod == 0:
            stair_flight_x(m, sx0, sx0 + run, sz0, sz0 + 1.2, y, y + h / 2, 'concrete', 'concrete_dark')
            stair_flight_x(m, sx0 + run, sx0, sz1 - 1.2, sz1, y + h / 2, y + h, 'concrete', 'concrete_dark')
        else:
            m.oriented_box('concrete', (sx0, y + 0.1, sz0 + 0.6), (sx0 + run, y + h / 2, sz0 + 0.6), 1.2, 0.2)
            m.oriented_box('concrete', (sx0 + run, y + h / 2, sz1 - 0.6), (sx0, y + h, sz1 - 0.6), 1.2, 0.2)
        m.box(None, (sx0 + run, y + h / 2 - 0.22, sz0), (sx1, y + h / 2, sz1), mats=CONC)

    # ---------------------------------------------------------------- what came down lies at the foot
    n = int(w * d / (6 if lod == 0 else 20))
    for _ in range(n):
        x, z = rng.uniform(-hx - 3, hx + 3), rng.uniform(-hz - 3, hz + 3)
        i = int(np.clip((x + hx) / w * gx, 0, gx - 1))
        lost = floors - int(alive[:, i, :].any(axis=1).sum())       # storeys missing over this part
        if rng.random() > 0.25 + 0.12 * lost:
            continue
        if sx0 - 0.5 < x < sx1 + 0.5 and sz0 - 0.5 < z < sz1 + 1.5:
            continue                                             # keep the foot of the stair free
        s = rng.uniform(0.25, 0.9) * (1.0 + 0.1 * lost)
        m.push((x, 0.0, z), rot_y(rng.uniform(0, 360)) @ rot_x(rng.uniform(-25, 25)))
        if rng.random() < 0.3:
            m.box(None, (-s * 1.6, -0.1, -s), (s * 1.6, SLAB_T, s), mats=CONC)              # a slab piece
        else:
            m.box('concrete' if rng.random() < 0.6 else 'st_brick_worn', (-s, -0.3, -s * 0.7), (s, s * 0.6, s * 0.7))
        m.pop()
    m.spots = []
    for f in range(1, floors + 1):                               # seeds take on the open slabs
        for i in range(gx):
            for j in range(gz):
                if alive[f, i, j] and (f == floors or not alive[min(f + 1, floors), i, j]) and rng.random() < 0.5:
                    m.spots.append((rng.uniform(xs[i] + 0.5, xs[i + 1] - 0.5), f * STOREY, rng.uniform(zs[j] + 0.5, zs[j + 1] - 0.5), 'ledge'))
    return m
