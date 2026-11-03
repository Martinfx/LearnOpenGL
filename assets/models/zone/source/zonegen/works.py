"""Street works and a building site on Mulberry Street (original, CC0).

What a New York street had when the city emptied: utility cuts in the roadway (a trench with timber shoring, the
water main and the conduits in the bottom, the spoil heaped beside it), steel road plates laid over the holes,
planks thrown across, and on one double lot a building that was never finished: the concrete frame four floors up,
scaffolding with green netting on the street front, a sidewalk shed over the pavement with the site office on top,
a plywood fence with old posters, and a tower crane over it all.

Frames
- trench(L, W, D): runs along X centred on 0, y = 0 is the road surface, the trench floor is at y = -D. One end
  (+X) is a slope of fallen spoil you can walk out on. The level lowers the terrain inside |x| < L/2, |z| < W/2.
- steel_plate, plank_bridge: centred on 0, y = 0 is what they lie on; the plank bridge spans along Z.
- site(): a 15.2 x 18 m lot like a walk-up (zonegen/walkup.py): front at z = +9 facing +Z (the street),
  y = 0 the pavement. site.ladders lists the ladder up the crane; site.stash the spot in the crane cab.
- tower_crane(): mast at 0, jib toward +X, y = 0 its foot.
"""
import math

import numpy as np

from .mats import M, PALETTE
from .mesh import Mesh, rot_x, rot_y, rot_z

PAL = dict(PALETTE)
PAL['netting'] = M(None, 1.0, color=(0.06, 0.15, 0.08), rough=0.95, metal=0.0, alpha=0.45)
PAL['crane_yellow'] = dict(PALETTE['paint_yellow'])
PAL['plywood'] = dict(PALETTE['wood'], color=(1.05, 0.95, 0.8))
PAL['poster_a'] = dict(PALETTE['paint_cream'], color=(0.9, 0.85, 0.75))
PAL['poster_b'] = dict(PALETTE['paint_red'], color=(0.75, 0.55, 0.5))
PAL['poster_c'] = dict(PALETTE['paint_blue'], color=(0.8, 0.85, 0.9))


def _mesh():
    return Mesh(PAL)


# ------------------------------------------------------------------------------------------------- street works
def trench(L=8.0, W=1.6, D=1.1, seed=1, lod=0):
    rng = np.random.default_rng(seed)
    m = _mesh()
    hl, hw = L / 2, W / 2
    ramp = 2.4                                            # the spoil slope at the +X end
    # the soil behind the boards (hides the terrain's slope) and the cut asphalt lip along both sides
    for s in (-1, 1):
        za, zb = sorted((s * hw, s * (hw + 0.35)))
        m.box('mud', (-hl - 0.35, -D, za), (hl, 0.0, zb), faces='xyYzZ')
        za, zb = sorted((s * hw, s * (hw + 0.55)))
        x = -hl - 0.4
        while x < hl:                                    # the lip broken into slabs, a few tilted into the hole
            w = rng.uniform(0.7, 1.6)
            m.push((x + w / 2, 0.0, (za + zb) / 2), rot_x(s * rng.uniform(-2, 6)) if rng.random() < 0.25 else None)
            m.box('road_worn', (-w / 2 + 0.02, -0.12, -(zb - za) / 2), (w / 2 - 0.02, 0.04, (zb - za) / 2))
            m.pop()
            x += w
    m.box('mud', (-hl - 0.35, -D, -hw - 0.35), (-hl, 0.0, hw + 0.35), faces='xyYzZ')
    m.box('road_worn', (-hl - 0.6, -0.12, -hw - 0.55), (-hl, 0.04, hw + 0.55))
    m.box('mud', (-hl, -D - 0.2, -hw), (hl, -D, hw), faces='Y')                          # the floor
    # timber shoring: boards on both walls and the closed end, walers along them, struts across (not over the slope)
    for s in (-1, 1):
        x = -hl
        while x < hl - ramp + 0.3:
            w = rng.uniform(0.22, 0.3)
            if rng.random() < 0.9:
                top = rng.uniform(-0.02, 0.06)
                z0 = s * (hw - 0.05)
                za, zb = sorted((z0, s * hw))
                m.box('wood' if rng.random() < 0.7 else 'wood_dark', (x, -D, za), (x + w - 0.02, top, zb))
            x += w
        for y in (-0.3, -D + 0.3):
            za, zb = sorted((s * (hw - 0.18), s * (hw - 0.05)))
            m.box('wood_dark', (-hl, y - 0.08, za), (hl - ramp + 0.3, y + 0.08, zb))
    for z in np.arange(-hw + 0.05, hw - 0.05, 0.27):
        m.box('wood', (-hl, -D, z), (-hl + 0.05, 0.03, z + 0.25))
    if lod == 0:
        for x in np.arange(-hl + 0.6, hl - ramp - 0.2, 2.2):
            for y in (-0.3, -D + 0.3):
                m.box('wood_dark', (x - 0.07, y - 0.07, -hw + 0.18), (x + 0.07, y + 0.07, hw - 0.18))
    # what was dug for: the cast-iron water main and a duct bank, one duct broken open; a puddle in the bottom
    m.cylinder('rust', (-hl - 0.4, -D + 0.32, -hw * 0.4), (hl + 0.4, -D + 0.32, -hw * 0.4), 0.26, 10 if lod == 0 else 6)
    m.cylinder('rust_dark', (-hl * 0.2, -D + 0.32, -hw * 0.4), (-hl * 0.2 + 0.2, -D + 0.32, -hw * 0.4), 0.31, 10 if lod == 0 else 6)
    m.box('concrete_dark', (-hl - 0.4, -D + 0.05, hw * 0.15), (hl + 0.4, -D + 0.5, hw * 0.85))
    if lod == 0:
        for k in range(3):
            z = hw * (0.3 + 0.2 * k)
            m.cylinder('paint_red', (-hl * 0.5, -D + 0.62, z), (hl * 0.3, -D + 0.62, z), 0.06, 6)
        m.cylinder('paint_red', (hl * 0.3, -D + 0.62, hw * 0.5), (hl * 0.3 + 0.4, -D + 1.0, hw * 0.55), 0.06, 6)
        pts = [(rng.uniform(-hl + 0.5, 0) + math.cos(a) * 1.2, -D + 0.015, math.sin(a) * hw * 0.8) for a in np.linspace(0, -2 * math.pi, 9)[:-1]]
        m.poly('water', pts)
    # the far end: spoil fallen in, a slope to walk out on
    xa = hl - ramp
    m.poly('mud', [(xa, -D + 0.02, hw), (xa, -D + 0.02, -hw), (hl + 0.5, 0.05, -hw - 0.3), (hl + 0.5, 0.05, hw + 0.3)])
    for k in range(6 if lod == 0 else 2):
        x = rng.uniform(xa, hl)
        t = (x - xa) / ramp
        s = rng.uniform(0.12, 0.3)
        m.push((x, -D + t * D, rng.uniform(-hw + 0.2, hw - 0.2)), rot_y(rng.uniform(0, 360)))
        m.box('debris' if rng.random() < 0.5 else 'road_worn', (-s, -0.05, -s * 0.7), (s, s * 0.5, s * 0.7))
        m.pop()
    return m


def spoil_heap(seed=2, lod=0):
    """The dug soil heaped on the road beside a trench, asphalt chunks in it, weeds will grow there."""
    rng = np.random.default_rng(seed)
    m = _mesh()
    n = 12 if lod == 0 else 7
    xs, zs = np.linspace(-1.8, 1.8, n), np.linspace(-1.0, 1.0, n)
    H = np.zeros((n, n))
    for i, x in enumerate(xs):
        for j, z in enumerate(zs):
            r = math.hypot(x / 1.8, z / 1.0)
            H[i, j] = max(0.0, 0.95 * (1 - r * r)) * rng.uniform(0.85, 1.1) if r < 1 else 0.0
    for i in range(n - 1):
        for j in range(n - 1):
            a, b = (xs[i], H[i, j] - 0.03, zs[j]), (xs[i + 1], H[i + 1, j] - 0.03, zs[j])
            c, d = (xs[i + 1], H[i + 1, j + 1] - 0.03, zs[j + 1]), (xs[i], H[i, j + 1] - 0.03, zs[j + 1])
            m.poly('mud', [a, d, c, b])
    for k in range(10 if lod == 0 else 3):
        x, z = rng.uniform(-1.3, 1.3), rng.uniform(-0.7, 0.7)
        i, j = int(np.argmin(abs(xs - x))), int(np.argmin(abs(zs - z)))
        s = rng.uniform(0.12, 0.3)
        m.push((x, H[i, j] - 0.08, z), rot_y(rng.uniform(0, 360)) @ rot_x(rng.uniform(-30, 30)))
        m.box('road_worn', (-s, -0.04, -s * 0.8), (s, 0.06, s * 0.8))
        m.pop()
    return m


def steel_plate(seed=3, w=1.6, d=2.4):
    """A steel road plate, 25 mm, the cold-patch asphalt ramped against its edges, worn bright where tyres ran."""
    rng = np.random.default_rng(seed)
    m = _mesh()
    hx, hz = w / 2, d / 2
    m.box('metal_plate', (-hx, 0.0, -hz), (hx, 0.03, hz))
    for s in (-1, 1):
        m.poly('road_worn', [(s * hx, 0.03, -hz), (s * hx, 0.03, hz), (s * (hx + 0.2), 0.0, hz), (s * (hx + 0.2), 0.0, -hz)][::(1 if s > 0 else -1)])
        m.poly('road_worn', [(-hx, 0.03, s * hz), (hx, 0.03, s * hz), (hx, 0.0, s * (hz + 0.2)), (-hx, 0.0, s * (hz + 0.2))][::(-1 if s > 0 else 1)])
    for k in range(2):                                    # the lifting holes, a bolt in one
        m.box('rust_dark', (-0.05 + (k - 0.5) * w * 0.6, 0.03, hz - 0.15), (0.05 + (k - 0.5) * w * 0.6, 0.035, hz - 0.05))
    if rng.random() < 0.5:
        m.cylinder('rust', (0.3, 0.03, -hz + 0.2), (0.3, 0.08, -hz + 0.2), 0.03, 6)
    return m


def plank_bridge(span=2.8, n=4, seed=4):
    """Scaffold boards thrown across a trench, spanning Z, two cleats nailed under them."""
    rng = np.random.default_rng(seed)
    m = _mesh()
    x = -n * 0.26 / 2
    for k in range(n):
        m.push((x + 0.13, 0.0, rng.uniform(-0.1, 0.1)), rot_y(rng.uniform(-4, 4)) @ rot_z(rng.uniform(-1.5, 1.5)))
        m.box('wood' if k % 2 else 'wood_dark', (-0.12, 0.0, -span / 2), (0.12, 0.05, span / 2))
        m.pop()
        x += 0.26
    for z in (-span / 2 + 0.25, span / 2 - 0.25):
        m.box('wood_dark', (-n * 0.13, -0.05, z - 0.05), (n * 0.13, 0.0, z + 0.05))
    return m


# ------------------------------------------------------------------------------------------------- the site
FLOOR_H = 3.4
SLAB = 0.25


def _rebar(m, x, y, z, n, rng, h=1.0):
    for k in range(n):
        a = 2 * math.pi * k / n
        bx, bz = x + math.cos(a) * 0.12, z + math.sin(a) * 0.12
        m.cylinder('rust', (bx, y, bz), (bx + rng.uniform(-0.15, 0.15), y + h * rng.uniform(0.7, 1.2), bz + rng.uniform(-0.15, 0.15)), 0.014, 4, caps=False)


def _stair(m, x0, z0, y0, rise, run_dir, width=1.2, mat='concrete'):
    """A straight concrete flight from (x0, y0, z0) going along -z (run_dir -1) or +z, rising `rise`."""
    n = int(round(rise / 0.19))
    r, t = rise / n, 0.28
    for k in range(n):
        za, zb = sorted((z0 + run_dir * k * t, z0 + run_dir * (k + 1) * t))
        m.box(mat, (x0, y0 + k * r, za), (x0 + width, y0 + (k + 1) * r, zb), faces='xXYzZ')
    # the soffit: a sloped slab under the steps
    za, zb = z0, z0 + run_dir * n * t
    m.poly('concrete_dark', [(x0, y0, za), (x0 + width, y0, za), (x0 + width, y0 + rise - 0.2, zb), (x0, y0 + rise - 0.2, zb)][::(1 if run_dir < 0 else -1)])
    return n * t


def site(seed=11, lod=0, floors=4):
    """The unfinished building on a 50-foot lot: concrete frame, stairs inside, scaffold and netting on the front,
    the sidewalk shed over the pavement with the site office on it, the plywood fence and the gate."""
    rng = np.random.default_rng(seed)
    m = _mesh()
    m.ladders, m.stash = [], None
    hx, hz = 7.6, 9.0
    xs = (-hx + 0.3, -hx / 3, hx / 3, hx - 0.3)
    zs = (hz - 0.6, 3.0, -3.0, -hz + 0.3)
    m.box('concrete', (-hx + 0.05, -0.2, -hz + 0.05), (hx - 0.05, 0.12, hz - 0.3))           # the ground slab
    # floors 1..floors-1 cast, the top one only the columns with rebar out of them and the formwork for the slab
    # the stairs: one straight flight per floor rising toward the back (-z) from the middle of the lot, the even ones
    # in strip A, the odd in strip B beside it; the slab over a flight has its hole; you walk off the top, round the
    # west side of the next flight and up it
    S0 = 0.5
    STRIPS = ((2.8, 4.2), (4.4, 5.8))
    for f in range(floors):
        y = f * FLOOR_H + 0.12
        top = y + FLOOR_H - SLAB
        last = f == floors - 1
        for x in xs:
            for z in zs:
                if last and rng.random() < 0.4:
                    h = rng.uniform(0.6, 1.8)                       # columns poured part way on the top floor
                    m.box('concrete', (x - 0.22, y, z - 0.22), (x + 0.22, y + h, z + 0.22))
                    _rebar(m, x, y + h, z, 6 if lod == 0 else 0, rng, 1.2)
                    continue
                m.box('concrete', (x - 0.22, y, z - 0.22), (x + 0.22, top, z + 0.22))
                if last and lod == 0:
                    _rebar(m, x, top + (SLAB if not last else 0), z, 4, rng, 0.9)
        if last:
            # plywood formwork on shores over the back half, waiting for the pour that never came
            m.box('plywood', (-hx + 0.3, top, -hz + 0.3), (0.0, top + 0.03, hz * 0.2))
            if lod == 0:
                for x in np.arange(-hx + 0.8, -0.2, 1.4):
                    for z in np.arange(-hz + 0.8, hz * 0.2, 1.6):
                        m.cylinder('metal_plate', (x, y, z), (x, top, z), 0.03, 4, caps=False)
                for x in np.arange(-hx + 0.5, -0.1, 0.4):
                    m.cylinder('rust', (x, top + 0.08, -hz + 0.3), (x, top + 0.08, hz * 0.2), 0.008, 3, caps=False)
            break
        # the slab over this floor, with the hole over this floor's flight; its edge beam
        sy = top
        x0, x1 = STRIPS[f % 2]
        z0, z1 = S0 - 4.6, S0 - 1.0
        for (a, b, c, d) in ((-hx + 0.05, x0, -hz + 0.05, hz - 0.3), (x1, hx - 0.05, -hz + 0.05, hz - 0.3),
                             (x0, x1, z1, hz - 0.3), (x0, x1, -hz + 0.05, z0)):
            if b - a > 0.05 and d - c > 0.05:
                m.box('concrete', (a, sy, c), (b, sy + SLAB, d))
        m.box('concrete_dark', (-hx + 0.05, sy - 0.35, hz - 0.75), (hx - 0.05, sy, hz - 0.3), faces='xXyzZ')
        _stair(m, x0, S0, y, FLOOR_H, -1, width=x1 - x0)
        if lod == 0:                                       # scaffold tube railing along the hole's open side
            ry = sy + SLAB
            rx = x1 + 0.05 if f % 2 == 0 else x0 - 0.05
            m.cylinder('rust', (rx, ry + 1.0, z0), (rx, ry + 1.0, z1), 0.025, 4, caps=False)
            for zz in (z0, z1):
                m.cylinder('rust', (rx, ry, zz), (rx, ry + 1.0, zz), 0.025, 4, caps=False)
    # block walls started on the ground floor at the back and the party walls (the neighbours' brick shows above)
    for x in (-hx + 0.05, hx - 0.25):
        m.box('st_brick_worn', (x, 0.0, -hz + 0.05), (x + 0.2, FLOOR_H * 1.2, hz - 0.8))
    m.box('concrete_dark', (-hx + 0.3, 0.12, -hz + 0.05), (-1.2, 1.4, -hz + 0.25))        # (the rest open to the yard)
    # materials on the ground floor: pallets of block, rebar, a cement mixer drum, bags
    if lod == 0:
        for k in range(6):
            x, z = rng.uniform(-hx + 1.2, hx - 4.0), rng.uniform(-hz + 1.5, hz - 3.0)
            m.push((x, 0.12, z), rot_y(rng.uniform(0, 90)))
            m.box('wood', (-0.6, 0.0, -0.5), (0.6, 0.14, 0.5))
            if k % 2:
                m.box('concrete', (-0.55, 0.14, -0.45), (0.55, rng.uniform(0.6, 1.0), 0.45))
            else:
                for b in range(4):
                    m.box('paint_cream', (-0.5 + b * 0.25, 0.14, -0.4), (-0.28 + b * 0.25, 0.36, 0.4))
            m.pop()
        for k in range(14):
            z = rng.uniform(-hz + 1.0, -hz + 2.0)
            m.cylinder('rust', (-hx + 1.0, 0.16 + k * 0.012, z), (-hx + 7.0, 0.16 + k * 0.012, z + rng.uniform(-0.1, 0.1)), 0.012, 3, caps=False)
    # scaffold on the street front: standards, ledgers, boards on every lift, braces, green netting
    sz0, sz1 = hz + 0.1, hz + 1.25
    top = floors * FLOOR_H
    lifts = np.arange(2.0, top - 0.5, 2.0)
    for x in np.arange(-hx, hx + 0.01, 2.0):
        for z in (sz0, sz1):
            m.cylinder('rust', (x, 0.0, z), (x, top + 1.0, z), 0.024, 5, caps=False)
    for y in list(lifts) + [top + 0.9]:
        for z in (sz0, sz1):
            m.cylinder('rust', (-hx, y, z), (hx, y, z), 0.024, 5, caps=False)
        if y < top:
            m.box('plywood', (-hx, y + 0.02, sz0), (hx, y + 0.06, sz1))                  # the boards
            m.cylinder('rust', (-hx, y + 1.0, sz1), (hx, y + 1.0, sz1), 0.024, 5, caps=False)    # guard rail
            m.box('wood_dark', (-hx, y + 0.06, sz1 - 0.04), (hx, y + 0.2, sz1))                   # toe board
    if lod == 0:
        for k, x in enumerate(np.arange(-hx, hx - 1.9, 2.0)):
            for y0 in lifts[::2]:
                a, b = (x, y0 - 2.0, sz1), (x + 2.0, y0, sz1)
                if k % 2:
                    a, b = (x, y0, sz1), (x + 2.0, y0 - 2.0, sz1)
                m.cylinder('rust', a, b, 0.022, 4, caps=False)
    # netting: in sheets, a few torn and hanging; both faces so it reads from the scaffold too
    x = -hx
    while x < hx - 0.01:
        w = min(rng.uniform(1.8, 2.6), hx - x)
        y_top = top + 0.9 - (rng.uniform(0.0, 6.0) if rng.random() < 0.3 else 0.0)
        y_bot = 3.6 + (rng.uniform(0.0, 3.0) if rng.random() < 0.3 else 0.0)
        if y_top - y_bot > 1.0:
            p = [(x, y_bot, sz1 + 0.05), (x + w, y_bot, sz1 + 0.05), (x + w, y_top, sz1 + 0.05), (x, y_top, sz1 + 0.05)]
            m.poly('netting', p)
            m.poly('netting', p[::-1])
        x += w
    # the sidewalk shed: green steel posts, beams, plywood deck with a parapet, lamps under it
    s_out = hz + 3.1                     # its outer posts stand at the kerb side of the pavement
    sh = 3.2
    for x in np.arange(-hx - 1.0, hx + 1.01, 2.4):
        for z in (hz + 0.3, s_out):
            m.box('paint_green', (x - 0.08, 0.0, z - 0.08), (x + 0.08, sh, z + 0.08))
    for z in (hz + 0.3, s_out):
        m.box('paint_green', (-hx - 1.1, sh - 0.3, z - 0.1), (hx + 1.1, sh, z + 0.1))
    m.box('plywood', (-hx - 1.1, sh, hz + 0.2), (hx + 1.1, sh + 0.05, s_out + 0.15))
    m.box('paint_green', (-hx - 1.1, sh + 0.05, s_out - 0.02), (hx + 1.1, sh + 1.1, s_out + 0.15))      # parapet
    m.box('paint_green', (-hx - 1.1, sh - 0.6, s_out + 0.1), (hx + 1.1, sh, s_out + 0.15))             # fascia
    if lod == 0:
        for x in np.arange(-hx + 0.2, hx, 3.0):
            m.box('lamp' if rng.random() < 0.0 else 'iron_black', (x - 0.15, sh - 0.42, hz + 1.6), (x + 0.15, sh - 0.3, hz + 1.8))
    # the plywood fence at the lot line, under the shed, with the gate swung open; posters over it
    gx0, gx1 = 1.4, 4.4
    for (a, b) in ((-hx, gx0), (gx1, hx)):
        m.box('plywood', (a, 0.0, hz - 0.1), (b, 2.6, hz - 0.05))
        m.box('wood_dark', (a, 2.4, hz - 0.15), (b, 2.5, hz - 0.1))
        if lod == 0:
            x = a + 0.3
            while x < b - 1.0:
                if rng.random() < 0.7:
                    pw, ph = rng.uniform(0.6, 0.9), rng.uniform(0.9, 1.2)
                    y = rng.uniform(0.6, 1.2)
                    mat = ('poster_a', 'poster_b', 'poster_c')[int(rng.integers(0, 3))]
                    m.poly(mat, [(x, y, hz - 0.04), (x + pw, y, hz - 0.04), (x + pw, y + ph, hz - 0.04), (x, y + ph, hz - 0.04)])
                    if rng.random() < 0.5:                                                 # torn half off
                        m.poly(mat, [(x + pw * 0.3, y + ph * 0.6, hz - 0.035), (x + pw, y + ph * 0.6, hz - 0.035), (x + pw * 0.8, y + ph * 0.6 - 0.4, hz + 0.05)])
                x += rng.uniform(0.8, 1.3)
    m.push((gx1, 0.0, hz - 0.08), rot_y(-115))
    m.box('plywood', (-3.0, 0.05, -0.03), (0.0, 2.5, 0.03))
    m.box('wood_dark', (-3.0, 1.1, 0.03), (0.0, 1.2, 0.08))
    m.pop()
    # the site office: a container on the shed deck (stands on the deck, the level adds no other support)
    return m


def tower_crane(h=40.0, jib=36.0, counter=11.0, lod=0, seed=5):
    """A tower crane left with its jib free to weathervane: lattice mast, slewing unit, cab, jib and counter-jib with
    the concrete counterweights, the trolley parked out on the jib with the hook hanging. Ladder up inside the mast."""
    rng = np.random.default_rng(seed)
    m = _mesh()
    m.ladders = []
    a = 1.0                     # half the mast width
    chord, lace = 0.09, 0.035
    seg = 4 if lod else 6
    m.box('concrete', (-2.5, -0.5, -2.5), (2.5, 0.6, 2.5))                                  # the base
    corners = [(-a, -a), (a, -a), (a, a), (-a, a)]
    for (x, z) in corners:
        m.cylinder('crane_yellow', (x, 0.6, z), (x, h, z), chord, seg, caps=False)
    step = 2.0 if lod == 0 else 4.0
    for y in np.arange(0.6, h - 0.1, step):
        for i in range(4):
            (xa, za), (xb, zb) = corners[i], corners[(i + 1) % 4]
            m.cylinder('crane_yellow', (xa, y, za), (xb, y, zb), lace, 4, caps=False)
            if lod == 0:
                m.cylinder('crane_yellow', (xa, y, za), (xb, y + step, zb), lace, 4, caps=False)
    # slewing unit, cab, the A-frame
    y = h
    m.box('crane_yellow', (-1.3, y, -1.3), (1.3, y + 1.2, 1.3))
    m.box('paint_cream', (1.3, y + 0.2, 0.2), (3.0, y + 2.6, 1.6))                          # the cab
    m.box('interior_dark', (3.0, y + 1.1, 0.35), (3.02, y + 2.4, 1.45))
    for (a0, a1, b0, b1) in ((-1.3, 1.0, -1.3, 1.8), (2.2, 3.2, -1.3, 1.8), (1.0, 2.2, 0.0, 1.8)):   # platform, hatch
        m.box('metal_plate', (a0, y + 1.2, b0), (a1, y + 1.3, b1))
    for (x, z) in ((-0.9, -0.9), (0.9, -0.9), (0.9, 0.9), (-0.9, 0.9)):
        m.cylinder('crane_yellow', (x, y + 1.2, z), (0.0, y + 7.0, 0.0), chord, seg, caps=False)
    # the jib (+X) and counter-jib (-X): triangular lattice
    def lattice(x0, x1, yb, ht, wd):
        n = max(2, int(abs(x1 - x0) / (2.0 if lod == 0 else 4.0)))
        xsx = np.linspace(x0, x1, n + 1)
        for z in (-wd, wd):
            m.cylinder('crane_yellow', (x0, yb, z), (x1, yb, z), chord * 0.8, seg, caps=False)
        m.cylinder('crane_yellow', (x0, yb + ht, 0.0), (x1, yb + ht * 0.6, 0.0), chord * 0.8, seg, caps=False)
        for i in range(n):
            xa_, xb_ = xsx[i], xsx[i + 1]
            t = (xa_ - x0) / (x1 - x0)
            yt = yb + ht * (1 - 0.4 * t)
            m.cylinder('crane_yellow', (xa_, yb, -wd), (xa_, yb, wd), lace, 4, caps=False)
            if lod == 0:
                for z in (-wd, wd):
                    m.cylinder('crane_yellow', (xa_, yb, z), ((xa_ + xb_) / 2, yt, 0.0), lace, 4, caps=False)
                    m.cylinder('crane_yellow', ((xa_ + xb_) / 2, yt, 0.0), (xb_, yb, z), lace, 4, caps=False)
    lattice(1.3, jib, y + 1.2, 1.8, 0.8)
    lattice(-1.3, -counter, y + 1.2, 1.2, 1.0)
    m.box('metal_plate', (-counter, y + 1.2, -1.0), (-1.3, y + 1.3, 1.0))                  # walkway on the counter-jib
    for k in range(4):                                                                       # counterweights
        x = -counter + 0.3 + k * 1.1
        m.box('concrete', (x, y - 1.6, -0.9), (x + 1.0, y + 1.2, 0.9))
    # pendants from the A-frame top to the jibs
    m.cylinder('iron_black', (0.0, y + 7.0, 0.0), (jib * 0.62, y + 3.0, 0.0), 0.03, 4, caps=False)
    m.cylinder('iron_black', (0.0, y + 7.0, 0.0), (-counter + 1.0, y + 2.4, 0.0), 0.03, 4, caps=False)
    # the trolley parked out on the jib, the hook block hanging, the cable slack
    tx = jib * rng.uniform(0.55, 0.8)
    m.box('crane_yellow', (tx - 0.6, y + 0.8, -0.9), (tx + 0.6, y + 1.2, 0.9))
    hy = rng.uniform(6.0, 14.0)
    for z in (-0.12, 0.12):
        m.cylinder('iron_black', (tx, y + 0.8, z), (tx + 0.2, hy + 1.0, z), 0.012, 3, caps=False)
    m.box('crane_yellow', (tx - 0.25, hy, -0.35), (tx + 0.45, hy + 1.0, 0.35))
    m.cylinder('iron_black', (tx + 0.1, hy, 0.0), (tx + 0.1, hy - 0.6, 0.0), 0.08, 6, r2=0.03)
    # the ladder up the outside of the mast (+X face) to the hatch in the platform, hoops of a cage round it
    lz = -0.6
    m.ladders.append((a + 0.12, 0.6, lz, h + 1.3 + 0.9 - 0.6, (-1, 0)))     # x, y, z, height, direction into the mast
    for z in (lz - 0.22, lz + 0.22):
        m.cylinder('iron_black', (a + 0.1, 0.6, z), (a + 0.1, h + 2.2, z), 0.02, 4, caps=False)
    if lod == 0:
        for yy in np.arange(0.9, h + 1.2, 0.3):
            m.cylinder('iron_black', (a + 0.1, yy, lz - 0.22), (a + 0.1, yy, lz + 0.22), 0.012, 3, caps=False)
        for yy in np.arange(3.0, h, 1.5):
            for (p0, p1) in (((a + 0.1, lz - 0.35), (a + 0.85, lz - 0.35)), ((a + 0.85, lz - 0.35), (a + 0.85, lz + 0.35)),
                             ((a + 0.85, lz + 0.35), (a + 0.1, lz + 0.35))):
                m.cylinder('iron_black', (p0[0], yy, p0[1]), (p1[0], yy, p1[1]), 0.012, 3, caps=False)
    m.stash = (-0.3, y + 1.3, 1.1)
    return m


MODELS = {
    'works_trench_8m': lambda lod=0: trench(8.0, 1.6, 1.1, 1, lod),
    'works_trench_12m': lambda lod=0: trench(12.0, 1.8, 1.2, 2, lod),
    'works_spoil': lambda lod=0: spoil_heap(2, lod),
    'works_plate': lambda lod=0: steel_plate(3),
    'works_plate_b': lambda lod=0: steel_plate(8, 1.8, 3.0),
    'works_planks': lambda lod=0: plank_bridge(2.8, 4, 4),
    'works_site': lambda lod=0: site(11, lod),
    'works_crane': lambda lod=0: tower_crane(40.0, 36.0, 11.0, lod),
}
LODS = ('works_trench_8m', 'works_trench_12m', 'works_spoil', 'works_site', 'works_crane')
