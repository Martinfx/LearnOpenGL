"""Infrastructure: rail track, guardrail, sign gantry, cooling towers, pipe racks, conveyor,
broken bridge, lamp posts, puddles and river water."""
import math
import numpy as np
from .mesh import Mesh, rot_y, rot_x, rot_z
from .mats import PALETTE


def track(length=25.0, lod=0):
    """Straight track along Z, centred, rails 1.52 m apart, top of rail at 0.35 m."""
    m = Mesh(PALETTE)
    hz = length / 2
    # ballast bed (trapezoid prism)
    m.quad('gravel', (-2.2, 0.0, hz), (2.2, 0.0, hz), (1.6, 0.2, hz), (-1.6, 0.2, hz))
    m.quad('gravel', (2.2, 0.0, -hz), (-2.2, 0.0, -hz), (-1.6, 0.2, -hz), (1.6, 0.2, -hz))
    m.quad('gravel', (-1.6, 0.2, hz), (1.6, 0.2, hz), (1.6, 0.2, -hz), (-1.6, 0.2, -hz))
    m.quad('gravel', (1.6, 0.2, hz), (2.2, 0.0, hz), (2.2, 0.0, -hz), (1.6, 0.2, -hz))
    m.quad('gravel', (-2.2, 0.0, hz), (-1.6, 0.2, hz), (-1.6, 0.2, -hz), (-2.2, 0.0, -hz))
    if lod == 0:
        rng = np.random.default_rng(int(length))
        n = int(length / 0.6)
        for i in range(n):
            z = -hz + 0.3 + i * 0.6
            if rng.random() < 0.06:
                continue
            m.push((rng.uniform(-0.05, 0.05), 0, z), rot_y(rng.uniform(-4, 4)))
            m.box('wood_dark', (-1.35, 0.17, -0.12), (1.35, 0.27, 0.12), faces='xXYzZ')
            m.pop()
    else:
        m.box('wood_dark', (-1.35, 0.2, -hz), (1.35, 0.25, hz), faces='Y')
    for s in (-1, 1):
        x = s * 0.76
        m.box('rust', (x - 0.07, 0.27, -hz), (x + 0.07, 0.29, hz), faces='xXY')
        m.box('rust', (x - 0.01, 0.29, -hz), (x + 0.01, 0.33, hz), faces='xX')
        m.box('rust', (x - 0.035, 0.33, -hz), (x + 0.035, 0.36, hz), faces='xXY')
    return m


def guardrail(length=24.0, lod=0):
    """W-beam guardrail along Z from z=0 to z=length; posts every 2 m, beam on the -X side.
    One sagging section in the middle where a post has bent over."""
    m = Mesh(PALETTE)
    n = int(length / 2)
    for i in range(n + 1):
        z = i * 2.0
        if i == n // 2:
            m.push((0, 0, z), rot_z(28))
            m.box('rust_sheet', (-0.1, 0.0, -0.04), (0.0, 0.85, 0.04))
            m.pop()
            continue
        m.box('rust_sheet', (-0.1, 0.0, z - 0.04), (0.0, 0.85, z + 0.04))
    sag = lambda z: -0.25 * max(0.0, 1 - abs(z - n // 2 * 2.0) / 5.0)
    zs = np.arange(0, length + 0.01, 2.0)
    for za, zb in zip(zs[:-1], zs[1:]):
        for y in (0.66, 0.5):
            m.oriented_box('rust_sheet', (-0.17, y + sag(za), za), (-0.17, y + sag(zb), zb), 0.05, 0.1)
    return m


def _sign(m, x0, x1, y0, y1, z, row, cols=(0.0, 1.0), facing=1):
    """Quad textured with one row (0..3) of the sign atlas; v is negated by the writer."""
    u0, u1 = cols
    v0, v1 = row * 0.25, (row + 1) * 0.25
    if facing > 0:
        m.poly('signs', [(x0, y0, z), (x1, y0, z), (x1, y1, z), (x0, y1, z)],
               uvs=[(u0, -v1), (u1, -v1), (u1, -v0), (u0, -v0)])
    else:
        m.poly('signs', [(x1, y0, z), (x0, y0, z), (x0, y1, z), (x1, y1, z)],
               uvs=[(u0, -v1), (u1, -v1), (u1, -v0), (u0, -v0)])


def sign_gantry(span=24.0, lod=0):
    """Overhead sign gantry across a highway (span along X), signs face +Z (toward oncoming traffic going -Z)."""
    m = Mesh(PALETTE)
    h = 6.5
    hx = span / 2
    for s in (-1, 1):
        m.cylinder('rust', (s * hx, 0, 0), (s * hx, h + 1.2, 0), 0.22, 10)
        m.box('concrete', (s * hx - 0.6, -0.2, -0.6), (s * hx + 0.6, 0.3, 0.6))
    # truss: two chords + diagonals
    for y in (h, h + 1.0):
        for z in (-0.4, 0.4):
            m.cylinder('rust', (-hx, y, z), (hx, y, z), 0.07, 6, caps=False)
    if lod == 0:
        n = int(span / 1.5)
        for i in range(n):
            xa = -hx + span * i / n
            xb = -hx + span * (i + 1) / n
            for z in (-0.4, 0.4):
                m.cylinder('rust', (xa, h, z), (xb, h + 1.0, z), 0.035, 4, caps=False)
                m.cylinder('rust', (xa, h, z), (xa, h + 1.0, z), 0.035, 4, caps=False)
    # signs (blue: Zavodsk, green: GRES/industrial zone); one hangs askew
    m.box('rust_dark', (-9.0, h - 1.2, 0.45), (-1.0, h + 1.9, 0.5))
    _sign(m, -9.0, -1.0, h - 1.2, h + 1.9, 0.51, 0)
    m.push((5.5, h + 1.9, 0.5), rot_z(-9))
    m.box('rust_dark', (-4.0, -3.1, -0.05), (4.0, 0.0, 0.0))
    _sign(m, -4.0, 4.0, -3.1, 0.0, 0.01, 1)
    m.pop()
    return m


def cooling_tower(broken=False, seed=0, lod=0):
    """Hyperboloid natural-draught cooling tower, 62 m base, 90 m high. Open between the legs,
    so the player can walk inside the basin. broken: a section of the shell has fallen out."""
    rng = np.random.default_rng(seed)
    m = Mesh(PALETTE)
    seg = 40 if lod == 0 else 20
    rb, rw, rt, H = 31.0, 20.0, 22.0, 90.0
    yw = 68.0
    leg_h = 7.0
    prof = []
    steps = 14 if lod == 0 else 7
    for i in range(steps + 1):
        y = leg_h + (H - leg_h) * i / steps
        if y < yw:
            t = (y - leg_h) / (yw - leg_h)
            r = rw + (rb - rw) * (1 - t) ** 1.6
        else:
            t = (y - yw) / (H - yw)
            r = rw + (rt - rw) * t ** 1.5
        prof.append((r, y))
    th = 0.5
    if broken:
        gap = (200.0, 245.0)
        arcs = [(gap[1], 360 + gap[0])]
        # lower part of the gap stays (ring beam to 25 m)
        low = [p for p in prof if p[1] <= 26.0]
        m.lathe('concrete_moss', low, seg, arc=gap, inside=False)
        m.lathe('concrete_dark', [(r - th, y) for r, y in low], seg, arc=gap, outside=False)
        for a0, a1 in arcs:
            m.lathe('concrete_moss', prof, seg, arc=(a0, a1), inside=False)
            m.lathe('concrete_dark', [(r - th, y) for r, y in prof], seg, arc=(a0, a1), outside=False)
        # jagged edges: stepped blocks along the gap sides
        for ang in gap:
            a = math.radians(ang)
            for p in prof:
                if p[1] < 26:
                    continue
                r = p[0]
                w = rng.uniform(1.0, 3.5)
                m.push((math.cos(a) * (r - th / 2), p[1] - 3, math.sin(a) * (r - th / 2)), rot_y(-ang))
                m.box('concrete_dark', (-th, 0, -w / 2), (th, rng.uniform(2, 6), w / 2))
                m.pop()
        # fallen shell fragments inside the basin
        for k in range(6 if lod == 0 else 2):
            a = math.radians(rng.uniform(*gap))
            r = rng.uniform(8, 24)
            m.push((math.cos(a) * r, 0.5, math.sin(a) * r), rot_y(rng.uniform(0, 360)) @ rot_x(rng.uniform(5, 30)))
            m.box('concrete_moss', (-rng.uniform(2, 5), -0.5, -0.25), (rng.uniform(2, 5), 0.5, rng.uniform(3, 7)))
            m.pop()
    else:
        m.lathe('concrete_moss', prof, seg, inside=False)
        m.lathe('concrete_dark', [(r - th, y) for r, y in prof], seg, outside=False)
    # top rim
    m.lathe('concrete_dark', [(rt - th, H), (rt + 0.2, H + 0.4), (rt + 0.2, H + 0.6)], seg, inside=False,
            arc=(0, 360) if not broken else (245.0, 560.0))
    # bottom ring beam
    m.lathe('concrete', [(rb + 0.3, leg_h - 1.0), (rb + 0.3, leg_h), (rb, leg_h)], seg, inside=False)
    m.lathe('concrete', [(rb - 1.0, leg_h - 1.0), (rb - 1.0, leg_h)], seg, outside=False)
    m.lathe('concrete', [(rb - 1.0, leg_h - 1.0), (rb + 0.3, leg_h - 1.0)], seg, outside=False, inside=True)
    # diagonal legs (pairs forming a lattice)
    nleg = 36 if lod == 0 else 18
    for i in range(nleg):
        a0 = 2 * math.pi * i / nleg
        a1 = 2 * math.pi * (i + 0.5) / nleg
        for aa, bb in ((a0, a1), (a1 + math.pi / nleg, a1)):
            p0 = (math.cos(aa) * rb, 0.0, math.sin(aa) * rb)
            p1 = (math.cos(bb) * (rb - 0.3), leg_h - 1.0, math.sin(bb) * (rb - 0.3))
            m.oriented_box('concrete', p0, p1, 0.6, 0.6)
    # basin floor: shallow wall ring and mud floor
    m.lathe('concrete', [(rb + 2.0, 0.0), (rb + 2.0, 0.6), (rb + 1.5, 0.6)], seg, inside=False)
    if lod == 0:
        # internal fill (packing) supports: grid of columns
        for x in range(-18, 19, 6):
            for z in range(-18, 19, 6):
                if x * x + z * z > 22 * 22 or rng.random() < 0.3:
                    continue
                m.box_c('concrete', (x, 2.5, z), (0.5, 5.0, 0.5))
    return m


def pipe_rack(length=24.0, lod=0):
    """Rusted pipe bridge along Z: portal frames every 6 m, three big pipes, catwalk on top with railing."""
    m = Mesh(PALETTE)
    seg = 12 if lod == 0 else 6
    hz = length / 2
    h = 5.0
    n = int(length / 6)
    for i in range(n + 1):
        z = -hz + i * length / n
        for s in (-1, 1):
            m.box_c('rust', (s * 1.6, h / 2, z), (0.25, h, 0.25))
            m.box_c('concrete', (s * 1.6, 0.15, z), (0.7, 0.3, 0.7))
        m.box_c('rust', (0, h, z), (3.6, 0.25, 0.25))
        m.box_c('rust', (0, h - 1.6, z), (3.6, 0.2, 0.2))
    for x, r, y in ((-0.9, 0.55, h + 0.68), (0.55, 0.4, h + 0.53), (0.0, 0.3, h - 1.2)):
        m.cylinder('rust_dark' if r > 0.5 else 'rust', (x, y, -hz), (x, y, hz), r, seg, caps=False)
    # catwalk: grating on the +X edge with railings
    m.box('metal_plate', (1.0, h + 0.13, -hz), (1.8, h + 0.18, hz))
    if lod == 0:
        for z in np.arange(-hz, hz + 0.01, 2.0):
            m.box_c('rust', (1.78, h + 0.7, z), (0.05, 1.05, 0.05))
        m.cylinder('rust', (1.78, h + 1.2, -hz), (1.78, h + 1.2, hz), 0.03, 5, caps=False)
        m.cylinder('rust', (1.78, h + 0.7, -hz), (1.78, h + 0.7, hz), 0.025, 5, caps=False)
    return m


def conveyor(fallen=True, lod=0):
    """Inclined belt conveyor gallery (truss), 30 m; fallen: one end on the ground, broken support."""
    m = Mesh(PALETTE)
    L = 30.0
    ang = 14 if fallen else 22
    m.push((0, 0.4, 0), rot_x(-ang) @ rot_z(6 if fallen else 0))
    w, h = 2.4, 2.2
    for y in (0, h):
        for x in (-w / 2, w / 2):
            m.box('rust', (x - 0.1, y - 0.1, 0), (x + 0.1, y + 0.1, L))
    k = 12 if lod == 0 else 4
    for i in range(k + 1):
        z = L * i / k
        for x in (-w / 2, w / 2):
            m.box('rust', (x - 0.06, 0, z - 0.06), (x + 0.06, h, z + 0.06))
            if i < k and lod == 0:
                m.oriented_box('rust', (x, 0, z), (x, h, z + L / k), 0.06, 0.06)
        m.box('rust', (-w / 2, -0.06, z - 0.06), (w / 2, 0.06, z + 0.06))
    m.box('rust_dark', (-0.6, 0.15, 0), (0.6, 0.25, L))           # belt
    m.box('metal_plate', (0.7, 0.05, 0), (1.15, 0.1, L))           # walkway
    if lod == 0:
        for i in range(0, k, 2):  # corrugated cladding remnants
            z = L * i / k
            m.box('corrugated', (-w / 2 - 0.12, 0.2, z), (-w / 2 - 0.1, h, z + L / k * 0.8))
    m.pop()
    # supports: one standing, one buckled
    m.box_c('rust', (0, 1.0, 1.0), (0.3, 2.0, 0.3))
    m.push((0, 0, L * 0.75), rot_z(35 if fallen else 0))
    m.box('rust', (-0.15, 0, -0.15), (0.15, 6.0, 0.15))
    m.pop()
    return m


def bridge(lod=0):
    """Concrete road bridge along Z over a 44 m river: 4 spans of 20 m on piers, deck 12 m wide.
    Span 3 has collapsed: one end dropped into the river, so it forms a steep ramp."""
    rng = np.random.default_rng(9)
    m = Mesh(PALETTE)
    W = 12.0
    top = 0.05
    spans = [(-40, -20), (-20, 0), (0, 20), (20, 40)]
    for zp in (-20, 0, 20):
        m.box('concrete_moss', (-4.5, -9.0, zp - 1.0), (4.5, top - 1.2, zp + 1.0))
        m.box('concrete_dark', (-5.5, top - 1.2, zp - 1.2), (5.5, top - 0.6, zp + 1.2))
    for zp in (-40, 40):  # abutments
        m.box('concrete_moss', (-W / 2 - 0.5, -9.0, zp - 3.0 if zp < 0 else zp), (W / 2 + 0.5, top, zp if zp < 0 else zp + 3.0))

    def deck(z0, z1, rail_l=True, rail_r=True, broken_end=None):
        m.box('asphalt', (-W / 2 + 0.6, top - 0.05, z0), (W / 2 - 0.6, top, z1), faces='Y')
        m.box('concrete_moss', (-W / 2, top - 0.6, z0), (W / 2, top - 0.05, z1), faces='xXyzZ')
        m.box('concrete_moss', (-W / 2 + 0.6, top - 0.05, z0), (-W / 2 + 0.6, top, z1), faces='')
        for s, keep in ((-1, rail_l), (1, rail_r)):
            # kerb + pipe railing
            m.box('concrete', (s * W / 2 - (0.6 if s > 0 else 0), top - 0.05, z0), (s * W / 2 + (0.6 if s < 0 else 0), top + 0.25, z1))
            if not keep:
                continue
            x = s * (W / 2 - 0.3)
            for z in np.arange(z0 + 1.0, z1, 2.5):
                m.cylinder('rust', (x, top + 0.25, z), (x, top + 1.1, z), 0.04, 6, caps=False)
            m.cylinder('rust', (x, top + 1.1, z0), (x, top + 1.1, z1), 0.12, 10)

    deck(-43, -20)
    deck(-20, 0, rail_r=False)
    deck(20, 43)
    # collapsed span 0..20: north end on pier at 20 (top), south end in the river at -2 m
    a = math.degrees(math.atan2(7.9, 20.0))
    m.push((0, top, 20.0), rot_x(-a))
    m.box('concrete_moss', (-W / 2, -0.6, -21.5), (W / 2, 0, 0))
    m.box('asphalt', (-W / 2 + 0.6, 0, -21.5), (W / 2 - 0.6, 0.05, 0), faces='Y')
    for z in np.arange(-20, 0, 2.5):
        m.cylinder('rust', (W / 2 - 0.3, 0.0, z), (W / 2 - 0.3, 0.9, z), 0.04, 6, caps=False)
    m.cylinder('rust', (W / 2 - 0.3, 0.9, -21), (W / 2 - 0.3, 0.9, 0), 0.12, 10)
    m.pop()
    # rubble and hanging rebar at the break
    for k in range(10 if lod == 0 else 3):
        m.push((rng.uniform(-5, 5), rng.uniform(-2.5, -1.0), rng.uniform(-2, 4)), rot_y(rng.uniform(0, 360)) @ rot_x(rng.uniform(0, 40)))
        m.box('concrete_dark', (-rng.uniform(0.5, 1.5), -0.4, -0.5), (rng.uniform(0.5, 1.5), 0.4, rng.uniform(0.5, 2)))
        m.pop()
    if lod == 0:
        for k in range(14):
            x = rng.uniform(-5.5, 5.5)
            m.cylinder('rust', (x, top - 0.3, 0.0), (x + rng.uniform(-0.3, 0.3), top - rng.uniform(1.0, 2.5), rng.uniform(0.3, 1.0)), 0.025, 4, caps=False)
    return m


def lamp_post(lean=0.0, lod=0):
    """Soviet concrete street lamp post, 9 m, arm toward +X."""
    m = Mesh(PALETTE)
    m.push((0, 0, 0), rot_z(-lean))
    m.cylinder('concrete', (0, 0, 0), (0, 9.0, 0), 0.16, 8, r2=0.1)
    m.cylinder('rust', (0, 8.6, 0), (1.4, 9.2, 0), 0.04, 6)
    m.box('rust_dark', (1.2, 8.95, -0.18), (1.9, 9.2, 0.18))
    m.pop()
    return m


def puddle(seed=0, size=4.0, lod=0):
    """Irregular flat water patch, 1 cm above ground (very smooth: reflects the sky)."""
    rng = np.random.default_rng(seed)
    m = Mesh(PALETTE)
    n = 14
    pts = []
    for i in range(n):
        a = -2 * math.pi * i / n
        r = size / 2 * rng.uniform(0.6, 1.0) * (1.0 if i % 2 else 0.85)
        pts.append((math.cos(a) * r * 1.3, 0.012, math.sin(a) * r * 0.8))
    for i in range(n):
        m.poly('water', [(0, 0.012, 0), pts[(i + 1) % n], pts[i]][::-1])
    return m


def river(length=80.0, width=44.0, lod=0):
    """River in a ravine (along X): still dark water 7.7 m below the ground, muddy banks that
    slope from y=0 at |z| = width/2 + 15 down to the water. Collision: walking on the water surface
    reads as wading in shallow water."""
    m = Mesh(PALETTE)
    hx, hz = length / 2, width / 2
    m.quad('water', (-hx, -7.7, hz), (hx, -7.7, hz), (hx, -7.7, -hz), (-hx, -7.7, -hz))
    for s in (-1, 1):
        z0, z1 = s * hz, s * (hz + 15.0)
        if s > 0:
            m.quad('mud', (-hx, -8.0, z0 - 2), (hx, -8.0, z0 - 2), (hx, 0.0, z1), (-hx, 0.0, z1))
        else:
            m.quad('mud', (hx, -8.0, z0 + 2), (-hx, -8.0, z0 + 2), (-hx, 0.0, z1), (hx, 0.0, z1))
    return m


def rubble(seed=0, size=3.0, lod=0):
    """Heap of broken concrete and brick chunks (cheap replacement for the 18k-tri scan)."""
    rng = np.random.default_rng(seed)
    m = Mesh(PALETTE)
    n = 26 if lod == 0 else 8
    for k in range(n):
        r = size / 2 * math.sqrt(rng.random())
        a = rng.uniform(0, 2 * math.pi)
        h = max(0.0, (1 - r / (size / 2))) * size * 0.25
        m.push((math.cos(a) * r, h * rng.uniform(0.3, 1.0), math.sin(a) * r),
               rot_y(rng.uniform(0, 360)) @ rot_x(rng.uniform(-35, 35)) @ rot_z(rng.uniform(-35, 35)))
        s = rng.uniform(0.15, 0.6) * size / 3
        mat = 'brick_red' if rng.random() < 0.3 else ('concrete_dark' if rng.random() < 0.5 else 'concrete')
        m.box(mat, (-s, -s * 0.4, -s * 0.7), (s, s * 0.4, s * 0.7))
        m.pop()
    if lod == 0:
        for k in range(5):
            a = rng.uniform(0, 2 * math.pi)
            p = (math.cos(a) * size * 0.2, 0.2, math.sin(a) * size * 0.2)
            q = (p[0] + rng.uniform(-1, 1), rng.uniform(0.5, 1.2), p[2] + rng.uniform(-1, 1))
            m.cylinder('rust', p, q, 0.012, 4, caps=False)
    return m


def slab_fallen(seed=0, lod=0):
    """Large cracked concrete panel lying tilted on the ground, rebar sticking out."""
    rng = np.random.default_rng(seed)
    m = Mesh(PALETTE)
    m.push((0, 0.6, 0), rot_x(rng.uniform(8, 18)) @ rot_z(rng.uniform(-6, 6)))
    m.box('concrete_moss', (-3.0, -0.12, -1.5), (rng.uniform(0.0, 0.8), 0.12, 1.5))
    m.box('concrete_moss', (rng.uniform(0.9, 1.2), -0.12, -1.5), (3.0, 0.12, 1.5))
    if lod == 0:
        for z in np.arange(-1.3, 1.31, 0.3):
            m.cylinder('rust', (0.5, 0.0, z), (1.0, rng.uniform(-0.2, 0.3), z), 0.012, 4, caps=False)
    m.pop()
    return m


def station_sign(lod=0):
    """Station name board on two posts (atlas row 3, left half)."""
    m = Mesh(PALETTE)
    for x in (-1.6, 1.6):
        m.cylinder('rust', (x, 0, 0), (x, 3.2, 0), 0.05, 6)
    m.box('rust_dark', (-1.9, 2.0, -0.03), (1.9, 3.2, 0.0))
    _sign(m, -1.9, 1.9, 2.0, 3.2, 0.005, 3, cols=(0.0, 0.5))
    m.push((0, 0, -0.03), rot_y(180))
    _sign(m, -1.9, 1.9, 2.0, 3.2, 0.005, 3, cols=(0.0, 0.5))
    m.pop()
    return m


def warning_sign(lod=0):
    m = Mesh(PALETTE)
    m.cylinder('rust', (0, 0, 0), (0, 2.4, 0), 0.04, 6)
    m.box('rust_dark', (-0.5, 1.5, -0.03), (0.5, 2.5, 0.0))
    _sign(m, -0.5, 0.5, 1.5, 2.5, 0.005, 3, cols=(0.5, 1.0))
    return m
