"""Rusted wrecks: generic boxy Soviet-era sedan / estate / van, small bus, long bus,
passenger carriage, steam locomotive + tender. All original low-poly designs.
Cars face +Z, centred on X/Z, wheels on y = 0 (or sunk when flat)."""
import math
import numpy as np
from .mesh import Mesh, rot_y, rot_x, rot_z
from .mats import PALETTE


def _wheel(m, x, y, z, r, w, seg, flat=False, rim_only=False):
    """Wheel axis along X. flat: tyre gone, rim on the ground."""
    if rim_only:
        m.cylinder('rust_dark', (x - w * 0.3, y, z), (x + w * 0.3, y, z), r * 0.62, seg)
        return
    m.cylinder('rubber', (x - w / 2, y, z), (x + w / 2, y, z), r, seg)
    side = 1 if x > 0 else -1
    m.cylinder('rust_dark', (x + side * w / 2, y, z), (x + side * (w / 2 + 0.01), y, z), r * 0.6, seg, caps=True)


def car(kind='sedan', paint='paint_green', seed=0, lod=0, burnt=False, taxi=False):
    """kind: sedan | estate | van. ~4.1 x 1.6 x 1.45 m. taxi: a roof sign and a black-and-white check band."""
    rng = np.random.default_rng(seed)
    m = Mesh(PALETTE)
    body = 'burnt' if burnt else paint
    seg = 10 if lod == 0 else 6
    L, W = (4.1, 1.62) if kind != 'van' else (4.4, 1.9)
    sink = rng.uniform(0.0, 0.12)  # flat tyres
    m.push((0, -sink, 0))
    y0, y1 = 0.32, 0.92   # lower body
    hz = L / 2
    # lower body split around wheel arches: centre, front, rear; arches leave a gap above the wheels
    wr = 0.3
    zf, zr = hz - 0.78, -hz + 0.78
    m.box(body, (-W / 2, y0 + 0.18, -hz), (W / 2, y1, hz))                     # upper skirt over arches
    m.box(body, (-W / 2, y0, zr + 0.42), (W / 2, y0 + 0.18, zf - 0.42), faces='xXyzZ')  # sill between wheels
    m.box(body, (-W / 2, y0, zf + 0.42), (W / 2, y0 + 0.18, hz), faces='xXyZ')       # front valance
    m.box(body, (-W / 2, y0, -hz), (W / 2, y0 + 0.18, zr - 0.42), faces='xXyz')      # rear valance
    # bumpers
    m.box('rust_dark', (-W / 2 - 0.02, 0.38, hz), (W / 2 + 0.02, 0.5, hz + 0.08))
    m.box('rust_dark', (-W / 2 - 0.02, 0.38, -hz - 0.08), (W / 2 + 0.02, 0.5, -hz))
    if lod == 0:
        # grille and headlights
        m.box('rust_dark', (-0.5, 0.58, hz), (0.5, 0.82, hz + 0.02))
        for s in (-1, 1):
            m.cylinder('rust_dark', (s * 0.6, 0.7, hz), (s * 0.6, 0.7, hz + 0.03), 0.09, 8)
    # wheels
    for z in (zf, zr):
        for s in (-1, 1):
            missing = rng.random() < 0.15
            _wheel(m, s * (W / 2 - 0.12), wr - 0.0 + sink * 0.5, z, wr, 0.2, seg, rim_only=missing)
    # cabin
    if kind == 'van':
        cab0, cab1, roof = -hz + 0.05, hz - 0.55, 1.95
        m.box(body, (-W / 2, y1, cab0), (W / 2, 1.3, cab1))                  # walls below windows
        m.box(body, (-W / 2, y1, cab1), (W / 2, 1.05, hz), faces='xXYZ')    # hood
        pillars = [cab0, cab0 + 1.2, cab1 - 1.4, cab1 - 0.9, cab1]
    else:
        cab0 = -0.95 if kind == 'sedan' else -hz + 0.12
        cab1, roof = 0.75, 1.42
        pillars = [cab0, (cab0 + cab1) / 2 - 0.05, cab1]
        if kind == 'sedan':
            m.box(body, (-W / 2, y1, -hz), (W / 2, y1 + 0.06, cab0), faces='xXYz')  # trunk lid
    m.box(body, (-W / 2 + 0.05, roof - 0.06, cab0 + (0.18 if kind != 'van' else 0)), (W / 2 - 0.05, roof, cab1 - (0.3 if kind != 'van' else 0)))
    # pillars (front and rear raked)
    ybase = 1.3 if kind == 'van' else y1
    for i, z in enumerate(pillars):
        for s in (-1, 1):
            x = s * (W / 2 - 0.06)
            if i == len(pillars) - 1 and kind != 'van':
                m.oriented_box(body, (x, ybase, z), (x, roof - 0.03, z - 0.3), 0.08, 0.08)
            elif i == 0 and kind == 'sedan':
                m.oriented_box(body, (x, ybase, z), (x, roof - 0.03, z + 0.18), 0.08, 0.08)
            else:
                m.box_c(body, (x, (ybase + roof) / 2, z), (0.08, roof - ybase, 0.08))
    if taxi:
        # Roof sign (its plastic long gone grey) and a check band along the doors.
        m.box_c('rust_dark', (0, roof + 0.02, (cab0 + cab1) / 2 + 0.1), (0.5, 0.04, 0.2))
        m.box_c('paint_cream', (0, roof + 0.13, (cab0 + cab1) / 2 + 0.1), (0.62, 0.2, 0.16))
        if lod == 0:
            for k in range(12):
                z = -1.3 + k * 0.22
                for s in (-1, 1):
                    m.box_c('iron_black' if k % 2 else 'paint_cream', (s * (W / 2 + 0.004), 0.74, z), (0.01, 0.08, 0.22), faces='xX')
    if lod == 0:
        # interior: floor, seats, dashboard
        m.box('rust_dark', (-W / 2 + 0.05, y0 + 0.05, cab0), (W / 2 - 0.05, y0 + 0.1, cab1), faces='Y')
        rows = [0.15, -0.65] if kind != 'van' else [cab1 - 0.9]
        for z in rows:
            for s in ((-1, 1) if z > 0 or kind == 'van' else (0,)):
                sw = 0.55 if s else W - 0.25
                if burnt:
                    m.box('rust_dark', (s * 0.38 - sw / 2, 0.5, z - 0.25), (s * 0.38 + sw / 2, 0.56, z + 0.25))
                    m.oriented_box('rust_dark', (s * 0.38, 0.55, z - 0.27), (s * 0.38, 1.15, z - 0.38), sw, 0.05)
                else:
                    m.box('wood_dark', (s * 0.38 - sw / 2, 0.45, z - 0.25), (s * 0.38 + sw / 2, 0.62, z + 0.25))
                    m.oriented_box('wood_dark', (s * 0.38, 0.6, z - 0.27), (s * 0.38, 1.2, z - 0.4), sw, 0.12)
        m.box('rust_dark', (-W / 2 + 0.05, y1 - 0.15, cab1 - 0.25), (W / 2 - 0.05, y1 + 0.05, cab1 - 0.05))
        if kind == 'van':  # cargo partition
            m.box(body, (-W / 2, ybase, cab1 - 1.25), (W / 2, roof - 0.06, cab1 - 1.2))
    m.pop()
    return m


def bus(length=7.0, paint='paint_cream', seed=0, lod=0, enterable=True):
    """Small (PAZ-like 7 m) or long (10.5 m) city bus. Door on the right (+X) side."""
    rng = np.random.default_rng(seed)
    m = Mesh(PALETTE)
    W, H = 2.45, 2.95
    hz = length / 2
    floor = 0.75
    sink = rng.uniform(0.0, 0.15)
    m.push((0, -sink, 0))
    seg = 12 if lod == 0 else 6
    # wheels
    for z in (hz - 1.5, -hz + 1.9):
        for s in (-1, 1):
            _wheel(m, s * (W / 2 - 0.2), 0.48, z, 0.48, 0.3, seg, rim_only=rng.random() < 0.2)
    # chassis / floor slab (walkable)
    m.box('rust_dark', (-W / 2 + 0.05, floor - 0.12, -hz), (W / 2 - 0.05, floor, hz))
    # skirt below windows: y 0.45 .. 1.55, with door gaps on the right side
    belt = 1.55
    doors = [hz - 0.95, 0.0] if length > 8 else [hz - 0.95, -hz + 1.0]
    dw = 1.1
    # left side full
    m.box(paint, (-W / 2, 0.45, -hz), (-W / 2 + 0.06, belt, hz))
    # right side with door gaps
    cuts = sorted([(d - dw / 2, d + dw / 2) for d in doors])
    z = -hz
    for a, b in cuts:
        m.box(paint, (W / 2 - 0.06, 0.45, z), (W / 2, belt, a))
        z = b
    m.box(paint, (W / 2 - 0.06, 0.45, z), (W / 2, belt, hz))
    # front and rear walls
    m.box(paint, (-W / 2, 0.45, hz - 0.06), (W / 2, belt, hz))
    m.box(paint, (-W / 2, 0.45, -hz), (W / 2, H - 0.3, -hz + 0.06))
    m.box('rust_dark', (-W / 2, 0.4, hz), (W / 2, 0.55, hz + 0.1))
    m.box('rust_dark', (-W / 2, 0.4, -hz - 0.1), (W / 2, 0.55, -hz))
    # window band: pillars every ~1.2 m both sides, empty windows
    npil = int(length / 1.15)
    for i in range(npil + 1):
        zz = -hz + 0.03 + (length - 0.06) * i / npil
        for s in (-1, 1):
            if s > 0 and any(abs(zz - d) < dw / 2 for d in doors):
                continue
            m.box_c(paint, (s * (W / 2 - 0.03), (belt + H - 0.3) / 2, zz), (0.06, H - 0.3 - belt, 0.09))
    # door frames reach the floor
    for d in doors:
        for e in (-1, 1):
            m.box_c(paint, (W / 2 - 0.03, (0.45 + H - 0.3) / 2, d + e * dw / 2), (0.06, H - 0.75, 0.09))
        # entry steps (0.3 m)
        m.box('rust_dark', (W / 2 - 0.45, 0.35, d - dw / 2 + 0.05), (W / 2 - 0.06, 0.45, d + dw / 2 - 0.05))
    # windscreen frame
    for x in (-W / 2 + 0.05, -0.02, W / 2 - 0.09):
        m.box(paint, (x, belt, hz - 0.08), (x + 0.07, H - 0.3, hz))
    # roof: flat centre + chamfered edges, one rust hole
    m.box(paint, (-W / 2 + 0.25, H - 0.08, -hz), (W / 2 - 0.25, H, hz))
    for s in (-1, 1):
        m.oriented_box(paint, (s * (W / 2 - 0.02), H - 0.3, -hz), (s * (W / 2 - 0.02), H - 0.3, hz), 0.06, 0.06)
        m.push((0, 0, 0))
        a = (s * (W / 2 - 0.02), H - 0.3)
        b = (s * (W / 2 - 0.25), H - 0.04)
        m.quad(paint, (a[0], a[1], -hz), (a[0], a[1], hz), (b[0], b[1], hz), (b[0], b[1], -hz)) if s > 0 else \
            m.quad(paint, (a[0], a[1], hz), (a[0], a[1], -hz), (b[0], b[1], -hz), (b[0], b[1], hz))
        m.pop()
    m.box(paint, (-W / 2, H - 0.32, hz - 0.08), (W / 2, H, hz))   # front header
    if lod == 0 and enterable:
        # seats: pairs on the left, singles on the right
        n = int((length - 2.6) / 0.85)
        for i in range(n):
            zz = hz - 2.0 - i * 0.85
            if rng.random() < 0.15:
                continue
            for x0, x1 in ((-W / 2 + 0.1, -0.1), (W / 2 - 0.62, W / 2 - 0.12)):
                if x0 > 0 and any(abs(zz - d) < dw / 2 + 0.3 for d in doors):
                    continue
                m.box('wood_dark', (x0, floor + 0.38, zz - 0.22), (x1, floor + 0.46, zz + 0.2))
                m.box('wood_dark', (x0, floor + 0.46, zz - 0.26), (x1, floor + 1.0, zz - 0.2))
                m.box('rust_dark', ((x0 + x1) / 2 - 0.03, floor, zz - 0.1), ((x0 + x1) / 2 + 0.03, floor + 0.38, zz - 0.04))
        # driver cabin partition
        m.box('rust_dark', (-W / 2 + 0.1, floor, hz - 1.3), (-0.2, floor + 1.2, hz - 1.25))
        # handrail
        m.cylinder('rust', (0.4, H - 0.45, -hz + 0.3), (0.4, H - 0.45, hz - 1.3), 0.02, 4, caps=False)
    m.pop()
    return m


def carriage(seed=0, lod=0, paint='rust'):
    """Soviet-style passenger carriage (compartment car), 24 m x 3.1 m, floor at 1.3 m.
    Enterable through the end vestibule doors (steps on both sides at both ends)."""
    rng = np.random.default_rng(seed)
    m = Mesh(PALETTE)
    L, W = 24.0, 3.1
    hz = L / 2
    fl = 1.3          # floor top
    wall_top = 3.6
    seg = 10 if lod == 0 else 6
    # bogies
    for zc in (hz - 3.2, -hz + 3.2):
        m.box('rust_dark', (-1.1, 0.35, zc - 1.4), (1.1, 0.9, zc + 1.4))
        for dz in (-0.9, 0.9):
            for s in (-1, 1):
                m.cylinder('rust_dark', (s * 0.72, 0.47, zc + dz), (s * 0.82, 0.47, zc + dz), 0.47, seg)
            m.cylinder('rust_dark', (-0.72, 0.47, zc + dz), (0.72, 0.47, zc + dz), 0.08, 6, caps=False)
    # underframe and floor (walkable)
    m.box('rust_dark', (-W / 2 + 0.1, 0.9, -hz), (W / 2 - 0.1, fl - 0.05, hz))
    m.box('wood_floor', (-W / 2 + 0.05, fl - 0.05, -hz + 0.05), (W / 2 - 0.05, fl, hz - 0.05), faces='Y')
    # side walls with windows (window 1.0 x 0.8 m every 2.2 m), door gaps at both ends (vestibules)
    win_y0, win_y1 = fl + 0.95, fl + 1.85
    vest = 1.6     # vestibule length at each end
    dw = 0.9
    for s in (-1, 1):
        x0, x1 = (s * W / 2 - 0.05, s * W / 2) if s > 0 else (-W / 2, -W / 2 + 0.05)
        # lower wall below windows, except door gaps at vestibules
        d_front = (hz - vest / 2 - dw / 2, hz - vest / 2 + dw / 2)
        d_back = (-hz + vest / 2 - dw / 2, -hz + vest / 2 + dw / 2)
        for a, b in ((-hz, d_back[0]), (d_back[1], d_front[0]), (d_front[1], hz)):
            m.box(paint, (x0, fl - 0.3, a), (x1, win_y0, b))
        # door upper part
        for a, b in (d_front, d_back):
            m.box(paint, (x0, fl + 2.1, a), (x1, wall_top, b))
        # window band between vestibules
        piers = []
        zz = -hz + vest
        while zz < hz - vest:
            piers.append((zz, zz + 1.2))
            zz += 2.2
        for a, b in piers:
            b = min(b, hz)
            if b <= a:
                continue
            m.box(paint, (x0, win_y0, a), (x1, win_y1, b))
        # small vestibule windows: wall above doors at ends
        m.box(paint, (x0, win_y0, d_front[1]), (x1, win_y1, hz))
        m.box(paint, (x0, win_y0, -hz), (x1, win_y1, d_back[0]))
        m.box(paint, (x0, win_y0, d_back[1]), (x1, win_y1, -hz + vest))
        m.box(paint, (x0, win_y0, hz - vest), (x1, win_y1, d_front[0]))
        m.box(paint, (x0, win_y1, -hz), (x1, wall_top, d_back[0]))
        m.box(paint, (x0, win_y1, d_back[1]), (x1, wall_top, d_front[0]))
        m.box(paint, (x0, win_y1, d_front[1]), (x1, wall_top, hz))
        # steps under each door (3 steps of 0.32 m)
        for a, b in (d_front, d_back):
            for k in range(3):
                top = fl - 0.33 * (k + 1)
                out = 0.25 * (k + 1)
                xa, xb = (W / 2, W / 2 + out) if s > 0 else (-W / 2 - out, -W / 2)
                m.box('rust_dark', (xa, top - 0.05, a + 0.05), (xb, top, b - 0.05))
                m.box('rust_dark', (xa if s > 0 else xb - 0.03, top - 0.33, a + 0.05),
                      (xa + 0.03 if s > 0 else xb, top, a + 0.08))
    # end walls with gangway doors
    for zz, sgn in ((hz, 1), (-hz, -1)):
        za, zb = (zz - 0.05, zz) if sgn > 0 else (zz, zz + 0.05)
        m.box(paint, (-W / 2, fl - 0.3, za), (-0.45, wall_top, zb))
        m.box(paint, (0.45, fl - 0.3, za), (W / 2, wall_top, zb))
        m.box(paint, (-0.45, fl + 2.05, za), (0.45, wall_top, zb))
        m.box('rust_dark', (-0.7, 0.9, zz + sgn * 0.0), (0.7, 1.2, zz + sgn * 0.35))  # buffer beam
    # roof: shallow arc of panels
    arcs = 8 if lod == 0 else 4
    rise = 0.55
    for i in range(arcs):
        t0, t1 = i / arcs, (i + 1) / arcs
        xa, xb = -W / 2 + W * t0, -W / 2 + W * t1
        ya = wall_top + rise * math.sin(math.pi * t0)
        yb = wall_top + rise * math.sin(math.pi * t1)
        if lod == 0 and i == 5 and rng.random() < 0.7:
            # rusted-through hole in the roof
            for za, zb in ((-hz, -2.0), (1.5, hz)):
                m.quad(paint, (xa, ya, za), (xa, ya, zb), (xb, yb, zb), (xb, yb, za))
                m.quad('interior_dark', (xb, yb - 0.03, za), (xb, yb - 0.03, zb), (xa, ya - 0.03, zb), (xa, ya - 0.03, za))
            continue
        m.quad(paint, (xa, ya, -hz), (xa, ya, hz), (xb, yb, hz), (xb, yb, -hz))
        m.quad('interior_dark', (xb, yb - 0.03, -hz), (xb, yb - 0.03, hz), (xa, ya - 0.03, hz), (xa, ya - 0.03, -hz))
    for zz, sgn in ((hz, 1), (-hz, -1)):
        pts = [(-W / 2 + W * i / arcs, wall_top + rise * math.sin(math.pi * i / arcs), zz) for i in range(arcs + 1)]
        m.poly(paint, pts[::-1] if sgn > 0 else pts)
    if lod == 0:
        # interior: corridor along -X side, compartments on +X side (partitions every 2.2 m)
        cx = -W / 2 + 0.95   # corridor wall x
        zz = -hz + vest
        comp = []
        while zz + 2.2 <= hz - vest + 0.01:
            comp.append((zz, zz + 2.2))
            zz += 2.2
        for a, b in comp:
            # corridor wall with a 0.7 m door opening
            m.box('wood_dark', (cx, fl, a), (cx + 0.05, fl + 2.3, a + 0.25))
            m.box('wood_dark', (cx, fl, a + 0.95), (cx + 0.05, fl + 2.3, b))
            m.box('wood_dark', (cx, fl + 2.0, a + 0.25), (cx + 0.05, fl + 2.3, a + 0.95))
            # partition
            m.box('wood_dark', (cx, fl, b - 0.03), (W / 2 - 0.05, fl + 2.3, b + 0.03))
            # two berths (lower bench + upper bunk) on each side
            for zb0, zb1 in ((a + 0.05, a + 0.7), (b - 0.7, b - 0.05)):
                if rng.random() < 0.25:
                    continue
                m.box('wood_dark', (cx + 0.3, fl + 0.4, zb0), (W / 2 - 0.08, fl + 0.48, zb1))
                m.box('rust_dark', (cx + 0.3, fl, zb0 + 0.1), (cx + 0.35, fl + 0.4, zb0 + 0.15))
                if rng.random() < 0.6:
                    m.box('wood_dark', (cx + 0.3, fl + 1.55, zb0), (W / 2 - 0.08, fl + 1.6, zb1))
            m.box('wood_dark', (W / 2 - 0.5, fl + 0.7, (a + b) / 2 - 0.25), (W / 2 - 0.08, fl + 0.74, (a + b) / 2 + 0.25))
        # vestibule partitions with door openings
        for zz in (-hz + vest, hz - vest):
            m.box('wood_dark', (-W / 2 + 0.05, fl, zz - 0.03), (-W / 2 + 0.05 + 0.05, fl + 2.3, zz + 0.03))
            m.box('wood_dark', (cx, fl, zz - 0.03), (W / 2 - 0.05, fl + 2.3, zz + 0.03))
            m.box('wood_dark', (-W / 2 + 0.1, fl + 2.05, zz - 0.03), (cx, fl + 2.3, zz + 0.03))
    return m


def locomotive(seed=0, lod=0):
    """Steam locomotive (generic 0-8-0 freight type) with an open cab, about 13 m long, faces +Z."""
    m = Mesh(PALETTE)
    seg = 20 if lod == 0 else 10
    p = 'rust_dark'
    # frame and running boards
    m.box(p, (-1.1, 0.9, -5.5), (1.1, 1.25, 6.4))
    m.box('rust', (-1.55, 1.25, -5.5), (1.55, 1.3, 6.0))
    # drivers: 4 axles
    for i in range(4):
        z = 3.4 - i * 1.45
        for s in (-1, 1):
            m.cylinder('rust_dark', (s * 0.75, 0.66, z), (s * 0.9, 0.66, z), 0.66, seg)
        m.cylinder('rust_dark', (-0.75, 0.66, z), (0.75, 0.66, z), 0.1, 6, caps=False)
    for s in (-1, 1):   # coupling rods
        m.box('rust', (s * 0.92, 0.55, -1.0), (s * 0.97, 0.66, 3.5))
        m.cylinder('rust_dark', (s * 0.9, 1.0, 4.9), (s * 0.9, 1.0, 6.2), 0.35, 12)   # cylinders
    # boiler and smokebox
    m.cylinder(p, (0, 2.45, -2.0), (0, 2.45, 5.6), 0.95, seg)
    m.cylinder('rust_dark', (0, 2.45, 5.6), (0, 2.45, 6.5), 1.0, seg)
    m.cylinder('rust_dark', (0, 2.45, 6.5), (0, 2.45, 6.55), 0.8, seg)
    m.cylinder('rust_dark', (0, 3.3, 5.9), (0, 4.45, 5.9), 0.28, 12, r2=0.34)   # chimney
    m.cylinder(p, (0, 3.3, 2.5), (0, 3.85, 2.5), 0.38, 12)                      # steam dome
    m.cylinder(p, (0, 3.3, 0.5), (0, 3.7, 0.5), 0.3, 12)                        # sand dome
    # cab (open back, walkable floor at 1.3)
    cz0, cz1 = -5.4, -2.0
    m.box('rust', (-1.55, 1.3, cz1 - 0.05), (1.55, 3.9, cz1))                    # front wall
    for s in (-1, 1):
        x0, x1 = (1.5, 1.55) if s > 0 else (-1.55, -1.5)
        m.box('rust', (x0, 1.3, cz0), (x1, 2.2, cz1))
        m.box('rust', (x0, 2.2, cz1 - 0.3), (x1, 3.9, cz1))
        m.box('rust', (x0, 2.2, cz0), (x1, 3.9, cz0 + 0.3))
        m.box('rust', (x0, 3.3, cz0), (x1, 3.9, cz1))
    m.box('rust', (-1.7, 3.9, cz0 - 0.2), (1.7, 4.0, cz1 + 0.1))                 # roof
    m.box('rust_dark', (-0.8, 1.3, cz1 - 0.6), (0.8, 2.2, cz1 - 0.05))          # firebox backhead
    # cab steps
    for s in (-1, 1):
        for k in range(3):
            top = 1.3 - 0.33 * (k + 1)
            m.box('rust_dark', (s * 1.55 - 0.25 if s > 0 else -1.8, top - 0.05, cz0 + 0.1),
                  (1.8 if s > 0 else -1.55 + 0.25, top, cz0 + 0.6))
    # buffer beam + cowcatcher-ish plate
    m.box('paint_red', (-1.4, 0.9, 6.4), (1.4, 1.3, 6.6))
    return m


def tender(seed=0, lod=0):
    m = Mesh(PALETTE)
    seg = 12 if lod == 0 else 6
    for zc in (1.8, -1.8):
        for dz in (-0.7, 0.7):
            for s in (-1, 1):
                m.cylinder('rust_dark', (s * 0.72, 0.45, zc + dz), (s * 0.82, 0.45, zc + dz), 0.45, seg)
    m.box('rust_dark', (-1.1, 0.85, -3.6), (1.1, 1.25, 3.6))
    m.box('rust', (-1.5, 1.25, -3.5), (1.5, 3.3, 3.5), faces='xXyzZ')
    m.box('mud', (-1.45, 2.5, -3.45), (1.45, 2.6, 3.45), faces='Y')   # coal/soil load
    return m
