"""New York walk-up tenements, one house per lot (original, CC0). docs/LEVEL_ART_GUIDE.md sections 10 to 12.

martin 2026-11-03: "make it after real architecture". A Lower Manhattan street is not a few long blocks but a
row of narrow houses, each on its own 25-foot lot (7.6 m; 20, 30 and double 50-foot lots too), each with its own
height, brick, cornice and window rhythm. One house here:
  - 3 to 7 storeys, a tall ground floor (3.9 m) with a cast-iron shop front and the residential door at one side,
    storeys of 3.05 m above, four windows across a 25-foot front with stone sills and lintels (flat, hooded or
    arched), a pressed-metal cornice on brackets (or a corbelled brick one), a date panel on some;
  - a black iron fire escape over the two middle bays, a drop ladder over the pavement and a gooseneck ladder to
    the roof; on the back a plainer one over the yard;
  - a flat tar roof behind a parapet, chimneys on the party walls, the stair bulkhead, on tall houses a water tank;
  - in the pavement two steel cellar doors, two stone steps at the house door.
enter=True (LOD 0): the house can be walked through, as real tenements are planned: the house door opens into a
hall along one party wall that runs to the back door to the yard; beside it a switchback stair climbs to every
floor and on through the bulkhead onto the roof; front and rear flats (railroad rooms) open off the hall on each
floor, gutted and full of junk; the shop in front has its own open door.
state='burnt': the roof and the floors burnt and fell in, the walls stand with scorched windows (enterable through
the house door into the rubble).

Frame: the street front faces +Z at z = depth / 2, the width runs along X centred on 0, y = 0 is the pavement.
corner='+x' or '-x': that side wall is a second street front (the house stands on a corner).
"""
import math

import numpy as np

from .buildings import wall_x, wall_z, slab, railing_x, railing_z
from .mats import PALETTE
from .mesh import Mesh, rot_x, rot_y
from .skrunda import _frame, grime
from .fixtures import SIGNS, SIGN_H, sign

GROUND = 3.9        # shop floor, floor to floor
STOREY = 3.05       # residential storey
T = 0.4             # brick wall (16 inches)
WIN_W, WIN_H, SILL = 0.95, 1.75, 0.85
STEP_RISE, STEP_RUN = 0.18, 0.26


def floor_y(f):
    return 0.0 if f == 0 else GROUND + (f - 1) * STOREY


def _flight_x(m, x_start, x_end, z0, z1, y_start, y_end, mat, under):
    """Straight flight from (x_start, y_start) to (x_end, y_end), walking along +-X, solid stringer under it."""
    n = max(1, int(round(abs(y_end - y_start) / STEP_RISE)))
    rise, run = (y_end - y_start) / n, (x_end - x_start) / n
    for i in range(n):
        top = y_start + rise * (i + 1)
        a, b = sorted((x_start + run * i, x_start + run * (i + 1)))
        m.box(None, (a, top - 0.22, z0), (b, top, z1), mats={'Y': mat, 'y': under, 'z': under, 'Z': under, 'x': mat, 'X': mat})


def walkup(width=7.6, depth=18.0, floors=5, seed=0, lod=0, skin='st_brick_red', cornice='metal', lintel='flat',
           shop=True, enter=False, corner=None, state='intact', tank=None, date=True, drop=False):
    rng = np.random.default_rng(seed)
    m = Mesh(PALETTE)
    m.spots, m.fire_escapes = [], []
    m.ladders = []          # (x, y, z, height, out): climbable ladders, the level puts a ladder entity on each
    hx, hz = width / 2, depth / 2
    burnt = state == 'burnt'
    enter = enter and lod == 0
    top = floor_y(floors)                     # roof level
    trim = 'concrete'
    party = 'st_brick_worn' if skin != 'st_brick_dark' else 'st_brick_dark'
    nb = max(2, int(round(width / 1.9)))      # four windows across 25 feet
    bay = (width - 2 * 0.3) / nb
    bays = [-hx + 0.3 + bay * (i + 0.5) for i in range(nb)]
    a = -hx + T                               # inner face of the left party wall: the hall runs along it
    HALL = 1.25
    hall_x = a + HALL / 2
    double = nb >= 7
    if double:                                # a double house: the hall in the middle
        a = -HALL / 2
        hall_x = 0.0
    # stair well beside the hall (switchback along X), in the middle of the depth
    half_g = GROUND / 2
    run_g = int(round(half_g / STEP_RISE)) * STEP_RUN
    SW0, SW1 = a + HALL, a + HALL + run_g + 1.05          # x of the well
    SZ0, SZ1 = -1.3, 1.1                                   # z of the well
    inner_x1 = hx - T
    drng = np.random.default_rng(seed + 7000)
    walls_in = 'wallpaper'
    floor_mat = 'wood_floor'

    # ------------------------------------------------------------------ floors, roof, inner walls
    inner = (-hx + T, -hz + T, hx - T, hz - T)
    if burnt:
        slab(m, inner, [], 0.02, 0.3, 'concrete_floor', 'interior_dark', 'interior_dark')
    else:
        for f in range(floors + 1):
            y = floor_y(f)
            holes = []
            if enter and 0 < f:
                holes.append((SW0, SZ0, SW1, SZ1))
            fm = 'roof_tar' if f == floors else ('tiles_floor' if f == 0 and enter else (floor_mat if enter else 'flat_floor'))
            cm = 'plaster' if enter else 'interior_dark'
            slab(m, inner, holes, y + (0.02 if f == 0 else 0.0), 0.3 if f == 0 else 0.25, fm, cm, cm)
    if not burnt and not enter:
        # a closed house: a spine wall so a window shows a dark room, not the sky through the back windows
        wall_x(m, -hx + T, hx - T, 0.0, 0.0, top, 0.2, 'interior_dark', 'interior_dark')
    if enter:
        for f in range(floors):
            y0, y1 = floor_y(f), floor_y(f + 1) - 0.25
            # the hall: walls along it with doorways into the front and the rear flat (on the ground floor the
            # front part is the shop, shut off from the hall)
            hw_x = a + HALL
            for z0, z1, front in ((SZ1 + 0.1, hz - T, True), (-hz + T, SZ0 - 0.1, False)):
                ops = [] if (f == 0 and front) else [((z0 + z1) / 2 + (1.0 if front else -1.0) * min(1.2, (z1 - z0) / 4), 0.95, y0 + 0.02, y0 + 2.15)]
                wall_z(m, z0, z1, hw_x + 0.06, y0, y1, 0.12, walls_in, 'plaster', ops, out_side=1)
                if double:                                   # the other half of a double house
                    wall_z(m, z0, z1, -HALL / 2 - 0.06, y0, y1, 0.12, walls_in, 'plaster', ops, out_side=-1)
            # walls round the stair well (front and back), the hall stays open
            for zw in (SZ1 + 0.06, SZ0 - 0.06):
                wall_x(m, SW0, inner_x1, zw, y0, y1, 0.12, walls_in, walls_in)
            if double:
                for zw in (SZ1 + 0.06, SZ0 - 0.06):
                    wall_x(m, -hx + T, a, zw, y0, y1, 0.12, walls_in, walls_in)
            # railroad rooms: one partition across each flat with a doorway
            for zp, lo, hi in (((SZ1 + hz) / 2 + 0.3, SZ1, hz), ((SZ0 - hz) / 2 - 0.3, -hz, SZ0)):
                if f == 0 and zp > 0:
                    continue                                  # the shop is one room
                x0 = SW0 + 0.12 if not double else a + HALL + 0.12
                wall_x(m, x0, inner_x1, zp, y0, y1, 0.1, walls_in, walls_in, [(x0 + 0.9, 0.9, y0 + 0.02, y0 + 2.1)])
            # what is left: plaster down from the ceilings, a wardrobe, a bed frame, a table, rags
            for _ in range(int(width * depth / 30)):
                x, z = drng.uniform(SW0 + 0.4, inner_x1 - 0.4), drng.uniform(-hz + T + 0.5, hz - T - 0.5)
                if SZ0 - 0.7 < z < SZ1 + 0.7:
                    continue
                m.push((x, y0 + 0.02 if f else 0.04, z), rot_y(drng.choice((0, 90, 180, 270)) + drng.uniform(-12, 12)))
                r = drng.random()
                if r < 0.45:
                    s = drng.uniform(0.3, 0.7)
                    m.box('plaster', (-s, 0.0, -s * 0.7), (s, s * 0.22, s * 0.7))
                elif r < 0.65:
                    m.box('wood_dark', (-0.55, 0.0, -0.3), (0.55, 1.95, 0.3))
                elif r < 0.82:
                    m.box('rust', (-0.95, 0.35, -0.45), (0.95, 0.42, 0.45))                  # bed frame
                    for sx in (-0.9, 0.9):
                        m.box('rust', (sx - 0.03, 0.0, -0.45), (sx + 0.03, 0.9 if sx < 0 else 0.6, 0.45))
                else:
                    m.box('wood', (-0.6, 0.72, -0.4), (0.6, 0.77, 0.4))                     # table
                    for sx in (-0.52, 0.52):
                        for sz in (-0.32, 0.32):
                            m.box('wood', (sx - 0.03, 0.0, sz - 0.03), (sx + 0.03, 0.72, sz + 0.03))
                m.pop()
        # the shop: a counter along the hall wall, empty shelves on the party wall
        if shop:
            zc = (SZ1 + hz) / 2
            m.box('wood_dark', (SW0 + 0.3, 0.02, zc - 1.6), (SW0 + 0.9, 1.0, zc + 1.2))
            if not double:
                for yy in (0.4, 1.1, 1.8):
                    m.box('wood', (inner_x1 - 0.4, yy, SZ1 + 0.6), (inner_x1, yy + 0.04, hz - T - 0.8))
        # the stair: two flights a storey, landing at the far end, on through the roof into the bulkhead
        for f in range(floors):
            y = floor_y(f) + (0.02 if f == 0 else 0.0)
            h = floor_y(f + 1) - y
            n = int(round(h / 2 / STEP_RISE))
            run = n * STEP_RUN
            _flight_x(m, SW0, SW0 + run, SZ0, SZ0 + 1.1, y, y + h / 2, 'stair_paint', 'plaster')
            m.box(None, (SW0 + run, y + h / 2 - 0.2, SZ0), (SW1, y + h / 2, SZ1),
                  mats={k: ('stair_paint' if k == 'Y' else 'plaster') for k in 'xXyYzZ'})
            _flight_x(m, SW0 + run, SW0, SZ1 - 1.1, SZ1, y + h / 2, y + h, 'stair_paint', 'plaster')
            yt = floor_y(f + 1)
            m.box('wood_dark', (SW0 - 0.02, yt + 0.95, SZ0 + 1.1), (SW0 + 0.04, yt + 1.0, SZ1 - 1.1))      # newel rail
            railing_x(m, SW0 + 0.05, SW1 - 0.05, SZ0 + 1.15, yt, 1.0) if f < floors - 1 else None
        # the bulkhead on the roof over the well, its door open to the roof
        bx0, bx1, bz0, bz1, bh = SW0 - 1.3, SW1 + 0.15, SZ0 - 0.15, SZ1 + 0.15, 2.6
        wall_z(m, bz0, bz1, bx0, top, top + bh, 0.15, 'st_brick_worn', 'plaster', out_side=-1)
        wall_z(m, bz0, bz1, bx1, top, top + bh, 0.15, 'st_brick_worn', 'plaster', out_side=1)
        wall_x(m, bx0, bx1, bz0, top, top + bh, 0.15, 'st_brick_worn', 'plaster', out_side=-1)
        wall_x(m, bx0, bx1, bz1, top, top + bh, 0.15, 'st_brick_worn', 'plaster', [((bx0 + SW0) / 2, 0.9, top, top + 2.1)], out_side=1)
        m.box('roof_tar', (bx0 - 0.1, top + bh, bz0 - 0.1), (bx1 + 0.1, top + bh + 0.15, bz1 + 0.1))

    # ------------------------------------------------------------------ outer walls with openings
    front_open, back_open = [], []           # (cx, w, y0, y1, kind)
    shop_x0, shop_x1 = -hx + 0.55, hx - 0.55
    door_x = hall_x
    if shop:
        if double:
            shops = [(-hx + 0.55, -HALL / 2 - 0.35), (HALL / 2 + 0.35, hx - 0.55)]
        else:
            shops = [(a + HALL + 0.35, hx - 0.55)]
    else:
        shops = []
    for f in range(floors):
        y0, y1 = floor_y(f), floor_y(f + 1)
        if f == 0:
            front_open.append((door_x, 1.1, 0.0, 2.55, 'open' if (enter or burnt) else 'door'))
            for s0, s1 in shops:
                w = s1 - s0
                front_open.append(((s0 + s1) / 2, w, 0.0, 2.85, 'shop'))
            if not shop:
                for cx in bays:
                    if abs(cx - door_x) > 1.0:
                        front_open.append((cx, WIN_W, 1.1, 1.1 + WIN_H, 'win'))
            back_open.append((door_x, 1.0, 0.0, 2.25, 'open' if (enter or burnt) else 'door'))
            for cx in bays:
                if abs(cx - door_x) > 1.0:
                    back_open.append((cx, WIN_W, 1.2, 1.2 + WIN_H * 0.9, 'win'))
        else:
            for cx in bays:
                front_open.append((cx, WIN_W, y0 + SILL, y0 + SILL + WIN_H, 'win'))
                back_open.append((cx, WIN_W, y0 + SILL, y0 + SILL + WIN_H, 'win'))
    side_open = []
    if corner:
        nside = max(2, int(round((depth - 1.0) / 2.2)))
        sb = (depth - 1.0) / nside
        sbays = [-hz + 0.5 + sb * (i + 0.5) for i in range(nside)]
        for f in range(floors):
            y0 = floor_y(f)
            for cz in sbays:
                if f == 0:
                    if cz > 0 and shop:
                        side_open.append((cz, min(1.7, sb - 0.5), 0.55, 3.0, 'shop_side'))
                    continue
                side_open.append((cz, WIN_W, y0 + SILL, y0 + SILL + WIN_H, 'win'))
    para = 1.0 if cornice != 'none' else 0.8
    for f in range(floors):
        y0, y1 = floor_y(f), floor_y(f + 1)
        y1 = y1 + (para if f == floors - 1 else 0.0)
        fo = [(c, w, b, t) for (c, w, b, t, k) in front_open if b < y1 and t > y0 and (b >= y0 - 0.01)]
        bo = [(c, w, b, t) for (c, w, b, t, k) in back_open if b < y1 and t > y0 and (b >= y0 - 0.01)]
        wall_x(m, -hx, hx, hz - T / 2, y0, y1, T, skin, walls_in if enter else 'interior_dark', fo, out_side=1)
        wall_x(m, -hx, hx, -hz + T / 2, y0, y1, T, party if not corner else skin, walls_in if enter else 'interior_dark', bo, out_side=-1)
        so = [(c, w, b, t) for (c, w, b, t, k) in side_open if b < y1 and t > y0 and (b >= y0 - 0.01)]
        for sx in (-1, 1):
            out = skin if corner == ('+x' if sx > 0 else '-x') else party
            ops = so if corner == ('+x' if sx > 0 else '-x') else []
            wall_z(m, -hz + T, hz - T, sx * (hx - T / 2), y0, y1, T, out, walls_in if enter else 'interior_dark', ops, out_side=sx)
    if burnt:
        for f in range(floors):                                  # joist pockets and charred stubs on the walls
            y = floor_y(f + 1) - 0.2
            for k in range(int(rng.integers(2, 6))):
                x = rng.uniform(-hx + T + 0.3, hx - T - 0.3)
                m.push((x, y, rng.choice((-1, 1)) * (hz - T - rng.uniform(0.2, 0.9))), rot_y(rng.uniform(-20, 20)))
                m.box('burnt', (-0.06, -0.12, -0.9), (0.06, 0.12, 0.9))
                m.pop()

    # ------------------------------------------------------------------ the street front
    zf = hz
    # ground floor: cast-iron pilasters at the party walls and between shop and door, an iron cornice over all
    piers = [-hx + 0.25, hx - 0.25]
    for s0, s1 in shops:
        piers += [s0 - 0.15, s1 + 0.15]
    for px in sorted(set(round(p, 3) for p in piers)):
        m.box('iron_black', (px - 0.2, 0.0, zf), (px + 0.2, GROUND - 0.45, zf + 0.12), faces='xXYZ')
        m.box('iron_black', (px - 0.26, 0.0, zf), (px + 0.26, 0.35, zf + 0.18), faces='xXYZ')
    m.box('iron_black', (-hx, GROUND - 0.45, zf), (hx, GROUND - 0.05, zf + 0.22), faces='xXyYZ')       # shop cornice
    m.box('iron_black', (-hx, GROUND - 0.12, zf), (hx, GROUND + 0.02, zf + 0.32), faces='xXyYZ')
    # upper windows: stone sills, lintels in the house's style
    for (c, w, b, t, k) in front_open + ([] if not corner else []):
        if k != 'win' or b < GROUND:
            continue
        m.box(trim, (c - w / 2 - 0.07, b - 0.1, zf), (c + w / 2 + 0.07, b, zf + 0.1), faces='xXyYZ')           # sill
        if lod:
            continue
        if lintel == 'flat':
            m.box(trim, (c - w / 2 - 0.12, t, zf), (c + w / 2 + 0.12, t + 0.28, zf + 0.06), faces='xXyYZ')
        elif lintel == 'hood':
            m.box(trim, (c - w / 2 - 0.12, t, zf), (c + w / 2 + 0.12, t + 0.3, zf + 0.08), faces='xXyYZ')
            m.box(trim, (c - w / 2 - 0.2, t + 0.3, zf), (c + w / 2 + 0.2, t + 0.4, zf + 0.18), faces='xXyYZ')
        else:                                                   # segmental brick arch with a stone keystone
            for k2, (dx, dy) in enumerate(((-0.4, 0.0), (-0.2, 0.07), (0.0, 0.1), (0.2, 0.07), (0.4, 0.0))):
                mat = trim if k2 == 2 else 'st_brick_dark'
                ww = 0.16 if k2 == 2 else 0.22
                m.box(mat, (c + dx * w - ww / 2, t + dy, zf), (c + dx * w + ww / 2, t + dy + (0.3 if k2 == 2 else 0.22), zf + 0.05), faces='xXyYZ')
    if corner:
        sx = 1 if corner == '+x' else -1
        xf = sx * hx
        for (c, w, b, t, k) in side_open:
            if k != 'win':
                continue
            m.box(trim, (xf - (0.1 if sx < 0 else 0), b - 0.1, c - w / 2 - 0.07), (xf + (0.1 if sx > 0 else 0), b, c + w / 2 + 0.07))
            if lod == 0:
                m.box(trim, (xf - (0.06 if sx < 0 else 0), t, c - w / 2 - 0.12), (xf + (0.06 if sx > 0 else 0), t + 0.28, c + w / 2 + 0.12))
        m.box('iron_black', (xf - (0.22 if sx < 0 else 0), GROUND - 0.55, -hz), (xf + (0.22 if sx > 0 else 0), GROUND - 0.05, hz))

    # cornice and parapet
    ytop = top + para
    if not burnt or rng.random() < 0.6:
        if cornice == 'metal':
            m.box('iron_black', (-hx, top - 0.05, zf), (hx, top + 0.5, zf + 0.12), faces='xXyYZ')             # frieze
            if lod == 0:
                for x in np.arange(-hx + 0.25, hx - 0.1, 0.8):
                    m.box('iron_black', (x - 0.07, top, zf), (x + 0.07, top + 0.55, zf + 0.5), faces='xXyZ')  # brackets
                if date:
                    m.box('iron_black', (-0.7, top + 0.05, zf + 0.12), (0.7, top + 0.45, zf + 0.16), faces='xXyYZ')
            m.box('iron_black', (-hx, top + 0.55, zf), (hx, top + 0.72, zf + 0.62), faces='xXyYZ')            # crown
        elif cornice == 'corbel':
            for k in range(3):
                m.box(skin, (-hx, top + 0.1 + k * 0.12, zf), (hx, top + 0.22 + k * 0.12, zf + 0.06 * (k + 1)), faces='xXyYZ')
        m.box(trim, (-hx, ytop - 0.08, -hz), (hx, ytop + 0.05, -hz + T + 0.05))                                  # coping
        m.box(trim, (-hx, ytop - 0.08, hz - T - 0.05), (hx, ytop + 0.05, hz + 0.05))
        if corner:
            sx = 1 if corner == '+x' else -1
            if cornice == 'metal':
                m.box('iron_black', (sx * hx - (0.62 if sx < 0 else 0), top + 0.55, -hz), (sx * hx + (0.62 if sx > 0 else 0), top + 0.72, hz + 0.62))
                m.box('iron_black', (sx * hx - (0.12 if sx < 0 else 0), top - 0.05, -hz), (sx * hx + (0.12 if sx > 0 else 0), top + 0.5, hz))
    # chimneys on the party walls, a water tank on tall houses
    if not burnt:
        for k in range(int(rng.integers(1, 3))):
            x = rng.choice((-1, 1)) * (hx - 0.45)
            z = rng.uniform(-hz + 2.0, hz - 3.0)
            m.box('st_brick_dark', (x - 0.4, top, z - 0.55), (x + 0.4, ytop + rng.uniform(0.8, 1.6), z + 0.55))
        if tank if tank is not None else floors >= 6 and rng.random() < 0.5:
            x, z = rng.uniform(-hx + 2.2, hx - 2.2) if width > 5 else 0.0, rng.uniform(-hz + 3.0, -1.5)
            for sx in (-1, 1):
                for sz in (-1, 1):
                    m.cylinder('iron_black', (x + sx * 1.1, top, z + sz * 1.1), (x + sx * 1.1, top + 2.8, z + sz * 1.1), 0.08, 6)
            m.box('iron_black', (x - 1.5, top + 2.8, z - 1.5), (x + 1.5, top + 2.95, z + 1.5))
            hgt = rng.uniform(3.0, 3.8)
            m.cylinder('wood_dark', (x, top + 2.95, z), (x, top + 2.95 + hgt, z), 1.4, 16 if lod == 0 else 10, caps=True)
            if lod == 0:
                for yy in np.linspace(top + 3.2, top + 2.8 + hgt, 5):
                    m.cylinder('iron_black', (x, yy, z), (x, yy + 0.05, z), 1.42, 16, caps=False)
            m.cylinder('wood_dark', (x, top + 2.95 + hgt, z), (x, top + 3.9 + hgt, z), 1.55, 16 if lod == 0 else 10, caps=False, r2=0.06)

    # ------------------------------------------------------------------ fillings: windows, doors, shops
    srng = np.random.default_rng(seed + 1000)
    if lod == 0:
        for side, opens in (('+z', front_open), ('-z', back_open)):
            zo = hz if side == '+z' else -hz
            for (c, w, b, t, k) in opens:
                m.push((c, 0, zo), rot_y(0 if side == '+z' else 180))
                if k == 'win':
                    if burnt:
                        _frame(m, np.eye(3), 0, 0, b, t, w, rng, 'empty')
                        m.poly('grime', [(-w / 2 - 0.3, t - 0.2, 0.013), (w / 2 + 0.3, t - 0.2, 0.013),
                                         (w / 2 + 0.3, t + rng.uniform(1.0, 2.2), 0.013), (-w / 2 - 0.3, t + 1.5, 0.013)],
                               uvs=[(0.0, -0.5), (0.5, -0.5), (0.5, -1.0), (0.0, -1.0)])
                    else:
                        r = rng.random()
                        kind = 'glass' if r < 0.1 else 'broken' if r < 0.55 else 'boarded' if (b < 3 and r < 0.75) else 'empty'
                        if r > 0.93 and side == '+z':
                            kind = 'boarded'                     # tin sealed by the city
                        _frame(m, np.eye(3), 0, 0, b, t, w, rng, kind)
                        if side == '+z' and rng.random() < 0.12 and b > GROUND:   # a window air conditioner
                            m.box('st_metal_paint', (-0.33, b + 0.02, -0.05), (0.33, b + 0.42, 0.42))
                elif k == 'door':
                    m.box('st_metal_paint' if rng.random() < 0.6 else 'wood_dark', (-w / 2, 0.0, -0.22), (w / 2, t, -0.16))
                elif k == 'open':
                    m.box('wood_dark', (-w / 2 - 0.08, t, -0.22), (w / 2 + 0.08, t + 0.08, -0.12))
                    if side == '+z' and not burnt:                      # the house door, pushed open, transom over it
                        m.push((-w / 2, 0.0, -0.3), rot_y(-100))
                        m.box('wood_dark', (0.0, 0.02, -0.03), (w - 0.05, t - 0.05, 0.03))
                        m.pop()
                elif k == 'shop':
                    if burnt:
                        pass
                    elif srng.random() < 0.55:                          # rolled down steel shutter
                        up = 0.0 if srng.random() < 0.7 else srng.uniform(0.5, 1.2)
                        m.box('st_shutter', (-w / 2, up, -0.2), (w / 2, t, -0.14))
                        m.box('rust_dark', (-w / 2 - 0.05, t - 0.35, -0.18), (w / 2 + 0.05, t, 0.08))
                    else:                                               # shop window, bulkhead, recessed door
                        dw = 1.0
                        dx = w / 2 - dw / 2 - 0.1 if srng.random() < 0.5 else -(w / 2 - dw / 2 - 0.1)
                        for s0, s1 in ((-w / 2, dx - dw / 2), (dx + dw / 2, w / 2)):
                            if s1 - s0 < 0.3:
                                continue
                            m.box('wood_dark', (s0, 0.0, -0.35), (s1, 0.55, -0.2))
                            _frame(m, np.eye(3), (s0 + s1) / 2, -0.15, 0.55, 2.6, s1 - s0, rng, 'broken' if srng.random() < 0.7 else 'empty', frame_mat='wood_dark')
                        m.box('wood_dark', (-w / 2, 2.6, -0.3), (w / 2, t, -0.2))                      # transom panel
                        if not enter:
                            m.box('wood_dark', (dx - dw / 2, 0.0, -0.9), (dx + dw / 2, 2.5, -0.84))    # shut door
                m.pop()
        if corner:
            sx = 1 if corner == '+x' else -1
            for (c, w, b, t, k) in side_open:
                m.push((sx * hx, 0, c), rot_y(90 * sx))
                if k == 'win':
                    r = rng.random()
                    _frame(m, np.eye(3), 0, 0, b, t, w, rng, 'glass' if r < 0.1 else 'broken' if r < 0.55 else 'empty')
                else:
                    m.box('st_shutter', (-w / 2, b, -0.2), (w / 2, t, -0.14))
                m.pop()
    else:
        for (c, w, b, t, k) in front_open:
            if k == 'shop':
                m.box('st_shutter', (c - w / 2, 0.0, zf - 0.2), (c + w / 2, t, zf - 0.14), faces='Z')

    # shop sign over the shop front, a torn awning on some
    for s0, s1 in shops:
        if burnt or lod or srng.random() < 0.25:
            continue
        room = s1 - s0 - 0.2
        fits = [k for k, v in SIGNS.items() if v[3] <= room]
        if fits:
            name = fits[int(srng.integers(len(fits)))]
            m.push(((s0 + s1) / 2, 2.88, zf + 0.02), rot_y(0))
            sign(name, m=m)
            m.pop()
        if srng.random() < 0.35:                                 # awning frame, rags of canvas
            for x in (s0, s1):
                m.cylinder('iron_black', (x, 3.0, zf), (x, 2.55, zf + 1.3), 0.02, 4, caps=False)
            m.cylinder('iron_black', (s0, 2.55, zf + 1.3), (s1, 2.55, zf + 1.3), 0.02, 4, caps=False)
            x = s0
            while x < s1 - 0.3:
                w = min(srng.uniform(0.4, 1.2), s1 - x)
                if srng.random() < 0.55:
                    m.poly('paint_green' if seed % 2 else 'paint_red', [(x, 2.55, zf + 1.3), (x + w, 2.55, zf + 1.3), (x + w, 3.0, zf), (x, 3.0, zf)])
                    m.poly('paint_green' if seed % 2 else 'paint_red', [(x, 3.0, zf), (x + w, 3.0, zf), (x + w, 2.55, zf + 1.3), (x, 2.55, zf + 1.3)])
                x += w

    # the house door: two stone steps onto the pavement, a stone surround with a hood
    if lod == 0:
        m.box(trim, (door_x - 0.85, 0.0, zf), (door_x + 0.85, 0.16, zf + 0.62))
        m.box(trim, (door_x - 0.75, 0.16, zf), (door_x + 0.75, 0.3, zf + 0.32))
        m.box(trim, (door_x - 0.75, 2.55, zf), (door_x + 0.75, 2.85, zf + 0.12), faces='xXyYZ')
    # steel cellar doors in the pavement in front of the shop
    if shops and lod == 0 and rng.random() < 0.7:
        s0, s1 = shops[0]
        cx = s0 + 0.9 if s1 - s0 > 2.2 else (s0 + s1) / 2
        m.box('metal_plate', (cx - 0.75, 0.0, zf + 0.25), (cx + 0.75, 0.06, zf + 1.45))
        m.box('rust_dark', (cx - 0.02, 0.07, zf + 0.25), (cx + 0.02, 0.08, zf + 1.45), faces='Y')

    # ------------------------------------------------------------------ fire escapes
    def iron_rail(x0, x1, z0, z1, y, h=0.95, step=0.25):
        m.cylinder('iron_black', (x0, y + h, z0), (x1, y + h, z1), 0.022, 5, caps=False)
        m.cylinder('iron_black', (x0, y + 0.1, z0), (x1, y + 0.1, z1), 0.015, 4, caps=False)
        n = max(1, int(max(abs(x1 - x0), abs(z1 - z0)) / step))
        for k in range(n + 1):
            t = k / n
            m.box_c('iron_black', (x0 + (x1 - x0) * t, y + h / 2, z0 + (z1 - z0) * t), (0.014, h, 0.014), faces='xXzZ')

    def fire_escape(xa, xb, zf, out, front):
        """Platforms on every floor from the first up, stairs alternating between them, the drop ladder (front),
        a gooseneck ladder over the parapet. out: +1 the facade faces +z, -1 faces -z."""
        D = 1.15 if front else 0.95
        fl = list(range(1, floors))
        if not fl:
            return
        if front:
            m.fire_escapes.append((xa, xb, floor_y(1) + 0.15, zf, D, len(fl)))
        run = min(2.7, xb - xa - 0.8)
        lx = xa + 0.45                                               # the drop ladder
        hatch = front and drop
        for k, f in enumerate(fl):
            y = floor_y(f) + 0.15
            z0, z1 = sorted((zf, zf + out * D))
            if hatch and k == 0:                                     # a hatch in the grating over the let-down ladder
                hz0, hz1 = sorted((zf + out * (D - 0.65), zf + out * D))
                m.box('iron_black', (xa, y - 0.06, z0), (lx - 0.08, y, z1))
                m.box('iron_black', (lx + 0.53, y - 0.06, z0), (xb, y, z1))
                za_, zb_ = (z0, hz0) if out > 0 else (hz1, z1)
                m.box('iron_black', (lx - 0.08, y - 0.06, za_), (lx + 0.53, y, zb_))
            else:
                m.box('iron_black', (xa, y - 0.06, z0), (xb, y, z1))
            if rng.random() < 0.3:
                m.spots.append((rng.uniform(xa + 0.3, xb - 0.3), y, zf + out * D * 0.6, 'balcony'))
            if lod:
                m.box('iron_black', (xa, y + 0.9, zf + out * (D - 0.03)), (xb, y + 0.95, zf + out * D), faces='xXyYZz')
                continue
            if hatch and k == 0:
                iron_rail(xa, lx - 0.1, zf + out * D, zf + out * D, y)
                iron_rail(lx + 0.55, xb, zf + out * D, zf + out * D, y)
            else:
                iron_rail(xa, xb, zf + out * D, zf + out * D, y)
            iron_rail(xa, xa, zf + out * 0.05, zf + out * D, y)
            iron_rail(xb, xb, zf + out * 0.05, zf + out * D, y)
            for x in (xa + 0.25, xb - 0.25):
                m.cylinder('iron_black', (x, y - 0.7, zf), (x, y - 0.06, zf + out * (D - 0.1)), 0.025, 4, caps=False)
            if k + 1 < len(fl):
                y2 = floor_y(fl[k + 1]) + 0.15
                sx0, sx1 = (xb - 0.3, xb - 0.3 - run) if k % 2 == 0 else (xa + 0.3, xa + 0.3 + run)
                for zz in (0.25, 0.85):
                    m.cylinder('iron_black', (sx0, y, zf + out * zz), (sx1, y2, zf + out * zz), 0.03, 4, caps=False)
                m.cylinder('iron_black', (sx0, y + 0.9, zf + out * 0.88), (sx1, y2 + 0.9, zf + out * 0.88), 0.02, 4, caps=False)
                nst = int((y2 - y) / 0.24)
                for j in range(1, nst):
                    t = j / nst
                    x, yy = sx0 + (sx1 - sx0) * t, y + (y2 - y) * t
                    za, zb = sorted((zf + out * 0.25, zf + out * 0.85))
                    m.box('iron_black', (x - 0.1, yy - 0.025, za), (x + 0.1, yy, zb), faces='xXyYzZ')
        if lod == 0:
            zl = zf + out * (D - 0.12)
            if front:                                                  # drop ladder: pulled up, or let down
                y = floor_y(1) + 0.15
                y0 = 0.05 if drop else y - 2.1
                for dx in (0.0, 0.45):
                    m.cylinder('iron_black', (lx + dx, y0, zl), (lx + dx, y + 0.9, zl), 0.02, 4, caps=False)
                for yy in np.arange(y0 + 0.25, y + 0.85, 0.3):
                    m.cylinder('iron_black', (lx, yy, zl), (lx + 0.45, yy, zl), 0.012, 4, caps=False)
                if drop:
                    m.ladders.append((lx + 0.225, 0.0, zl + out * 0.04, y, out))
            y = floor_y(fl[-1]) + 0.15                                # gooseneck up the wall and over the parapet
            gx = (xa + xb) / 2 - 0.225
            zg = zf + out * 0.12
            for dx in (0.0, 0.45):
                m.cylinder('iron_black', (gx + dx, y, zg), (gx + dx, ytop + 0.9, zg), 0.02, 4, caps=False)
                m.cylinder('iron_black', (gx + dx, ytop + 0.9, zg), (gx + dx, ytop + 0.9, zf - out * 0.6), 0.02, 4, caps=False)
            for yy in np.arange(y + 0.3, ytop + 0.8, 0.3):
                m.cylinder('iron_black', (gx, yy, zg), (gx + 0.45, yy, zg), 0.012, 4, caps=False)
            m.ladders.append((gx + 0.225, y, zg + out * 0.04, ytop + 0.95 - y, out))

    if not burnt and floors >= 3:
        if nb >= 4:
            runs = [(1, 2)] if nb < 7 else [(1, 2), (nb - 3, nb - 2)]
        else:
            runs = [(0, 1)]
        for i0, i1 in runs:
            fire_escape(bays[i0] - bay / 2 + 0.12, bays[i1] + bay / 2 - 0.12, zf, 1, True)
        if rng.random() < 0.6:
            i0 = 0 if nb < 4 else nb - 2
            fire_escape(bays[i0] - bay / 2 + 0.12, bays[min(nb - 1, i0 + 1)] + bay / 2 - 0.12, -hz, -1, False)

    # ------------------------------------------------------------------ grime, pipes, seed spots
    if lod == 0:
        skip = [(door_x - 0.8, door_x + 0.8)]
        for s0, s1 in shops:
            skip.append((s0, s1))
        for side, half, frame in (('+z', hx, (0, 0, hz)), ('-z', hx, (0, 0, -hz))):
            m.push(frame, rot_y(0 if side == '+z' else 180))
            x = -half + 0.25
            while x < half - 0.6:
                w = min(rng.uniform(1.4, 3.0), half - 0.25 - x)
                if rng.random() < 0.75:
                    grime(m, x, x + w, ytop - 0.1, ytop - rng.uniform(1.2, 3.6), 0.03, 'roof')
                x += w
            if side == '-z':                                         # the yard side: no plinth decal over the door
                sk = [(-door_x - 0.8, -door_x + 0.8)]
            else:
                sk = skip
            xa_ = -half + 0.25
            for s0, s1 in sorted(sk) + [(half - 0.25, half - 0.25)]:
                if s0 - xa_ > 0.4:
                    grime(m, xa_, s0, 1.0, 0.0, 0.05 if side == '+z' else 0.03, 'plinth')
                xa_ = max(xa_, s1)
            m.pop()
        x = hx - 0.3 if door_x < 0 else -hx + 0.3                    # leader pipe at the party wall
        m.cylinder('rust_dark', (x, 0.2, zf + 0.14), (x, ytop - 0.1, zf + 0.14), 0.05, 8, caps=False)
        m.box('rust_dark', (x - 0.14, ytop - 0.3, zf + 0.02), (x + 0.14, ytop, zf + 0.26))
    for k in range(int(width / 2.5) + 1):
        m.spots.append((rng.uniform(-hx + 0.8, hx - 0.8), top + 0.72, zf + 0.35, 'cornice'))
    if not burnt:
        for k in range(int(width * depth / 25)):
            m.spots.append((rng.uniform(-hx + 1.0, hx - 1.0), top, rng.uniform(-hz + 1.0, hz - 1.0), 'roof'))
    else:
        for k in range(6):
            m.spots.append((rng.uniform(-hx + 1.0, hx - 1.0), 0.5, rng.uniform(-hz + 1.5, hz - 1.5), 'ledge'))
        brng = np.random.default_rng(seed + 9000)                   # the floors that fell in: a heap of rubble
        for k in range(int(width * depth * (1.6 if lod == 0 else 0.4))):
            x, z = brng.uniform(-hx + T + 0.2, hx - T - 0.2), brng.uniform(-hz + T + 0.2, hz - T - 0.2)
            edge = min(x + hx, hx - x, z + hz, hz - z)
            s = brng.uniform(0.12, 0.45)
            hh = min(1.6, 0.3 + edge * 0.3) * brng.uniform(0.4, 1.0)
            m.push((x, hh - s * 0.3, z), rot_y(brng.uniform(0, 360)) @ rot_x(brng.uniform(-35, 35)))
            m.box('debris' if brng.random() < 0.6 else ('burnt' if brng.random() < 0.5 else party), (-s, -s * 0.4, -s * 0.6), (s, s * 0.4, s * 0.6))
            m.pop()
        m.box('debris', (-hx + T, 0.0, -hz + T), (hx - T, 0.35, hz - T))     # the bed of the heap
    m.shops, m.front, m.door_x = [(float(a_), float(b_)) for a_, b_ in shops], float(zf), float(door_x)   # for the level
    return m


def collapse_heap(lod=0, seed=55, width=7.6, depth=18.0, street=16.0):
    """What is left of a walk-up that came down into the street: a mound of brick and plaster filling its lot and
    spilling right across the street (about 6 m high at the lot, 2 m at the far kerb), joists and floor boards
    sticking out, a cornice piece and a fire escape tangle on top. Same frame as walkup(): the lot runs along X
    centred on 0, the street front at z = depth / 2, the street beyond it."""
    rng = np.random.default_rng(seed)
    m = Mesh(PALETTE)
    hx, hz = width / 2, depth / 2
    step = 1.0 if lod == 0 else 2.0
    xs = np.arange(-hx - 6.0, hx + 6.0 + 0.01, step)
    zs = np.arange(-hz, hz + street + 2.0 + 0.01, step)

    def h(x, z):
        if z < hz:                                           # on the lot: between the neighbours' walls
            if abs(x) > hx - 0.1:
                return -0.6
            return 6.2 - 2.2 * ((hz - z) / depth) ** 2 + 0.3 * math.sin(x * 1.7 + z)
        t = (z - hz) / street                                 # out over the street: lower and wider
        spread = hx + 1.0 + 5.0 * min(t, 1.0)
        e = max(0.0, 1.0 - (abs(x) / spread) ** 2)
        return max(-0.4, (6.0 - 4.2 * t) * e ** 0.7 + 0.25 * math.sin(x * 2.3 - z * 1.1)) if t < 1.12 else -0.4
    H = np.array([[h(x, z) for x in xs] for z in zs])
    for j in range(len(zs) - 1):
        for i in range(len(xs) - 1):
            p = [(xs[i], H[j, i], zs[j]), (xs[i], H[j + 1, i], zs[j + 1]), (xs[i + 1], H[j + 1, i + 1], zs[j + 1]),
                 (xs[i + 1], H[j, i + 1], zs[j])]
            if max(v[1] for v in p) < -0.3:
                continue
            m.poly('debris' if (i + j) % 5 else 'broken_wall', p)
    # loose stuff on the surface: bricks, plaster slabs, joists, floor boards
    for k in range(int(260 if lod == 0 else 50)):
        x, z = rng.uniform(xs[0] + 1, xs[-1] - 1), rng.uniform(zs[0] + 1, zs[-1] - 1)
        y = h(x, z)
        if y < 0.1:
            continue
        r = rng.random()
        m.push((x, y, z), rot_y(rng.uniform(0, 360)) @ rot_x(rng.uniform(-30, 30)))
        if r < 0.55:
            s = rng.uniform(0.15, 0.5)
            m.box('st_brick_worn' if rng.random() < 0.6 else 'plaster', (-s, -s * 0.3, -s * 0.6), (s, s * 0.35, s * 0.6))
        elif r < 0.8:
            ln = rng.uniform(2.0, 4.5)
            m.box('wood_dark', (-ln / 2, -0.1, -0.1), (ln / 2, 0.12, 0.1))                     # a joist
        else:
            m.box('wood', (-1.2, -0.02, -0.4), (1.2, 0.03, 0.4))                             # floor boards
        m.pop()
    if lod == 0:                                              # a piece of the cornice, the fire escape tangle
        m.push((0.5, h(0.5, hz + 4.0), hz + 4.0), rot_y(25) @ rot_x(-20))
        m.box('iron_black', (-3.0, -0.2, -0.3), (3.0, 0.4, 0.3))
        m.pop()
        for k in range(6):
            x, z = rng.uniform(-3, 3), rng.uniform(hz - 2, hz + 6)
            m.push((x, h(x, z) + 0.3, z), rot_y(rng.uniform(0, 360)) @ rot_x(rng.uniform(-50, 50)))
            m.box('iron_black', (-1.4, -0.03, -0.5), (1.4, 0.03, 0.5))
            m.cylinder('iron_black', (-1.4, 0.0, 0.5), (1.4, 0.9, 0.5), 0.02, 4, caps=False)
            m.pop()
    return m
