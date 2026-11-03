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

from .buildings import wall_x, wall_z
from .mats import PALETTE
from .mesh import Mesh, rot_y
from .skrunda import _frame

W, L = 18.0, 34.0            # nave width and length
WALL_H = 14.0
RIDGE = 23.0
T = 0.7
TW = 7.0                     # tower width
TOWER_H = 36.0
SPIRE_H = 17.0


def pointed(m, mat, cx, y0, w, h, z, depth=0.12, out=1):
    """A pointed arch outline (stone) on a wall face at z: two jambs and two leaning voussoir strips."""
    hw = w / 2
    za, zb = sorted((z, z + out * depth))
    m.box(mat, (cx - hw - 0.15, y0, za), (cx - hw, y0 + h, zb))
    m.box(mat, (cx + hw, y0, za), (cx + hw + 0.15, y0 + h, zb))
    apex = y0 + h + hw * 1.1
    for s in (-1, 1):
        m.poly(mat, [(cx + s * (hw + 0.15), y0 + h, zb), (cx, apex + 0.2, zb), (cx, apex, zb), (cx + s * hw, y0 + h, zb)][::(1 if s * out < 0 else -1)])


def church(lod=0, seed=7):
    rng = np.random.default_rng(seed)
    m = Mesh(PALETTE)
    hx, hz = W / 2, L / 2
    brick, stone, roof = 'st_brick_dark', 'concrete', 'concrete_dark'
    # nave walls with tall lancet openings (dark inside)
    bays = np.linspace(-hz + 3.0, hz - TW - 2.0, 6)
    for sx in (-1, 1):
        ops = [(z, 1.8, 4.0, 10.5) for z in bays]
        wall_z(m, -hz, hz, sx * (hx - T / 2), 0.0, WALL_H, T, brick, 'interior_dark', ops, out_side=sx)
        for z in bays:                                          # pointed heads, sills, buttresses between bays
            m.push((sx * hx, 0.0, z), rot_y(90 * sx))
            if lod == 0:
                _frame(m, np.eye(3), 0.0, 0.0, 4.0, 10.5, 1.8, rng, 'broken' if rng.random() < 0.7 else 'empty', frame_mat='iron_black')
            m.poly(brick, [(-0.9, 10.5, 0.0), (0.9, 10.5, 0.0), (0.0, 12.2, 0.0)])
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
        m.box('interior_dark', (cx - 0.8, 0.6, zf + 0.0), (cx + 0.8, 3.6, zf + 0.02))
        if sx > 0:
            m.box('wood_dark', (cx - 0.8, 0.6, zf + 0.03), (cx + 0.8, 3.6, zf + 0.1))
        pointed(m, stone, cx, 0.6, 1.6, 3.0, zf, 0.2)
        m.box(stone, (cx - 0.9, 7.0, zf), (cx + 0.9, 7.2, zf + 0.2))                # lancet over the door
        m.box('interior_dark', (cx - 0.55, 7.2, zf + 0.0), (cx + 0.55, 11.0, zf + 0.02))
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
    for sx in (-1, 1):                                          # angle buttresses climbing the tower, set back twice
        for zz in (tz0, tz1):
            for y0, y1, d in ((0.0, 12.0, 0.9), (12.0, 24.5, 0.6), (24.5, TOWER_H - 0.6, 0.35)):
                m.box(brick, (sx * th - d, y0, zz - d), (sx * th + d, y1, zz + d))
            m.cylinder(stone, (sx * th, TOWER_H, zz), (sx * th, TOWER_H + 4.0, zz), 0.55, 4, caps=True, r2=0.03)   # pinnacles
    # the spire: octagonal, copper turned green, a cross that leans
    cz = (tz0 + tz1) / 2
    m.cylinder('paint_green', (0.0, TOWER_H, cz), (0.0, TOWER_H + SPIRE_H, cz), th * 0.95, 8, caps=True, r2=0.05)
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
    # rubble and slates fallen at the foot of the walls
    for k in range(40 if lod == 0 else 10):
        sx = rng.choice((-1, 1))
        z = rng.uniform(-hz, hz)
        s = rng.uniform(0.15, 0.45)
        m.push((sx * (hx + rng.uniform(0.3, 2.0)), 0.0, z), rot_y(rng.uniform(0, 360)))
        m.box('concrete_dark' if rng.random() < 0.5 else brick, (-s, -0.1, -s * 0.6), (s, s * 0.4, s * 0.6))
        m.pop()
    return m
