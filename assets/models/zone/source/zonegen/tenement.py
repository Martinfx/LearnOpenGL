"""Pre-war brick and plaster tenements for the overgrown street (original, CC0).

Four to eight storeys of a provincial Soviet town street: shops on a tall ground floor, rows of tall windows
above, a cornice, balconies with rusty railings on some bays, a pitched tin roof or a flat one with a parapet,
rain-water pipes and decades of leaks under every sill and along the roof edge (docs/LEVEL_ART_GUIDE.md).

Frame: the street facade faces +Z at z = depth / 2, the length runs along X centred on 0, y = 0 is the ground
floor (the pavement level). The shell is closed: floors, a spine wall along the middle (so a window shows a dark
room, not the sky through the back windows), no interior partitions. Not enterable: the gateway is shut by a
rusty gate and the shop doors by shutters or boards.
"""
import numpy as np

from .buildings import wall_x, wall_z, slab, railing_x
from .mats import PALETTE
from .mesh import Mesh, rot_y
from .skrunda import _frame, facade_grime, grime
from .fixtures import SIGNS, SIGN_H, sign

GROUND = 4.0        # tall shop floor
STOREY = 3.2        # pre-war storey
T = 0.5             # outer wall
SKINS = ('st_brick_red', 'st_brick_patch', 'st_plaster_brick', 'st_plaster_crack', 'st_paint_peel', 'st_brick_dark',
         'st_brick_slumped', 'st_brick_worn', 'st_plaster_beige')
BRICK = {'st_brick_red', 'st_brick_patch', 'st_brick_dark', 'st_brick_slumped', 'st_brick_worn'}


def _kind(rng, upper):
    """Most windows of a house empty for decades are black holes; some keep a frame with shards, few keep glass."""
    r = rng.random()
    if r < 0.12:
        return 'glass'
    if r < 0.5:
        return 'broken'
    if not upper and r < 0.62:
        return 'boarded'
    return 'empty'


def tenement(length=36.0, floors=5, depth=14.0, seed=0, lod=0, skin='st_brick_red', roof='pitched', gateway=None,
             balconies=0.35, shops=True, ruin=None):
    """ruin=(a, b, keep): the upper floors collapsed between x = a and x = b (fractions of the length, 0..1), only
    floors below `keep` stand there; the break widens by about a bay per floor upwards, so the walls end in steps,
    the floors hang open and the roof is gone over the gap (the second reference: houses gutted by shelling)."""
    rng = np.random.default_rng(seed)
    m = Mesh(PALETTE)
    hx, hz = length / 2, depth / 2
    top = GROUND + (floors - 1) * STOREY
    inn = 'interior_dark'
    trim = 'concrete' if skin in BRICK else 'st_plaster_crack'
    # party walls were never meant to be seen: bare common brick (a patterned or plastered skin repeats on 30 m)
    party = {'st_brick_patch': 'st_brick_red', 'st_brick_dark': 'st_brick_dark'}.get(skin, 'st_brick_worn' if skin not in BRICK else skin)
    nb = max(3, int(round((length - 2.0) / 3.3)))
    bay = (length - 2.0) / nb
    bays = [-hx + 1.0 + bay * (i + 0.5) for i in range(nb)]
    gate_bay = gateway if gateway is not None else (nb // 2 if nb >= 7 and rng.random() < 0.6 else None)
    bal_bays = {i for i in range(nb) if rng.random() < balconies and i != gate_bay}
    if bal_bays and rng.random() < 0.6:          # symmetric pairs read as designed, not random
        bal_bays |= {nb - 1 - i for i in bal_bays}

    def floor_y(f):
        return 0.0 if f == 0 else GROUND + (f - 1) * STOREY

    rrng = np.random.default_rng(seed + 2000)
    if ruin:
        ra0, rb0 = -hx + ruin[0] * length, -hx + ruin[1] * length
        keep = ruin[2]
        wl, wr = [0.0], [0.0]                   # how far the break widens on each side, floor by floor: uneven steps
        for f in range(keep + 1, floors + 1):
            wl.append(wl[-1] + rrng.uniform(0.15, 1.4) * bay)
            wr.append(wr[-1] + rrng.uniform(0.15, 1.4) * bay)

    def gap(f):
        """(a, b) of the collapsed part on floor f (f = floors: the roof), or None where the floor stands."""
        if not ruin or f < keep:
            return None
        return (max(-hx + 1.0, ra0 - wl[f - keep]), min(hx - 1.0, rb0 + wr[f - keep]))

    def segs(f, x0, x1):
        g = gap(f)
        if g is None:
            return [(x0, x1)]
        return [(a, b) for a, b in ((x0, g[0]), (g[1], x1)) if b - a > 0.05]

    def standing(f, c, w=0.0):
        g = gap(f)
        return g is None or c + w / 2 <= g[0] or c - w / 2 >= g[1]

    def scorched(f, c):
        """A window next to the break: the fire that followed burnt it out and blackened the wall above."""
        if not ruin or f < keep - 1:
            return False
        g = gap(max(f, keep))
        return g[0] - 2.2 * bay < c < g[1] + 2.2 * bay

    def soot(x0, x1, y0, y1, z):
        """Soot plume above a burnt window: the roof-drip decal turned upside down (dense at the window)."""
        m.poly('grime', [(x0, y0, z), (x1, y0, z), (x1, y1, z), (x0, y1, z)],
               uvs=[(0.0, -0.5), (0.5, -0.5), (0.5, -1.0), (0.0, -1.0)])

    # ---------------------------------------------------------------- floors, roof slab, spine
    for f in range(floors + 1):
        y = top if f == floors else floor_y(f)
        g = gap(f) if ruin and f > keep else None      # the floor of the lowest broken storey stays, under rubble
        holes = [] if g is None else [(g[0] + rrng.uniform(0.3, 1.5), -hz + T, g[1] - rrng.uniform(0.3, 1.5), hz - T)]
        slab(m, (-hx + T, -hz + T, hx - T, hz - T), holes, y + (0.0 if f else 0.02), 0.25 if f else 0.3,
             'roof_tar' if f == floors else 'flat_floor', inn, inn)
    if ruin:
        for f in range(floors):
            for a, b in segs(f, -hx + T, hx - T):
                wall_x(m, a, b, 0.0, floor_y(f), floor_y(f + 1) if f < floors - 1 else top, 0.25, inn, inn)
    else:
        wall_x(m, -hx + T, hx - T, 0.0, 0.0, top, 0.25, inn, inn)

    # ---------------------------------------------------------------- walls with openings
    win = []                # (side, centre, width, y0, y1, floor, door)
    for f in range(floors):
        y0, y1 = floor_y(f), (floor_y(f + 1) if f < floors - 1 else top + 0.9)
        front, back = [], []
        for i, cx in enumerate(bays):
            if f == 0:
                if i == gate_bay:
                    front.append((cx, 3.0, 0.0, 3.4))
                    continue
                if shops and i % 3 != 1:
                    front.append((cx, min(2.4, bay - 0.7), 0.7, 3.2))
                    win.append(('+z', cx, min(2.4, bay - 0.7), 0.7, 3.2, f, False, 'shop'))
                else:
                    front.append((cx, 1.2, 0.0, 2.5))
                    win.append(('+z', cx, 1.2, 0.0, 2.5, f, True, 'door'))
                back.append((cx, 1.2, 1.2, 2.6))
                win.append(('-z', cx, 1.2, 1.2, 2.6, f, False, ''))
            else:
                ya, yb = y0 + 0.9, y0 + 2.75
                w = min(1.35, bay - 1.1)
                if i in bal_bays and f >= 2:
                    front.append((cx, 1.0, y0 + 0.05, yb))           # balcony door
                    win.append(('+z', cx, 1.0, y0 + 0.05, yb, f, True, 'balcony'))
                else:
                    front.append((cx, w, ya, yb))
                    win.append(('+z', cx, w, ya, yb, f, False, ''))
                back.append((cx, w, ya, yb))
                win.append(('-z', cx, w, ya, yb, f, False, ''))
        for a, b in segs(f, -hx, hx):
            wall_x(m, a, b, hz - T / 2, y0, y1, T, skin, inn, [o for o in front if a < o[0] < b], out_side=1)
            wall_x(m, a, b, -hz + T / 2, y0, y1, T, skin, inn, [o for o in back if a < o[0] < b], out_side=-1)
        g = gap(f)
        if g is not None:                       # broken wall ends: ragged brick stubs at both edges of the gap
            for e, sgn in ((g[0], 1), (g[1], -1)):
                for zc in (hz - T / 2, -hz + T / 2):
                    # brick teeth falling away from the standing wall into the gap: a ragged diagonal break, not a step
                    x, hgt = e, (y1 - y0) * rrng.uniform(0.75, 1.0)
                    for k in range(int(rrng.integers(3, 8))):
                        w = rrng.uniform(0.18, 0.55)
                        hgt -= (y1 - y0) * rrng.uniform(0.05, 0.3)
                        if hgt < 0.1:
                            break
                        m.box(party, (min(x, x + sgn * w), y0, zc - T / 2), (max(x, x + sgn * w), y0 + hgt, zc + T / 2))
                        x += sgn * w
        wall_z(m, -hz + T, hz - T, -hx + T / 2, y0, y1, T, party, inn, out_side=-1)     # party walls: blank brick
        wall_z(m, -hz + T, hz - T, hx - T / 2, y0, y1, T, party, inn, out_side=1)

    # ---------------------------------------------------------------- facade relief (street side)
    zf = hz
    m.box('concrete', (-hx, -0.6, zf), (hx, 0.65, zf + 0.06), faces='xXYZ')                  # plinth
    m.box(trim, (-hx, GROUND - 0.25, zf), (hx, GROUND + 0.05, zf + 0.18), faces='xXyYZ')    # string course
    for a, b in segs(floors, -hx - 0.1, hx + 0.1):
        m.box(trim, (a, top + 0.55, zf), (b, top + 0.9, zf + 0.5), faces='xXyYZ')  # cornice
        m.box(trim, (a + 0.05, top + 0.3, zf), (b - 0.05, top + 0.55, zf + 0.3), faces='xXyZ')
    win = [o for o in win if standing(o[5], o[1], o[2])]
    for (side, c, w, ya, yb, f, door, kind) in win:
        if side != '+z' or f == 0 or lod:
            continue
        m.box(trim, (c - w / 2 - 0.12, yb, zf), (c + w / 2 + 0.12, yb + 0.22, zf + 0.08), faces='xXyYZ')   # lintel
    # balconies: a thin concrete slab on two brackets, rusty railing, some half fallen
    for i in sorted(bal_bays):
        cx = bays[i]
        for f in range(2, floors):
            yf = floor_y(f)
            if rng.random() < 0.12 or not standing(f, cx, 2.6):
                continue                                          # this one has fallen off
            w = min(2.6, bay - 0.3)
            m.box('concrete', (cx - w / 2, yf - 0.16, zf), (cx + w / 2, yf, zf + 1.0))
            if lod == 0:
                for sx in (-1, 1):
                    m.box('concrete', (cx + sx * (w / 2 - 0.25) - 0.08, yf - 0.55, zf), (cx + sx * (w / 2 - 0.25) + 0.08, yf - 0.16, zf + 0.6))
                if rng.random() < 0.85:
                    railing_x(m, cx - w / 2 + 0.05, cx + w / 2 - 0.05, zf + 0.95, yf, 1.0)
                    for sx in (-1, 1):
                        m.cylinder('rust', (cx + sx * (w / 2 - 0.05), yf + 1.0, zf), (cx + sx * (w / 2 - 0.05), yf + 1.0, zf + 0.95), 0.02, 5, caps=False)
                grime(m, cx - w / 2, cx + w / 2, yf - 0.16, yf - rng.uniform(1.2, 2.6), zf + 0.01, 'sill', int(rng.integers(0, 4)))

    # ---------------------------------------------------------------- windows, doors, shop fronts, gate
    if lod == 0:
        for (side, c, w, ya, yb, f, door, kind) in win:
            zo = hz if side == '+z' else -hz
            m.push((c, 0, zo), rot_y(0 if side == '+z' else 180))
            if kind == 'shop':
                r = rng.random()
                if r < 0.45:                                    # roller shutter, rusted, sometimes half up
                    up = 0.0 if rng.random() < 0.6 else rng.uniform(0.4, 1.4)
                    m.box('st_shutter', (-w / 2, ya + up, -0.2), (w / 2, yb, -0.14))
                    m.box('rust_dark', (-w / 2 - 0.05, yb - 0.3, -0.16), (w / 2 + 0.05, yb, 0.05))
                elif r < 0.7:
                    _frame(m, np.eye(3), 0, 0, ya, yb, w, rng, 'boarded')
                else:
                    _frame(m, np.eye(3), 0, 0, ya, yb, w, rng, 'broken' if rng.random() < 0.6 else 'empty')
            elif kind == 'door':
                if rng.random() < 0.5:
                    m.box('st_metal_paint', (-w / 2, 0.0, -0.22), (w / 2, yb, -0.16))
                else:
                    _frame(m, np.eye(3), 0, 0, ya, yb, w, rng, 'boarded', door=False)
            elif scorched(f, c):
                _frame(m, np.eye(3), 0, 0, ya, yb, w, rng, 'empty', door=door)
                m.box('burnt', (-w / 2, ya, -0.5), (w / 2, ya + 0.02, -0.17))           # charred sill board
                soot(-w / 2 - 0.35, w / 2 + 0.35, yb - 0.2, yb + rrng.uniform(1.2, 2.4), 0.013)
            else:
                _frame(m, np.eye(3), 0, 0, ya, yb, w, rng, _kind(rng, f > 0), door=door)
            m.pop()
        if gate_bay is not None:                                 # gateway to the yard, shut by a rusty gate
            cx = bays[gate_bay]
            m.box('st_metal_paint', (cx - 1.5, 0.0, hz - 0.3), (cx + 1.5, 3.4, hz - 0.24))
            m.box('interior_dark', (cx - 1.5, 3.4, hz - 0.5), (cx + 1.5, 3.42, hz))

    # ---------------------------------------------------------------- shop signs over runs of shop fronts
    if shops:
        srng = np.random.default_rng(seed + 1000)      # own stream: the rest of the house stays as it was
        runs, cur = [], []
        for i in range(nb):
            if i % 3 != 1 and i != gate_bay:
                cur.append(i)
            elif cur:
                runs.append(cur)
                cur = []
        if cur:
            runs.append(cur)
        for r in runs:
            if srng.random() > 0.7:
                continue
            room = len(r) * bay - 0.5
            fits = [k for k, v in SIGNS.items() if v[3] <= room]
            if not fits:
                continue
            name = fits[int(srng.integers(len(fits)))]
            cx = (bays[r[0]] + bays[r[-1]]) / 2
            m.push((cx, 3.2, hz), rot_y(0))
            if srng.random() < 0.15:                    # the panel is gone, its rusty box stays
                w = SIGNS[name][3]
                m.box('rust_dark', (-w / 2, 0.0, 0.0), (w / 2, SIGN_H, 0.115), faces='xXyYZ')
            else:
                sign(name, flat=lod > 0, m=m)
            m.pop()

    # ---------------------------------------------------------------- roof
    if roof == 'pitched':
        ridge = top + 0.9 + min(depth * 0.28, 3.6)
        ov = 0.35
        for a, b in segs(floors, -hx - 0.1, hx + 0.1):
            for sz in (1, -1):
                m.poly('rust_sheet', [(a, top + 0.8, sz * (hz + ov)), (b, top + 0.8, sz * (hz + ov)),
                                      (b, ridge, 0.0), (a, ridge, 0.0)][::sz])
                m.poly('interior_dark', [(a, top + 0.8, sz * (hz + ov)), (b, top + 0.8, sz * (hz + ov)),
                                         (b, ridge, 0.0), (a, ridge, 0.0)][::-sz])
        g = gap(floors)
        if g is not None:                                        # bare rafters stick out over the gap, some snapped
            for e, sgn in ((g[0], 1), (g[1], -1)):
                for k in range(int(rrng.integers(2, 5))):
                    x = e + sgn * (0.3 + k * 0.9 + rrng.uniform(-0.1, 0.1))
                    if not (g[0] < x < g[1]):
                        continue
                    for sz in (1, -1):
                        if rrng.random() < 0.35:
                            continue
                        t = rrng.uniform(0.25, 1.0)
                        p0 = (x, top + 0.8, sz * (hz + ov) * 0.9)
                        p1 = (x + rrng.uniform(-0.2, 0.2), top + 0.8 + (ridge - top - 0.8) * t, sz * (hz + ov) * 0.9 * (1 - t))
                        m.cylinder('wood_dark', p0, p1, 0.07, 4)
        for sx in (-1, 1):                                       # gable ends in the wall material
            tri = [(sx * hx, top + 0.8, -hz), (sx * hx, top + 0.8, hz), (sx * hx, ridge - 0.05, 0.0)]
            m.poly(party, tri)
            m.poly(party, tri[::-1])
        if lod == 0:
            for k in range(int(rng.integers(1, 4))):            # chimneys
                x = rng.uniform(-hx + 2, hx - 2)
                if not standing(floors, x, 1.0):
                    continue
                m.box('st_brick_dark', (x - 0.4, ridge - 1.2, -0.5), (x + 0.4, ridge + 1.1, 0.5))
    else:
        for (a, b, c, d) in ((-hx, -hz, -hx + T, hz), (hx - T, -hz, hx, hz)):
            m.box(skin, (a, top + 0.9, b), (c, top + 1.6, d))
        for a, c in segs(floors, -hx, hx):
            m.box(skin, (a, top + 0.9, hz - T), (c, top + 1.6, hz))
            m.box(skin, (a, top + 0.9, -hz), (c, top + 1.6, -hz + T))
            m.box('concrete', (a - 0.05, top + 1.6, hz - T - 0.05), (c + 0.05, top + 1.7, hz + 0.05))

    # ---------------------------------------------------------------- the break: rubble, rebar
    if ruin:
        g = gap(keep)
        y = floor_y(keep)
        for k in range(int((g[1] - g[0]) * (10 if lod == 0 else 3))):
            x = rrng.uniform(g[0] + 0.2, g[1] - 0.2)
            z = rrng.uniform(-hz + T + 0.3, hz - T - 0.3)
            s = rrng.uniform(0.2, 0.7) ** 1.5 * 1.6
            mat = party if rrng.random() < 0.65 else 'concrete'
            m.push((x, y, z), rot_y(rrng.uniform(0, 360)))
            m.box(mat, (-s, 0.0, -s * 0.6), (s, s * rrng.uniform(0.4, 0.9), s * 0.6))
            m.pop()
        # what came down lies in a mound against the facade (and the yard wall), spilling onto the pavement
        for zs in (1, -1):
            n = int((g[1] - g[0]) * (24 if zs > 0 else 9) * (1.0 if lod == 0 else 0.3))
            for k in range(n):
                d = rrng.random() ** 1.6 * 4.2                     # distance from the wall
                x = rrng.uniform(g[0] - 1.0, g[1] + 1.0)
                edge = min(x - g[0] + 1.0, g[1] + 1.0 - x)         # lower toward the ends of the mound
                hgt = max(0.15, (1.0 - d / 4.6) * min(1.0, 0.3 + edge / 2.5) * rrng.uniform(0.6, 1.6))
                s = rrng.uniform(0.12, 0.5)
                mat = party if rrng.random() < 0.72 else ('concrete' if rrng.random() < 0.6 else 'plaster')
                m.push((x, 0.0, zs * (hz + d)), rot_y(rrng.uniform(0, 360)))
                m.box(mat, (-s, -0.2, -s * 0.7), (s, hgt, s * 0.7))
                m.pop()
        if lod == 0:
            for f in range(keep + 1, floors + 1):                # rebar hanging from the torn slab edges
                gf = gap(f)
                yf = top if f == floors else floor_y(f)
                for e, sgn in ((gf[0], 1), (gf[1], -1)):
                    for k in range(int(rrng.integers(3, 7))):
                        z = rrng.uniform(-hz + T + 0.5, hz - T - 0.5)
                        x0 = e + sgn * rrng.uniform(0.3, 1.4)
                        p1 = (x0 + sgn * rrng.uniform(0.2, 0.8), yf - rrng.uniform(0.2, 1.4), z + rrng.uniform(-0.3, 0.3))
                        m.cylinder('rust', (x0, yf - 0.15, z), p1, 0.012, 4, caps=False)

    # ---------------------------------------------------------------- rain: pipes, leaks
    if lod == 0:
        xs = [-hx + 0.35] + [bays[i] + bay / 2 for i in range(3, nb - 1, 4)] + [hx - 0.35]
        for x in xs:
            if rng.random() < 0.15:
                continue                                          # this one fell off long ago
            y_end = 0.15 if rng.random() < 0.8 else rng.uniform(2.0, 6.0)      # broken pipe ends mid-wall
            cut = [f for f in range(floors + 1) if not standing(f, x, 0.3)]
            if cut:                                              # the wall above is gone: the pipe ends torn off
                y_top = floor_y(cut[0]) - 0.4
                if y_top > y_end + 1.0:
                    m.cylinder('rust_dark', (x, y_end, zf + 0.14), (x, y_top, zf + 0.14), 0.055, 8, caps=False)
                continue
            m.cylinder('rust_dark', (x, y_end, zf + 0.14), (x, top + 0.45, zf + 0.14), 0.055, 8, caps=False)
            m.box('rust_dark', (x - 0.15, top + 0.4, zf + 0.02), (x + 0.15, top + 0.75, zf + 0.28))
            for y in np.arange(1.2, top, 2.0):
                if y > y_end:
                    m.box('rust_dark', (x - 0.015, y - 0.02, zf), (x + 0.015, y + 0.02, zf + 0.09))
            if y_end > 1.0:                                       # the wall below a broken pipe is soaked
                grime(m, x - 0.6, x + 0.6, y_end + 0.5, 0.2, zf + 0.012, 'roof')
        for side, half, frame in (('+z', hx, (0, 0, hz)), ('-z', hx, (0, 0, -hz)), ('+x', hz, (hx, 0, 0)), ('-x', hz, (-hx, 0, 0))):
            m.push(frame, rot_y({'+z': 0, '-z': 180, '+x': 90, '-x': -90}[side]))
            if ruin and side in ('+z', '-z'):
                g, sg = gap(floors), (1 if side == '+z' else -1)  # local x of the -z face runs the other way
                lo, hi = sorted((sg * g[0], sg * g[1]))
                for a, b in ((-half, lo), (hi, half)):
                    if b - a > 1.0:
                        facade_grime(m, a, b, top + 0.3 if side == '+z' else top + 0.9, 0.03, rng)
                facade_grime(m, -half, half, -10.0, 0.03, rng, plinth=(0.0, 1.1, 0.07 if side == '+z' else 0.02))
            else:
                facade_grime(m, -half, half, top + 0.3 if side == '+z' else top + 0.9, 0.03, rng,
                             plinth=(0.0, 1.1, 0.07 if side == '+z' else 0.02))
            m.pop()
    return m
