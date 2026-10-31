"""Enterable buildings with real floors, stairs, doors and rooms, plus ruined high-rises for the skyline.

Scale (assets/SCALE.md): storey 3 m, doors 1.0 x 2.1 m, stairs 0.1875 m risers x 0.3 m treads,
ground floor top at y = 0.05. Exterior walls 0.4 m, partitions 0.12 m.
"""
import math
import numpy as np
from .mesh import Mesh, rot_y, rot_x, rot_z
from .mats import PALETTE

STOREY = 3.0
SLAB = 0.22
DOOR_H = 2.1


# --------------------------------------------------------------------------- helpers
def wall_x(m, x0, x1, z, y0, y1, t, out_mat, in_mat, openings=(), out_side=1, skip_ends=False):
    """Wall running along X between x0..x1, centred on z, thickness t.
    openings: (x_center, width, bottom, top) in absolute coords. out_side: +1 outer face is +Z."""
    za, zb = z - t / 2, z + t / 2
    mats = {'Z': out_mat, 'z': in_mat} if out_side > 0 else {'z': out_mat, 'Z': in_mat}
    for k in 'xXyY':
        mats[k] = in_mat
    ops = sorted(openings, key=lambda o: o[0])
    x = x0
    for cx, w, b, tp in ops:
        a, e = cx - w / 2, cx + w / 2
        if a > x:
            m.box(None, (x, y0, za), (a, y1, zb), mats=mats)
        if b > y0:
            m.box(None, (a, y0, za), (e, b, zb), mats=mats)
        if tp < y1:
            m.box(None, (a, tp, za), (e, y1, zb), mats=mats)
        x = e
    if x < x1:
        m.box(None, (x, y0, za), (x1, y1, zb), mats=mats)


def wall_z(m, z0, z1, x, y0, y1, t, out_mat, in_mat, openings=(), out_side=1):
    """Wall running along Z (openings use z centres). out_side +1: outer face is +X."""
    xa, xb = x - t / 2, x + t / 2
    mats = {'X': out_mat, 'x': in_mat} if out_side > 0 else {'x': out_mat, 'X': in_mat}
    for k in 'zZyY':
        mats[k] = in_mat
    ops = sorted(openings, key=lambda o: o[0])
    z = z0
    for cz, w, b, tp in ops:
        a, e = cz - w / 2, cz + w / 2
        if a > z:
            m.box(None, (xa, y0, z), (xb, y1, a), mats=mats)
        if b > y0:
            m.box(None, (xa, y0, a), (xb, b, e), mats=mats)
        if tp < y1:
            m.box(None, (xa, tp, a), (xb, y1, e), mats=mats)
        z = e
    if z < z1:
        m.box(None, (xa, y0, z), (xb, y1, z1), mats=mats)


def slab(m, rect, holes, y_top, thick, floor_mat, ceil_mat, edge_mat):
    """Floor slab over rect=(x0,z0,x1,z1) minus axis-aligned holes, split into boxes."""
    x0, z0, x1, z1 = rect
    xs = sorted({x0, x1, *[h[0] for h in holes], *[h[2] for h in holes]})
    zs = sorted({z0, z1, *[h[1] for h in holes], *[h[3] for h in holes]})
    xs = [x for x in xs if x0 <= x <= x1]
    zs = [z for z in zs if z0 <= z <= z1]
    mats = {'Y': floor_mat, 'y': ceil_mat, 'x': edge_mat, 'X': edge_mat, 'z': edge_mat, 'Z': edge_mat}

    def inside_hole(cx, cz):
        return any(h[0] <= cx <= h[2] and h[1] <= cz <= h[3] for h in holes)

    for j in range(len(zs) - 1):
        # merge consecutive cells of the row
        run = None
        for i in range(len(xs) - 1):
            cx, cz = (xs[i] + xs[i + 1]) / 2, (zs[j] + zs[j + 1]) / 2
            solid = not inside_hole(cx, cz)
            if solid and run is None:
                run = xs[i]
            if (not solid or i == len(xs) - 2) and run is not None:
                end = xs[i + 1] if solid else xs[i]
                m.box(None, (run, y_top - thick, zs[j]), (end, y_top, zs[j + 1]), mats=mats)
                run = None


def stair_flight(m, x0, x1, z_start, z_end, y_start, y_end, mat, under_mat):
    """Straight flight of thin steps from (z_start, y_start) to (z_end, y_end); walks along +-Z."""
    n = max(1, int(round(abs(y_end - y_start) / 0.1875)))
    rise = (y_end - y_start) / n
    run = (z_end - z_start) / n
    for i in range(n):
        top = y_start + rise * (i + 1)
        za = z_start + run * i
        zb = za + run
        lo, hi = min(za, zb), max(za, zb)
        m.box(None, (x0, top - 0.28, lo), (x1, top, hi), mats={'Y': mat, 'y': under_mat, 'x': under_mat,
                                                              'X': under_mat, 'z': mat, 'Z': mat})


def railing_x(m, x0, x1, z, y, h=1.0):
    m.cylinder('rust', (x0, y + h, z), (x1, y + h, z), 0.025, 5, caps=False)
    n = max(1, int((x1 - x0) / 0.15))
    for i in range(0, n + 1, 1):
        x = x0 + (x1 - x0) * i / n
        m.box_c('rust', (x, y + h / 2, z), (0.015, h, 0.015))


def railing_z(m, z0, z1, x, y, h=1.0):
    m.cylinder('rust', (x, y + h, z0), (x, y + h, z1), 0.025, 5, caps=False)
    n = max(1, int(abs(z1 - z0) / 0.15))
    for i in range(n + 1):
        z = z0 + (z1 - z0) * i / n
        m.box_c('rust', (x, y + h / 2, z), (0.015, h, 0.015))


def stairwell(m, floors, wall_mat, lod=0, step_mat='concrete_floor', top_exit=False):
    """Switchback stairwell in local coords: x in [-1.3, 1.3], back wall at z=0.4, flights between
    z=1.9 and z=4.3, mid landing z in [0.6, 1.9], floor landings z in [4.3, ...] are part of the floor slab.
    Returns the slab hole (x0, z0, x1, z1) in local coords."""
    under = 'plaster'
    for f in range(floors - (0 if top_exit else 1)):
        y = 0.05 + f * STOREY
        if lod == 0:
            stair_flight(m, 0.05, 1.3, 4.3, 1.9, y, y + STOREY / 2, step_mat, under)
            stair_flight(m, -1.3, -0.05, 1.9, 4.3, y + STOREY / 2, y + STOREY, step_mat, under)
        else:
            m.oriented_box(step_mat, (0.675, y, 4.3), (0.675, y + STOREY / 2, 1.9), 1.25, 0.2)
            m.oriented_box(step_mat, (-0.675, y + STOREY / 2, 1.9), (-0.675, y + STOREY, 4.3), 1.25, 0.2)
        m.box(None, (-1.3, y + STOREY / 2 - SLAB, 0.4), (1.3, y + STOREY / 2, 1.9),
              mats={'Y': step_mat, 'y': under, 'x': under, 'X': under, 'z': under, 'Z': step_mat})
        if lod == 0:
            # handrail along the inner edge of the upper flight
            m.cylinder('rust', (0.0, y + STOREY / 2 + 0.95, 1.9), (0.0, y + STOREY + 0.95, 4.3), 0.025, 5, caps=False)
            m.box_c('rust', (0.0, y + STOREY / 2 + 0.5, 4.25), (0.03, 1.0 + STOREY / 2, 0.03))
    return (-1.32, 0.4, 1.32, 4.3)


# --------------------------------------------------------------------------- apartment
def apartment(floors=4, sections=2, seed=0, lod=0):
    """Five-storey style brick apartment block (khrushchevka type) with `floors` storeys and
    `sections` entrances. Each section is 13 m wide (X) and 12 m deep (Z); entrances face +Z.
    Each section: stairwell + two flats per floor (rooms, doors, balconies on the back side)."""
    rng = np.random.default_rng(seed)
    m = Mesh(PALETTE)
    SW, D = 13.0, 12.0
    W = SW * sections
    hx, hz = W / 2, D / 2
    t = 0.4
    top = 0.05 + floors * STOREY
    ext, inn = 'brick', 'plaster'
    holes_all = []
    for s in range(sections):
        cx = -hx + SW * (s + 0.5)
        m.push((cx, 0, hz - 6.2))  # stairwell local z=0.4 -> back, front landing towards +Z wall
        stairwell(m, floors, ext, lod)
        m.pop()
        holes_all.append((cx - 1.32, hz - 6.2 + 0.4, cx + 1.32, hz - 6.2 + 4.3))
    # slabs (ground, floors, roof)
    for f in range(floors + 1):
        y = 0.05 + f * STOREY
        holes = holes_all if 0 < f < floors else []
        fm = 'roof_tar' if f == floors else ('concrete_floor' if f == 0 else 'wood_floor')
        slab(m, (-hx + t / 2, -hz + t / 2, hx - t / 2, hz - t / 2), holes, y, SLAB if f else 0.3, fm, 'plaster', inn)
    # exterior walls with windows / doors / balconies
    for f in range(floors):
        y0 = 0.05 + f * STOREY - (SLAB if f else 0.3)
        y1 = 0.05 + (f + 1) * STOREY - SLAB if f < floors - 1 else top + 0.6
        yf = 0.05 + f * STOREY
        front, back, left, right = [], [], [], []
        for s in range(sections):
            cx = -hx + SW * (s + 0.5)
            # stairwell: entrance door on the ground floor, landing window above
            if f == 0:
                front.append((cx, 1.2, yf, yf + DOOR_H + 0.1))
            else:
                front.append((cx, 1.2, yf - 1.2, yf - 0.1))
            for wx in (-4.6, -2.5, 2.5, 4.6):
                front.append((cx + wx, 1.4, yf + 0.85, yf + 2.25))
            for wx in (-4.3, -1.6, 1.6, 4.3):
                if f > 0 and abs(wx) == 4.3:
                    back.append((cx + wx, 0.9, yf, yf + DOOR_H))          # balcony door
                    back.append((cx + wx + (1.0 if wx > 0 else -1.0), 0.9, yf + 0.85, yf + 2.25))
                else:
                    back.append((cx + wx, 1.4, yf + 0.85, yf + 2.25))
        for wz in (-3.0, 3.0):
            left.append((wz, 1.2, yf + 0.85, yf + 2.25))
            right.append((wz, 1.2, yf + 0.85, yf + 2.25))
        wall_x(m, -hx, hx, hz - t / 2, y0, y1, t, ext, inn, front, out_side=1)
        wall_x(m, -hx, hx, -hz + t / 2, y0, y1, t, ext, inn, back, out_side=-1)
        wall_z(m, -hz + t, hz - t, -hx + t / 2, y0, y1, t, ext, inn, left, out_side=-1)
        wall_z(m, -hz + t, hz - t, hx - t / 2, y0, y1, t, ext, inn, right, out_side=1)
        if lod == 0:
            pi = 0.12
            ya, yb = yf, yf + STOREY - SLAB
            for s in range(sections):
                cx = -hx + SW * (s + 0.5)
                # stairwell side walls with flat doors on the landing (z in 4.4..5.6 local -> hz-1.8..hz-0.6)
                zl0 = hz - 6.2 + 0.4
                for sx in (-1, 1):
                    wall_z(m, zl0, hz - t, cx + sx * 1.38, ya, yb, pi, inn, inn,
                           [(hz - 1.2, 1.0, ya, ya + DOOR_H)])
                wall_x(m, cx - 1.44, cx + 1.44, zl0 - pi / 2, ya, yb, pi, inn, inn)
                # flat partitions: front/back rooms split at z = -0.5, door near the stairwell
                for sx in (-1, 1):
                    xa, xb = (cx - SW / 2 + t / 2, cx - 1.44) if sx < 0 else (cx + 1.44, cx + SW / 2 - t / 2)
                    wall_x(m, xa, xb, -0.5, ya, yb, pi, inn, inn,
                           [((xa + xb) / 2 + sx * -1.4, 1.0, ya, ya + DOOR_H)])
                    # kitchen/bathroom wall in the front room
                    xw = cx + sx * 7.8 if abs(sx * 7.8) < SW / 2 else cx + sx * 4.0
                    wall_z(m, -0.5, hz - t, cx + sx * 4.0, ya, yb, pi, inn, inn,
                           [(1.6, 1.0, ya, ya + DOOR_H)])
                # back area behind the stairwell split between both flats
                wall_z(m, -hz + t, -0.5, cx, ya, yb, pi, inn, inn)
                # balconies on the back side
                if f > 0:
                    for wx in (-4.3, 4.3):
                        bx = cx + wx + (0.5 if wx > 0 else -0.5)
                        m.box('concrete', (bx - 1.6, yf - 0.18, -hz - 1.1), (bx + 1.6, yf, -hz))
                        railing_x(m, bx - 1.55, bx + 1.55, -hz - 1.05, yf, 1.0)
                        railing_z(m, -hz - 1.05, -hz, bx - 1.55, yf, 1.0)
                        railing_z(m, -hz - 1.05, -hz, bx + 1.55, yf, 1.0)
                # random junk inside: overturned cupboard / bed frames
                for k in range(2):
                    jx = cx + rng.uniform(-5.5, 5.5)
                    if abs(jx - cx) < 1.6:
                        continue
                    jz = rng.uniform(-5, 0)
                    m.box('wood_dark', (jx - 0.5, ya, jz - 0.3), (jx + 0.5, ya + rng.uniform(0.4, 1.8), jz + 0.3))
    # parapet on the roof and entrance canopies
    for (a, b, c, d) in ((-hx, hz - t, hx, hz), (-hx, -hz, hx, -hz + t), (-hx, -hz, -hx + t, hz), (hx - t, -hz, hx, hz)):
        m.box('concrete', (a, top + 0.6, b), (c, top + 0.75, d))
    for s in range(sections):
        cx = -hx + SW * (s + 0.5)
        m.box('concrete', (cx - 1.4, 2.6, hz), (cx + 1.4, 2.75, hz + 1.4))
        m.box('concrete_floor', (cx - 1.4, -0.3, hz), (cx + 1.4, 0.05, hz + 1.6))
    # roof hatch box over each stairwell
    return m


# --------------------------------------------------------------------------- hospital
def hospital(seed=0, floors=3, lod=0):
    """Three-storey brick hospital, 48 x 15 m, central corridor along X, wards on both sides,
    stairwells at both ends, emergency entrance with canopy on the +Z side, fire escape on -X end."""
    rng = np.random.default_rng(seed)
    m = Mesh(PALETTE)
    L, D = 48.0, 15.0
    hx, hz = L / 2, D / 2
    t = 0.45
    top = 0.05 + floors * STOREY
    ext, inn, inn2 = 'brick_red', 'plaster_green', 'tiles'
    cw = 2.6  # corridor width (z in [-1.3, 1.3])
    holes = []
    # stairwells at both ends: front landing opens into the corridor
    for side in (-1, 1):
        ox = side * (hx + 0.2 - t)
        m.push((ox, 0, 0), rot_y(-90 * side))
        stairwell(m, floors, ext, lod)
        m.pop()
        x_in, x_out = side * (hx - t - 4.3 + 0.2), side * (hx - t - 0.4 + 0.2)
        holes.append((min(x_in, x_out), -1.32, max(x_in, x_out), 1.32))
    for f in range(floors + 1):
        y = 0.05 + f * STOREY
        fm = 'roof_tar' if f == floors else 'tiles_floor'
        slab(m, (-hx + t / 2, -hz + t / 2, hx - t / 2, hz - t / 2), holes if 0 < f < floors else [], y,
             SLAB if f else 0.3, fm, 'plaster', inn)
    room = 4.8
    nrooms = int((L - 2 * 5.0) / room)
    x_rooms0 = -nrooms * room / 2
    for f in range(floors):
        yf = 0.05 + f * STOREY
        y0 = yf - (SLAB if f else 0.3)
        y1 = yf + STOREY - SLAB if f < floors - 1 else top + 0.8
        front, back = [], []
        for i in range(nrooms):
            xc = x_rooms0 + room * (i + 0.5)
            for o in (-1.1, 1.1):
                back.append((xc + o, 1.3, yf + 0.9, yf + 2.4))
                if f == 0 and i == nrooms // 2:
                    continue
                front.append((xc + o, 1.3, yf + 0.9, yf + 2.4))
        if f == 0:  # emergency entrance double door (front, middle)
            front.append((0.0, 2.4, yf, yf + 2.5))
        ends = [(-3.5, 1.2, yf + 0.9, yf + 2.4), (3.5, 1.2, yf + 0.9, yf + 2.4)]
        endw = ends + ([(0.0, 1.0, yf + 1.5 - 1.2, yf + 1.5)] if True else [])
        wall_x(m, -hx, hx, hz - t / 2, y0, y1, t, ext, inn, front, out_side=1)
        wall_x(m, -hx, hx, -hz + t / 2, y0, y1, t, ext, inn, back, out_side=-1)
        fire_door = [(-4.5, 1.0, yf, yf + DOOR_H)] if f > 0 else []
        wall_z(m, -hz + t, hz - t, -hx + t / 2, y0, y1, t, ext, inn, ends + fire_door, out_side=-1)
        wall_z(m, -hz + t, hz - t, hx - t / 2, y0, y1, t, ext, inn, ends, out_side=1)
        if lod == 0:
            ya, yb = yf, yf + STOREY - SLAB
            pi = 0.14
            # corridor walls with ward doors
            xa, xb = -hx + t + 4.6, hx - t - 4.6
            for zc, osd in ((-cw / 2 - pi / 2, 1), (cw / 2 + pi / 2, -1)):
                ops = []
                for i in range(nrooms):
                    xc = x_rooms0 + room * (i + 0.5)
                    if f == 0 and zc > 0 and i == nrooms // 2:
                        ops.append((xc, room - 0.4, ya, ya + 2.6))   # reception hall opens to the corridor
                        continue
                    ops.append((xc + 1.4, 1.2, ya, ya + DOOR_H))
                wall_x(m, xa, xb, zc, ya, yb, pi, inn2, inn, ops, out_side=osd)
            if f == 0:
                m.box('wood_dark', (-2.2, ya, 2.4), (-1.0, ya + 1.1, 4.6))   # reception counter
            # ward partitions
            for i in range(nrooms + 1):
                xw = x_rooms0 + room * i
                for z0, z1 in ((cw / 2 + pi, hz - t), (-hz + t, -cw / 2 - pi)):
                    wall_z(m, z0, z1, xw, ya, yb, pi, inn, inn)
            # stairwell end zones: walls closing the stair core sides to the corridor ends
            for side in (-1, 1):
                xw = side * (hx - t - 4.6)
                for z0, z1 in ((cw / 2 + pi, hz - t), (-hz + t, -cw / 2 - pi)):
                    wall_z(m, z0, z1, xw, ya, yb, pi, inn, inn, [((z0 + z1) / 2, 1.0, ya, ya + DOOR_H)])
                for zc in (-1.36 - 0.06, 1.36 + 0.06):
                    wall_x(m, min(xw, side * (hx - t)), max(xw, side * (hx - t)), zc, ya, yb, 0.12, inn, inn)
            # beds and junk in the wards
            for i in range(nrooms):
                xc = x_rooms0 + room * (i + 0.5)
                for zs in (1, -1):
                    if f == 0 and zs > 0 and i == nrooms // 2:
                        continue
                    for b in range(2):
                        if rng.random() < 0.35:
                            continue
                        bx = xc - 1.5 + b * 2.4 + rng.uniform(-0.2, 0.2)
                        bz = zs * (hz - t - 1.3)
                        rot = rng.uniform(-25, 25)
                        m.push((bx, ya, bz), rot_y(rot))
                        m.box('rust', (-0.45, 0.45, -1.0), (0.45, 0.55, 1.0))
                        m.box('paint_cream', (-0.42, 0.55, -0.95), (0.42, 0.65, 0.95))
                        for lx in (-0.42, 0.42):
                            for lz in (-0.95, 0.95):
                                m.box_c('rust', (lx, 0.25, lz), (0.04, 0.5, 0.04))
                        m.box('rust', (-0.45, 0.55, -1.0), (0.45, 1.05, -0.96))
                        m.pop()
    # roof parapet, entrance canopy + ramp, sign
    for (a, b, c, d) in ((-hx, hz - t, hx, hz), (-hx, -hz, hx, -hz + t), (-hx, -hz, -hx + t, hz), (hx - t, -hz, hx, hz)):
        m.box('concrete', (a, top + 0.8, b), (c, top + 0.95, d))
    m.box('concrete', (-4.0, 3.0, hz), (4.0, 3.25, hz + 5.0))
    for x in (-3.7, 3.7):
        m.box_c('concrete', (x, 1.5, hz + 4.7), (0.3, 3.0, 0.3))
    m.box('concrete_floor', (-4.0, -0.3, hz), (4.0, 0.05, hz + 5.0))
    # sign above the entrance (atlas row 2)
    from .structures import _sign
    m.box('concrete', (-4.5, 3.25, hz + 4.85), (4.5, 4.35, hz + 5.0))
    _sign(m, -4.5, 4.5, 3.25, 4.35, hz + 5.01, 2)
    # fire escape on the -X end: landings at each floor and steep flights
    if lod == 0:
        fx = -hx - 1.4
        for f in range(1, floors):
            yf = 0.05 + f * STOREY
            m.box('metal_plate', (fx - 1.0, yf - 0.08, -6.0), (fx + 1.3, yf, -3.4))
            railing_z(m, -6.0, -3.4, fx - 1.0, yf, 1.0)
        for f in range(floors - 1):
            ya = 0.05 + f * STOREY
            stair_flight(m, fx - 0.95, fx + 0.1, -0.6 if f == 0 else -0.6, -3.4, ya, ya + STOREY, 'metal_plate', 'rust')
        for z in (-6.0, -3.4):
            m.box_c('rust', (fx - 0.95, top / 2, z), (0.12, top, 0.12))
    return m


# --------------------------------------------------------------------------- industrial hall
def hall(length=48.0, width=21.0, height=15.0, kind='turbine', seed=0, lod=0):
    """Industrial hall along X. Brick walls with ribbon windows, two big gates (X ends), side doors,
    steel roof trusses with a partly collapsed corrugated roof, overhead crane, mezzanine with offices
    reached by a straight stair. kind: 'turbine' (two turbine casings) or 'workshop' (machines, shelves)."""
    rng = np.random.default_rng(seed)
    m = Mesh(PALETTE)
    hx, hz = length / 2, width / 2
    t = 0.5
    ext, inn = 'brick', 'brick'
    gate_w, gate_h = 6.0, 6.5
    bay = 6.0
    nb = int(length / bay)
    # floor
    m.box(None, (-hx, -0.3, -hz), (hx, 0.05, hz), mats={k: 'concrete_floor' for k in 'xXyYzZ'})
    # long walls: two rows of windows (low and high ribbon), side doors
    for side in (-1, 1):
        ops = []
        for i in range(nb):
            xc = -hx + bay * (i + 0.5)
            ops.append((xc, 3.6, 2.0, 4.6))
            ops.append((xc, 4.4, height - 6.0, height - 2.0))
        ops = [o for o in ops if not (side > 0 and abs(o[0] - (-hx + bay * 1.5)) < 0.1 and o[2] < 3)]
        if side > 0:
            ops.append((-hx + bay * 1.5, 2.0, 0.05, 2.6))
        else:
            ops.append((hx - bay * 1.5, 2.0, 0.05, 2.6))
            ops = [o for o in ops if not (abs(o[0] - (hx - bay * 1.5)) < 0.1 and o[1] == 3.6)]
        wall_x(m, -hx, hx, side * (hz - t / 2), -0.3, height, t, ext, 'plaster', ops, out_side=side)
    for side in (-1, 1):
        ops = [(0.0, gate_w, 0.05, gate_h), (-6.0, 2.4, height - 6.5, height - 2.5), (6.0, 2.4, height - 6.5, height - 2.5)]
        wall_z(m, -hz + t, hz - t, side * (hx - t / 2), -0.3, height + 2.0, t, ext, 'plaster', ops, out_side=side)
        # gable top
    # pilasters/columns with crane rail brackets
    for i in range(nb + 1):
        x = -hx + bay * i
        for side in (-1, 1):
            z = side * (hz - t - 0.3)
            m.box_c('concrete', (x, height / 2, z), (0.6, height, 0.6))
            if lod == 0:
                m.box_c('concrete', (x, height - 4.0, z - side * 0.6), (0.6, 0.8, 0.8))
    # crane rails + gantry crane
    for side in (-1, 1):
        z = side * (hz - t - 0.9)
        m.box('rust_dark', (-hx + t, height - 3.6, z - 0.25), (hx - t, height - 3.2, z + 0.25))
    cx = rng.uniform(-hx / 3, hx / 3)
    m.box('paint_cream', (cx - 0.6, height - 3.2, -hz + t + 0.5), (cx + 0.6, height - 2.2, hz - t - 0.5))
    if lod == 0:
        m.cylinder('rust', (cx, height - 3.2, 1.0), (cx, 2.5, 1.0), 0.03, 4, caps=False)
        m.box('rust_dark', (cx - 0.3, 2.0, 0.7), (cx + 0.3, 2.5, 1.3))
    # roof: trusses each bay + corrugated sheets with holes
    ridge = height + 3.0
    for i in range(nb + 1):
        x = -hx + bay * i
        for side in (-1, 1):
            m.oriented_box('rust', (x, height, side * hz), (x, ridge, 0), 0.25, 0.4)
        m.oriented_box('rust', (x, height, -hz), (x, height, hz), 0.2, 0.25)
        if lod == 0:
            for k in range(1, 6):
                zz = -hz + width * k / 6
                yy = height + (ridge - height) * (1 - abs(zz) / hz)
                m.oriented_box('rust', (x, height, zz), (x, yy, zz), 0.1, 0.1)
    for i in range(nb):
        x0, x1 = -hx + bay * i, -hx + bay * (i + 1)
        for side in (-1, 1):
            if rng.random() < 0.3:      # collapsed sheet: light shaft
                continue
            a, b = (side * hz + side * 0.3, height - 0.1), (0.0, ridge + 0.05)
            if side > 0:
                pts = [(x0, a[1], a[0]), (x1, a[1], a[0]), (x1, b[1], b[0]), (x0, b[1], b[0])]
            else:
                pts = [(x1, a[1], a[0]), (x0, a[1], a[0]), (x0, b[1], b[0]), (x1, b[1], b[0])]
            m.poly('corrugated', pts)
            m.poly('rust_dark', [(p[0], p[1] - 0.05, p[2]) for p in pts[::-1]])
    # gable walls (triangles above the end walls)
    for side in (-1, 1):
        x = side * hx
        tri = [(x, height + 2.0 - 2.0, -hz), (x, height + 2.0 - 2.0, hz), (x, ridge, 0)]
        m.poly(ext, tri if side > 0 else tri[::-1])
        m.poly('plaster', [(x - side * t, p[1], p[2]) for p in (tri[::-1] if side > 0 else tri)])
    # mezzanine along -Z wall with offices
    my = 4.5
    mz0, mz1 = -hz + t, -hz + t + 4.0
    mx0, mx1 = -hx + t + bay, hx - t - 8.0
    m.box(None, (mx0, my - 0.3, mz0), (mx1, my, mz1), mats={'Y': 'metal_plate', 'y': 'rust_dark', 'x': 'rust', 'X': 'rust', 'z': 'rust', 'Z': 'rust'})
    for x in np.arange(mx0 + 0.2, mx1, 6.0):
        m.box_c('rust', (x, my / 2, mz1 - 0.2), (0.3, my, 0.3))
    if lod == 0:
        railing_x(m, mx0, mx1 - 1.3, mz1 - 0.05, my, 1.0)
        # straight stair up to the mezzanine at the +X end, along -X
        n = 24
        for i in range(n):
            top = 0.05 + (my - 0.05) * (i + 1) / n
            x_hi = mx1 + 7.2 - 0.3 * i
            m.box(None, (x_hi - 0.3, top - 0.12, mz1 - 1.2), (x_hi, top, mz1), mats={k: 'metal_plate' for k in 'xXyYzZ'})
        m.box('metal_plate', (mx1, my - 0.3, mz1 - 1.2), (mx1 + 0.3, my, mz1))
        # offices on the mezzanine: glazed partition (frames only) with doors
        ya, yb = my, my + 3.0
        wall_x(m, mx0, mx1, mz1 - 1.6, ya, ya + 1.0, 0.12, 'plaster_blue', 'plaster_blue',
               [(mx0 + 3 + k * 6, 1.0, ya, ya + 1.0) for k in range(int((mx1 - mx0) / 6))])
        for x in np.arange(mx0, mx1 + 0.01, 2.0):
            m.box_c('rust', (x, ya + 2.0, mz1 - 1.6), (0.08, 2.0, 0.08))
        m.box('rust', (mx0, yb - 0.1, mz1 - 1.66), (mx1, yb, mz1 - 1.54))
        for x in np.arange(mx0, mx1 + 0.01, 6.0):
            wall_z(m, mz0, mz1 - 1.66, x, ya, yb, 0.12, 'plaster_blue', 'plaster_blue')
        m.box(None, (mx0, yb, mz0), (mx1, yb + 0.15, mz1 - 1.54), mats={k: 'plaster' for k in 'xXyYzZ'})
        for k in range(int((mx1 - mx0) / 6)):
            ox = mx0 + 1.5 + k * 6
            m.box('metal_plate', (ox, ya, mz0 + 0.3), (ox + 1.6, ya + 0.75, mz0 + 1.1))   # desk
    # contents
    if kind == 'turbine':
        for xc in (-hx / 2 + 2, hx / 2 - 6):
            m.box('concrete', (xc - 6, 0.05, -2.2), (xc + 6, 1.6, 3.2))
            m.cylinder('paint_green', (xc - 5.5, 3.3, 0.5), (xc + 4.0, 3.3, 0.5), 1.7, 20 if lod == 0 else 10)
            m.cylinder('rust_dark', (xc + 4.0, 3.3, 0.5), (xc + 5.6, 3.3, 0.5), 1.2, 16 if lod == 0 else 8)
            if lod == 0:
                for k in range(3):
                    px = xc - 4 + k * 3
                    m.cylinder('rust', (px, 4.9, 0.5), (px, 4.9, -hz + t + 4.5), 0.35, 10)
                    m.cylinder('rust', (px, 1.6, -hz + t + 4.5), (px, 4.9, -hz + t + 4.5), 0.35, 10)
    else:
        for i in range(6):
            xm = -hx + 5 + i * (length - 10) / 5 + rng.uniform(-1, 1)
            zm = rng.uniform(-1.5, 3.5)
            m.push((xm, 0.05, zm), rot_y(rng.uniform(-20, 20)))
            m.box('paint_green', (-1.2, 0, -0.6), (1.2, 1.1, 0.6))
            m.box('rust_dark', (-0.5, 1.1, -0.4), (0.6, 1.9, 0.4))
            m.pop()
        if lod == 0:
            for i in range(4):
                sx = -hx + 3 + i * 3.2
                m.box('rust', (sx, 0.05, hz - t - 0.7), (sx + 2.6, 0.08, hz - t - 0.1))
                for lvl in (0.6, 1.2, 1.8):
                    m.box('rust', (sx, lvl, hz - t - 0.7), (sx + 2.6, lvl + 0.04, hz - t - 0.1))
                for lx in (sx, sx + 2.55):
                    m.box('rust', (lx, 0.05, hz - t - 0.7), (lx + 0.05, 2.2, hz - t - 0.65))
                    m.box('rust', (lx, 0.05, hz - t - 0.15), (lx + 0.05, 2.2, hz - t - 0.1))
    return m


# --------------------------------------------------------------------------- skyline tower
def ruined_tower(floors=20, seed=0, w=24.0, d=18.0, lod=0):
    """Panel high-rise shell for the city skyline: floor slabs, columns, facade panels with random
    gaps; the top storeys are partly collapsed (missing slabs and a broken corner). Ground floor open."""
    rng = np.random.default_rng(seed)
    m = Mesh(PALETTE)
    hx, hz = w / 2, d / 2
    H = floors * STOREY
    # collapse profile: corner removed above `cut` storey, decreasing height toward one side
    cut = int(floors * rng.uniform(0.6, 0.85))
    corner = (rng.choice([-1, 1]), rng.choice([-1, 1]))

    def alive(f, x, z):
        if f < cut:
            return True
        k = (f - cut + 1) / (floors - cut + 1)
        return not ((x * corner[0] > hx * (1 - 1.6 * k)) and (z * corner[1] > -hz * 0.5 + hz * (1 - k)))

    nx, nz = int(w / 3), int(d / 3)
    for f in range(floors + 1):
        y = f * STOREY
        if lod == 0 or f % 1 == 0:
            for i in range(nx):
                for j in range(nz):
                    x0, z0 = -hx + 3 * i, -hz + 3 * j
                    if not alive(f, x0 + 1.5, z0 + 1.5):
                        continue
                    pass
        # slab rows merged per row of cells
        for j in range(nz):
            run = None
            for i in range(nx + 1):
                ok = i < nx and alive(f, -hx + 3 * i + 1.5, -hz + 3 * j + 1.5)
                if ok and run is None:
                    run = i
                if not ok and run is not None:
                    m.box(None, (-hx + 3 * run, y - 0.25, -hz + 3 * j), (-hx + 3 * i, y, -hz + 3 * (j + 1)),
                          mats={'Y': 'concrete_dark', 'y': 'interior_dark', 'x': 'concrete', 'X': 'concrete', 'z': 'concrete', 'Z': 'concrete'})
                    run = None
    if lod == 0:
        for i in range(0, nx + 1, 2):
            for j in range(0, nz + 1, 2):
                top_f = max(f for f in range(floors + 1) if alive(f, -hx + 3 * min(i, nx - 1) + 1.5, -hz + 3 * min(j, nz - 1) + 1.5))
                m.box_c('interior_dark', (-hx + 3 * i, top_f * STOREY / 2, -hz + 3 * j), (0.4, top_f * STOREY, 0.4))
    # facade panels (3 x 3 m quads, both sides), ground floor left open
    for f in range(1, floors):
        y0, y1 = f * STOREY, (f + 1) * STOREY - 0.25
        for side, (ax, n) in enumerate((('x', nx), ('x', nx), ('z', nz), ('z', nz))):
            sgn = 1 if side % 2 == 0 else -1
            for i in range(n):
                if ax == 'x':
                    xa, xb = -hx + 3 * i, -hx + 3 * (i + 1)
                    z = sgn * hz
                    if not alive(f + 1, (xa + xb) / 2, z - sgn * 1.5) or rng.random() < 0.08 + 0.25 * (f >= cut):
                        continue
                    pts = [(xa, y0, z), (xb, y0, z), (xb, y1, z), (xa, y1, z)]
                else:
                    za, zb = -hz + 3 * i, -hz + 3 * (i + 1)
                    x = sgn * hx
                    if not alive(f + 1, x - sgn * 1.5, (za + zb) / 2) or rng.random() < 0.08 + 0.25 * (f >= cut):
                        continue
                    pts = [(x, y0, zb), (x, y0, za), (x, y1, za), (x, y1, zb)]
                if sgn < 0:
                    pts = pts[::-1] if ax == 'x' else pts[::-1]
                if ax == 'x' and sgn < 0:
                    pass
                m.poly('facade', pts)
                if lod == 0:
                    m.poly('interior_dark', pts[::-1])
    return m
