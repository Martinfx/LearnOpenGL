"""A brick Gothic Revival parish church for the north end of Mulberry Street (original, CC0).

The landmark of the level (docs/LEVEL_ART_GUIDE.md section 11: the goal in sight from the start): a nave of dark brick
with stone trim, buttresses and tall pointed windows, a steep slate roof, and over the front a square tower with an
open belfry and a copper spire gone green, 52 m high, so it shows over the roofs from Canal Street. Decades empty:
the windows broken, the side door off its hinges, stone steps up to the portal.

Frame: the front faces +Z at z = +17 (the steps run out to z = +20), the nave runs back to z = -17, x centred on 0,
y = 0 is the pavement.
"""
import math

import numpy as np

from .buildings import wall_z
from .mats import M, PALETTE
from .mesh import Mesh, rot_y
from .skrunda import _frame

W, L = 18.0, 34.0            # nave width and length
WALL_H = 14.0
RIDGE = 23.0
T = 0.7
TW = 7.0                     # tower width
TOWER_H = 36.0
SPIRE_H = 17.0

GLASS = {                    # stained glass seen from the street: dark, the colour shows only where light comes through
    'sg_red': (0.3, 0.035, 0.03), 'sg_blue': (0.03, 0.06, 0.24), 'sg_amber': (0.36, 0.2, 0.03),
    'sg_green': (0.04, 0.16, 0.06), 'sg_violet': (0.14, 0.04, 0.19), 'sg_pale': (0.2, 0.19, 0.15),
}
PAL = dict(PALETTE)
for _k, _c in GLASS.items():
    PAL[_k] = M(None, 1.0, color=_c, rough=0.25, metal=0.0, alpha=0.92)
PAL['brownstone'] = dict(PALETTE['concrete'], color=(0.8, 0.58, 0.47))     # the trim: New York brownstone
PAL['clock_face'] = dict(PALETTE['concrete'], color=(0.95, 0.92, 0.82))


def wall_x(m, x0, x1, z, y0, y1, t, out_mat, in_mat, openings=(), out_side=1):
    """A wall along X like buildings.wall_x, but openings may stand over one another (a door under a lancet, the
    belfry over the portal): the wall is cut into strips at every opening edge and each strip is filled between them."""
    za, zb = z - t / 2, z + t / 2
    mats = {'Z': out_mat, 'z': in_mat} if out_side > 0 else {'z': out_mat, 'Z': in_mat}
    for k in 'xXyY':
        mats[k] = in_mat
    xs = sorted({x0, x1, *[o[0] - o[1] / 2 for o in openings], *[o[0] + o[1] / 2 for o in openings]})
    xs = [x for x in xs if x0 <= x <= x1]
    for xa, xb in zip(xs, xs[1:]):
        xm = (xa + xb) / 2
        spans = sorted((b, tp) for cx, w, b, tp in openings if abs(xm - cx) < w / 2)
        y = y0
        for b, tp in spans:
            if b > y:
                m.box(None, (xa, y, za), (xb, b, zb), mats=mats)
            y = max(y, tp)
        if y < y1:
            m.box(None, (xa, y, za), (xb, y1, zb), mats=mats)


def pointed(m, mat, cx, y0, w, h, z, depth=0.12, out=1):
    """A pointed arch outline (stone) on a wall face at z: two jambs and two leaning voussoir strips."""
    hw = w / 2
    za, zb = sorted((z, z + out * depth))
    m.box(mat, (cx - hw - 0.15, y0, za), (cx - hw, y0 + h, zb))
    m.box(mat, (cx + hw, y0, za), (cx + hw + 0.15, y0 + h, zb))
    apex = y0 + h + hw * 1.1
    for s in (-1, 1):
        m.poly(mat, [(cx + s * (hw + 0.15), y0 + h, zb), (cx, apex + 0.2, zb), (cx, apex, zb), (cx + s * hw, y0 + h, zb)][::(1 if s * out < 0 else -1)])


def leaded(m, rng, w, y0, h, z=-0.3, missing=0.3, lod=0, head=True):
    """A stained glass lancet in the local frame (+Z out of the wall, x centred): panes in a lead grid, a blue border,
    a medallion of red, amber and violet in the middle, some panes broken out; the pointed head over it."""
    hw = w / 2
    if lod > 0:
        m.poly('sg_blue', [(-hw, y0, z), (hw, y0, z), (hw, y0 + h, z), (-hw, y0 + h, z)])
        return
    nc = max(3, int(round(w / 0.22)))
    nr = max(4, int(round(h / 0.32)))
    cw, rh = w / nc, h / nr
    mid = (nr - 1) / 2
    for i in range(nc):
        for j in range(nr):
            if rng.random() < missing * (1.3 if j < 3 else 0.8):       # stones thrown from the street broke the low panes
                continue
            if i in (0, nc - 1) or j in (0, nr - 1):
                mat = 'sg_blue'
            elif abs(j - mid) < nr * 0.18 or abs(j - mid * 0.45) < 1.2:
                mat = ('sg_red', 'sg_amber', 'sg_violet')[int(rng.integers(0, 3))]
            else:
                mat = ('sg_pale', 'sg_pale', 'sg_green', 'sg_amber', 'sg_blue')[int(rng.integers(0, 5))]
            x0, x1 = -hw + i * cw, -hw + (i + 1) * cw
            ya, yb = y0 + j * rh, y0 + (j + 1) * rh
            m.poly(mat, [(x0, ya, z), (x1, ya, z), (x1, yb, z), (x0, yb, z)])
    # the storm glazing put over it in the seventies, gone grey and cracked: it mutes the colours from the street
    for k in range(3):
        ya, yb = y0 + h * k / 3, y0 + h * (k + 1) / 3
        if rng.random() < 0.7:
            m.poly('glass', [(-hw, ya, z + 0.1), (hw, ya, z + 0.1), (hw, yb, z + 0.1), (-hw, yb, z + 0.1)])
    for i in range(1, nc):                                              # the lead and the saddle bars
        x = -hw + i * cw
        m.box('iron_black', (x - 0.012, y0, z - 0.01), (x + 0.012, y0 + h, z + 0.02))
    for j in range(1, nr):
        y = y0 + j * rh
        m.box('iron_black', (-hw, y - (0.025 if j % 3 == 0 else 0.01), z - 0.01), (hw, y + (0.025 if j % 3 == 0 else 0.01), z + 0.02))
    if head:                                                            # the pointed head: two halves and a small rose
        apex = y0 + h + hw * 1.1
        top = y0 + h
        m.poly('sg_red', [(-hw, top, z), (0.0, top, z), (0.0, apex, z)])
        m.poly('sg_blue', [(0.0, top, z), (hw, top, z), (0.0, apex, z)])
        r = hw * 0.42
        cy = top + hw * 0.45
        pts = [(math.cos(a) * r, cy + math.sin(a) * r, z + 0.01) for a in np.linspace(0, 2 * math.pi, 9)[:-1]]
        m.poly('sg_amber', pts)


def church(lod=0, seed=7):
    rng = np.random.default_rng(seed)
    m = Mesh(PAL)
    hx, hz = W / 2, L / 2
    brick, stone, roof = 'st_brick_dark', 'brownstone', 'concrete_dark'
    # nave walls with tall lancet openings (dark inside)
    bays = np.linspace(-hz + 3.0, hz - TW - 2.0, 6)
    for sx in (-1, 1):
        ops = [(z, 1.8, 4.0, 10.5) for z in bays]
        wall_z(m, -hz, hz, sx * (hx - T / 2), 0.0, WALL_H, T, brick, 'interior_dark', ops, out_side=sx)
        for z in bays:                                          # pointed heads, sills, buttresses between bays
            m.push((sx * hx, 0.0, z), rot_y(90 * sx))
            leaded(m, rng, 1.8, 4.0, 6.5, missing=rng.uniform(0.15, 0.5), lod=lod, head=False)
            m.poly(brick, [(-0.9, 10.5, 0.0), (0.9, 10.5, 0.0), (0.0, 12.2, 0.0)])
            pointed(m, stone, 0.0, 4.0, 1.8, 6.5, 0.0, 0.18)                       # the hood mould
            m.box(stone, (-0.06, 4.0, -0.32), (0.06, 10.5, -0.22))               # the mullion
            m.box(stone, (-1.1, 3.8, 0.0), (1.1, 4.0, 0.25))
            m.pop()
        for z in np.concatenate(([-hz + 0.5], (bays[:-1] + bays[1:]) / 2, [bays[-1] + 2.2])):
            m.box(brick, (sx * hx - (1.0 if sx < 0 else 0), 0.0, z - 0.45), (sx * hx + (1.0 if sx > 0 else 0), 9.0, z + 0.45))
            m.box(stone, (sx * hx - (1.1 if sx < 0 else 0), 9.0, z - 0.5), (sx * hx + (1.1 if sx > 0 else 0), 9.4, z + 0.5))
            m.box(brick, (sx * hx - (0.5 if sx < 0 else 0), 9.4, z - 0.4), (sx * hx + (0.5 if sx > 0 else 0), 12.5, z + 0.4))
    wall_x(m, -hx, hx, -hz + T / 2, 0.0, WALL_H, T, brick, 'interior_dark', [(0.0, 3.0, 5.0, 12.0)], out_side=-1)
    m.box('interior_dark', (-1.5, 5.0, -hz + 0.2), (1.5, 12.0, -hz + 0.25))
    m.box(stone, (-hx - 0.2, WALL_H - 0.4, -hz - 0.2), (hx + 0.2, WALL_H, hz + 0.2), faces='xXyzZ')        # eaves course
    # the roof: steep slate, gable walls
    for sx in (-1, 1):
        m.poly(roof, [(sx * (hx + 0.4), WALL_H - 0.3, hz + 0.3), (sx * (hx + 0.4), WALL_H - 0.3, -hz - 0.3),
                      (0.0, RIDGE, -hz - 0.3), (0.0, RIDGE, hz + 0.3)][::(-1 if sx > 0 else 1)])
        m.poly('interior_dark', [(sx * (hx + 0.4), WALL_H - 0.3, hz + 0.3), (sx * (hx + 0.4), WALL_H - 0.3, -hz - 0.3),
                                 (0.0, RIDGE, -hz - 0.3), (0.0, RIDGE, hz + 0.3)][::(1 if sx > 0 else -1)])
    for zc, out in ((hz, 1), (-hz, -1)):
        tri = [(-hx, WALL_H, zc), (hx, WALL_H, zc), (0.0, RIDGE + 0.3, zc)]
        m.poly(brick, tri if out > 0 else tri[::-1])
        m.poly('interior_dark', tri[::-1] if out > 0 else tri)
    # the front: the gable with a rose window, the portal under the tower, side doors in the aisles
    zf = hz
    th = TW / 2
    m.box(stone, (-hx, 0.0, zf), (hx, 0.6, zf + 0.3))
    for sx in (-1, 1):                                          # corner buttresses of the front
        m.box(brick, (sx * hx - 0.8, 0.0, zf - 0.2), (sx * hx + 0.4, 15.5, zf + 0.9))
        m.cylinder(stone, (sx * (hx - 0.2), 15.5, zf + 0.35), (sx * (hx - 0.2), 18.5, zf + 0.35), 0.45, 4, caps=True, r2=0.02)
        cx = sx * (th + (hx - th) / 2)                          # aisle doors: one shut, one open into the dark
        xa, xb = sorted((sx * th, sx * hx))
        wall_x(m, xa, xb, zf - T / 2, 0.0, WALL_H, T, brick, 'interior_dark', [(cx, 1.6, 0.6, 3.6), (cx, 1.1, 7.2, 11.0)], out_side=1)
        m.box('interior_dark', (cx - 0.8, 0.6, zf - T - 0.02), (cx + 0.8, 3.6, zf - T))
        if sx > 0:
            m.box('wood_dark', (cx - 0.8, 0.6, zf - 0.32), (cx + 0.8, 3.6, zf - 0.24))
        pointed(m, stone, cx, 0.6, 1.6, 3.0, zf, 0.2)
        m.box(stone, (cx - 0.9, 7.0, zf), (cx + 0.9, 7.2, zf + 0.2))                # lancet over the door
        m.box('interior_dark', (cx - 0.55, 7.2, zf - T - 0.02), (cx + 0.55, 11.0, zf - T))
        m.push((cx, 0.0, zf), rot_y(0))
        leaded(m, rng, 1.1, 7.2, 3.8, z=-0.25, missing=0.2, lod=lod, head=False)
        m.pop()
        pointed(m, stone, cx, 7.2, 1.1, 3.8, zf, 0.15)
    # the tower: square, buttressed, belfry with louvred pointed openings, pinnacles, the green spire
    tz0, tz1 = zf - TW + 0.5, zf + 1.2
    for sx in (-1, 1):
        m.box(brick, (sx * th - (T if sx > 0 else 0), 0.0, tz0), (sx * th + (T if sx < 0 else 0), TOWER_H, tz1))
    m.box(brick, (-th, 0.0, tz0), (th, TOWER_H, tz0 + T))
    # front wall of the tower with the portal (open: the doors are gone) and the belfry openings
    ops = [(0.0, 2.6, 0.6, 5.2), (0.0, 1.2, 11.0, 15.0)] + [(x, 1.1, 27.0, 31.5) for x in (-1.4, 1.4)]
    wall_x(m, -th, th, tz1 - T / 2, 0.0, TOWER_H, T, brick, 'interior_dark', ops, out_side=1)
    pointed(m, stone, 0.0, 0.6, 2.6, 4.6, tz1, 0.25)
    m.box(stone, (-2.2, 0.0, tz1), (2.2, 0.6, tz1 + 0.6))
    for k, (dz, dy) in enumerate(((0.6, 0.0), (1.4, -0.15), (2.2, -0.3))):   # the steps out onto the pavement
        m.box(stone, (-4.0 - k * 0.5, -0.3, tz1 + dz - 0.8), (4.0 + k * 0.5, 0.6 + dy - 0.15 * k, tz1 + dz))
    pointed(m, stone, 0.0, 11.0, 1.2, 4.0, tz1, 0.15)
    m.push((0.0, 0.0, tz1 - 0.25), rot_y(0))
    leaded(m, rng, 1.2, 11.0, 4.0, z=0.0, missing=0.25, lod=lod)
    m.pop()
    # the clock, stopped at twenty past four
    cy = 20.3
    m.cylinder(stone, (0.0, cy, tz1), (0.0, cy, tz1 + 0.22), 1.45, 16 if lod == 0 else 10)
    m.cylinder('clock_face', (0.0, cy, tz1 + 0.22), (0.0, cy, tz1 + 0.26), 1.2, 16 if lod == 0 else 10)
    if lod == 0:
        for k in range(12):
            a = math.radians(k * 30)
            r0, r1 = (0.9, 1.12) if k % 3 else (0.78, 1.12)
            m.push((0.0, cy, tz1 + 0.27), np.array([[math.cos(a), -math.sin(a), 0], [math.sin(a), math.cos(a), 0], [0, 0, 1.0]]))
            m.box('iron_black', (-0.035, r0, 0.0), (0.035, r1, 0.02))
            m.pop()
        for deg, ln, wd in ((-130.0, 0.6, 0.06), (-120.0 + 360 * 20 / 60, 0.95, 0.04)):
            a = math.radians(-deg + 90)
            m.push((0.0, cy, tz1 + 0.3), np.array([[math.cos(a), -math.sin(a), 0], [math.sin(a), math.cos(a), 0], [0, 0, 1.0]]))
            m.box('iron_black', (-wd / 2, -0.1, 0.0), (wd / 2, ln, 0.02))
            m.pop()
    m.box(stone, (-1.7, cy - 1.75, tz1), (1.7, cy - 1.55, tz1 + 0.3))
    for sx in (-1, 1):
        for x in (sx * 1.4,):
            pointed(m, stone, x, 27.0, 1.1, 4.5, tz1, 0.15)
            for yy in np.arange(27.4, 31.4, 0.5):                # louvres
                m.box('wood_dark', (x - 0.55, yy, tz1 - 0.4), (x + 0.55, yy + 0.06, tz1 - 0.25))
        m.push((sx * th, 0, (tz0 + tz1) / 2), rot_y(90 * sx))   # belfry openings on the sides too
        for x in (-1.3, 1.3):
            m.box('interior_dark', (x - 0.55, 27.0, -0.02), (x + 0.55, 31.5, 0.0))
            pointed(m, stone, x, 27.0, 1.1, 4.5, 0.0, 0.15)
        m.pop()
    m.box(stone, (-th - 0.3, 24.5, tz0 - 0.3), (th + 0.3, 25.0, tz1 + 0.3))          # string courses
    m.box(stone, (-th - 0.4, TOWER_H - 0.6, tz0 - 0.4), (th + 0.4, TOWER_H, tz1 + 0.4))
    # a parapet of merlons along the top of the tower between the pinnacles, a few fallen
    for k in range(4):
        m.push((0.0, TOWER_H, cz0 := (tz0 + tz1) / 2), rot_y(k * 90))
        half = th + 0.4
        for x in np.arange(-half + 0.9, half - 0.7, 0.75):
            if rng.random() < 0.85:
                m.box(stone, (x, 0.0, half - 0.35), (x + 0.42, 0.9, half))
        m.box(stone, (-half, 0.0, half - 0.35), (half, 0.25, half))
        m.pop()
    for sx in (-1, 1):                                          # angle buttresses climbing the tower, set back twice
        for zz in (tz0, tz1):
            for y0, y1, d in ((0.0, 12.0, 0.9), (12.0, 24.5, 0.6), (24.5, TOWER_H - 0.6, 0.35)):
                m.box(brick, (sx * th - d, y0, zz - d), (sx * th + d, y1, zz + d))
            m.cylinder(stone, (sx * th, TOWER_H, zz), (sx * th, TOWER_H + 4.0, zz), 0.55, 4, caps=True, r2=0.03)   # pinnacles
    # the spire: octagonal, copper turned green, a cross that leans
    cz = (tz0 + tz1) / 2
    m.cylinder('paint_green', (0.0, TOWER_H, cz), (0.0, TOWER_H + SPIRE_H, cz), th * 0.95, 8, caps=True, r2=0.05)
    r_at = lambda t: th * 0.95 * (1 - t) + 0.05 * t
    for t in (0.08, 0.3, 0.55, 0.78):                                    # the copper bands
        y = TOWER_H + t * SPIRE_H
        m.cylinder('iron_black', (0.0, y, cz), (0.0, y + 0.22, cz), r_at(t) * 1.05, 8)
    for k in range(4):                                                   # lucarnes on the four sides of the spire
        m.push((0.0, TOWER_H, cz), rot_y(k * 90 + 22.5))
        rr = r_at(0.12)
        m.box('paint_green', (-0.5, 1.0, rr - 1.2), (0.5, 2.6, rr + 0.25))
        m.box('interior_dark', (-0.32, 1.15, rr + 0.25), (0.32, 2.35, rr + 0.27))
        m.poly('paint_green', [(-0.62, 2.6, rr + 0.3), (0.62, 2.6, rr + 0.3), (0.0, 3.4, rr + 0.3)])
        for sx in (-1, 1):
            m.poly('paint_green', [(sx * 0.62, 2.6, rr + 0.3), (0.0, 3.4, rr + 0.3), (0.0, 3.4, rr - 1.3), (sx * 0.62, 2.6, rr - 1.3)][::(1 if sx > 0 else -1)])
        m.pop()
    m.cylinder('iron_black', (0.0, TOWER_H + SPIRE_H - 0.2, cz), (0.15, TOWER_H + SPIRE_H + 2.4, cz), 0.06, 4)
    m.box('iron_black', (-0.6, TOWER_H + SPIRE_H + 1.5, cz - 0.05), (0.75, TOWER_H + SPIRE_H + 1.65, cz + 0.05))
    # the rose window in the gable, to either side of the tower the gable shows over the aisles
    if lod == 0:
        for sx in (-1, 1):
            for k in range(3):
                y = 15.0 + k * 1.6
                x0 = sx * th + (0.3 if sx > 0 else -0.3)
                x1 = sx * (hx - 0.5 - k * 1.6)
                a, b = sorted((x0, x1))
                if b - a > 0.6:
                    m.box(stone, (a, y, zf), (b, y + 0.15, zf + 0.2))
    # the iron railing round the front yard beside the steps, one stretch pushed over
    if lod == 0:
        zr = tz1 + 1.6
        for sx in (-1, 1):
            x0, x1 = sorted((sx * 5.4, sx * (hx + 1.2)))
            lean = rot_y(0) if sx < 0 else np.array([[1, 0, 0], [0, math.cos(0.5), -math.sin(0.5)], [0, math.sin(0.5), math.cos(0.5)]])
            m.box(stone, (x0, 0.0, zr - 0.15), (x1, 0.3, zr + 0.15))                  # the stone curb it stands on
            for x in (x0, x1):
                m.box(stone, (x - 0.2, 0.0, zr - 0.2), (x + 0.2, 1.9, zr + 0.2))
            m.push((0.0, 0.3, zr), lean)
            for y in (0.12, 1.1):
                m.box('iron_black', (x0 + 0.2, y, -0.02), (x1 - 0.2, y + 0.04, 0.02))
            for x in np.arange(x0 + 0.26, x1 - 0.2, 0.13):
                m.cylinder('iron_black', (x, 0.0, 0.0), (x, 1.25, 0.0), 0.012, 4, caps=False)
                m.cylinder('iron_black', (x, 1.25, 0.0), (x, 1.38, 0.0), 0.03, 4, r2=0.002)
            m.pop()
    # rubble and slates fallen at the foot of the walls
    for k in range(40 if lod == 0 else 10):
        sx = rng.choice((-1, 1))
        z = rng.uniform(-hz, hz)
        s = rng.uniform(0.15, 0.45)
        m.push((sx * (hx + rng.uniform(0.3, 2.0)), 0.0, z), rot_y(rng.uniform(0, 360)))
        m.box('concrete_dark' if rng.random() < 0.5 else brick, (-s, -0.1, -s * 0.6), (s, s * 0.4, s * 0.6))
        m.pop()
    return m
