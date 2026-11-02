"""Town square furniture (CC0): war memorial, dry fountain, park bench, newspaper kiosk.
Soviet provincial style, decades without care. Metres, +Y up, front toward +Z."""
import math
import numpy as np
from .mesh import Mesh, rot_y, rot_x, rot_z
from .mats import PALETTE


def monument(seed=0, lod=0):
    """War memorial: stepped plinth, tapering obelisk with a rusty star, wreath slab in front.
    Footprint about 7 x 7 m, 15 m high."""
    rng = np.random.default_rng(seed)
    m = Mesh(PALETTE)
    # Three steps of a square platform (worn concrete, moss in the joints).
    for k, (hw, y0, y1) in enumerate(((3.6, 0.0, 0.18), (3.1, 0.18, 0.36), (2.6, 0.36, 0.54))):
        m.box('concrete_moss' if k == 0 else 'concrete', (-hw, y0, -hw), (hw, y1, hw))
    # Plinth with a darker cap and a plaque on the front.
    m.box('concrete_dark', (-1.4, 0.54, -1.4), (1.4, 3.0, 1.4))
    m.box('concrete', (-1.6, 3.0, -1.6), (1.6, 3.3, 1.6))
    m.box('rust_dark', (-0.9, 1.3, 1.4), (0.9, 2.4, 1.46))
    # Obelisk: a tapered square shaft in four stacked sections (slightly uneven, settling).
    y = 3.3
    w = 0.95
    for k in range(4 if lod == 0 else 1):
        h = 2.8 if lod == 0 else 11.2
        w2 = w * (0.86 if lod == 0 else 0.55)
        m.push((rng.uniform(-0.01, 0.01), y, rng.uniform(-0.01, 0.01)), rot_y(rng.uniform(-0.6, 0.6)))
        pts = [(-w, 0, -w), (w, 0, -w), (w, 0, w), (-w, 0, w)]
        top = [(-w2, h, -w2), (w2, h, -w2), (w2, h, w2), (-w2, h, w2)]
        for i in range(4):
            a, b = pts[i], pts[(i + 1) % 4]
            c, d = top[(i + 1) % 4], top[i]
            m.quad('concrete', d, c, b, a)
        m.pop()
        y += h
        w = w2
    # Pyramid cap and the star (two flat rusty plates crossed).
    apex = (0, y + 0.8, 0)
    base = [(-w, y, -w), (w, y, -w), (w, y, w), (-w, y, w)]
    for i in range(4):
        m.poly('concrete_dark', [apex, base[(i + 1) % 4], base[i]])
    star = []
    for i in range(10):
        a = math.pi / 2 + i * math.pi / 5
        r = 0.75 if i % 2 == 0 else 0.3
        star.append((math.cos(a) * r, math.sin(a) * r))
    for rot in (0, 90):
        m.push((0, y + 1.75, 0), rot_y(rot) @ rot_z(rng.uniform(-8, 8)))
        for i in range(10):
            p, q = star[i], star[(i + 1) % 10]
            m.poly('rust', [(0, 0, 0.03), (p[0], p[1], 0.03), (q[0], q[1], 0.03)])
            m.poly('rust', [(0, 0, -0.03), (q[0], q[1], -0.03), (p[0], p[1], -0.03)])
        m.pop()
    # Wreath slab and a cracked flower bed in front.
    m.push((0, 0.54, 2.05), rot_x(-12))
    m.box('concrete_dark', (-0.9, 0, -0.35), (0.9, 0.12, 0.35))
    m.pop()
    if lod == 0:
        m.box('concrete_moss', (-2.4, 0.54, 1.75), (-1.6, 0.8, 2.55))
        m.box('concrete_moss', (1.6, 0.54, 1.75), (2.4, 0.8, 2.55))
        # Bits fallen off the cornice.
        for k in range(6):
            a = rng.uniform(0, 2 * math.pi)
            r = rng.uniform(1.8, 3.2)
            m.push((math.cos(a) * r, 0.54, math.sin(a) * r), rot_y(rng.uniform(0, 360)) @ rot_x(rng.uniform(-20, 20)))
            s = rng.uniform(0.12, 0.3)
            m.box('concrete', (-s, 0, -s * 0.7), (s, s * 0.8, s * 0.7))
            m.pop()
    return m


def fountain(seed=0, radius=5.0, lod=0):
    """Dry round fountain: low rim, cracked basin with mud, a bowl on a column in the middle."""
    rng = np.random.default_rng(seed)
    m = Mesh(PALETTE)
    seg = 32 if lod == 0 else 12
    r0, r1 = radius, radius - 0.4
    for i in range(seg):
        a0, a1 = 2 * math.pi * i / seg, 2 * math.pi * (i + 1) / seg
        c0, s0, c1, s1 = math.cos(a0), math.sin(a0), math.cos(a1), math.sin(a1)
        h = 0.55 if not (lod == 0 and i % 11 == 3) else 0.25   # a broken bit of rim
        # outer face, top, inner face
        m.quad('concrete', (c1 * r0, 0, s1 * r0), (c0 * r0, 0, s0 * r0), (c0 * r0, h, s0 * r0), (c1 * r0, h, s1 * r0))
        m.quad('concrete_moss', (c1 * r0, h, s1 * r0), (c0 * r0, h, s0 * r0), (c0 * r1, h, s0 * r1), (c1 * r1, h, s1 * r1))
        m.quad('concrete_dark', (c1 * r1, h, s1 * r1), (c0 * r1, h, s0 * r1), (c0 * r1, -0.2, s0 * r1), (c1 * r1, -0.2, s1 * r1))
        # basin floor: mud
        m.poly('mud', [(0, -0.2, 0), (c1 * r1, -0.2, s1 * r1), (c0 * r1, -0.2, s0 * r1)])
    # Column and bowl.
    m.cylinder('concrete', (0, -0.2, 0), (0, 1.6, 0), 0.32, 12 if lod == 0 else 6)
    m.lathe('concrete', [(0.3, 1.5), (1.3, 1.75), (1.4, 1.95), (1.2, 1.95), (0.25, 1.75)], seg=24 if lod == 0 else 8)
    if lod == 0:
        for k in range(8):
            a = rng.uniform(0, 2 * math.pi)
            r = rng.uniform(1.2, r1 - 0.5)
            m.push((math.cos(a) * r, -0.2, math.sin(a) * r), rot_y(rng.uniform(0, 360)))
            s = rng.uniform(0.1, 0.25)
            m.box('concrete_dark', (-s, 0, -s), (s, s * 0.6, s))
            m.pop()
    return m


def bench(seed=0, lod=0):
    """Park bench: two concrete legs, wooden seat and back slats (one missing or broken), 1.9 m long."""
    rng = np.random.default_rng(seed)
    m = Mesh(PALETTE)
    for x in (-0.75, 0.75):
        m.box('concrete', (x - 0.06, 0, -0.25), (x + 0.06, 0.42, 0.22))
        m.box('concrete', (x - 0.06, 0.42, -0.28), (x + 0.06, 0.85, -0.18))
    missing = int(rng.integers(0, 7)) if rng.random() < 0.6 else -1
    for k in range(3):
        if k == missing:
            continue
        z = 0.15 - k * 0.14
        m.box('wood_dark', (-0.95, 0.42, z - 0.055), (0.95, 0.46, z + 0.055))
    for k in range(3):
        if k + 3 == missing:
            continue
        y = 0.55 + k * 0.12
        m.push((0, y, -0.24), rot_x(-12))
        m.box('wood_dark', (-0.95, -0.045, -0.02), (0.95, 0.045, 0.02))
        m.pop()
    if missing == 6:   # a broken slat on the ground
        m.push((rng.uniform(-0.5, 0.5), 0.02, 0.45), rot_y(rng.uniform(-30, 30)))
        m.box('wood_dark', (-0.6, 0, -0.05), (0.6, 0.04, 0.05))
        m.pop()
    return m


def kiosk(seed=0, lod=0):
    """Newspaper kiosk (Soyuzpechat style): 3.2 x 2.2 m, 2.7 m high, glazed front all broken,
    rusty sheet sides, flat roof with an overhang. Front toward +Z."""
    rng = np.random.default_rng(seed)
    m = Mesh(PALETTE)
    w, d, h = 1.6, 1.1, 2.6
    m.box('concrete', (-w - 0.1, 0, -d - 0.1), (w + 0.1, 0.15, d + 0.1))
    m.box('rust_sheet', (-w, 0.15, -d), (w, h, -d + 0.04))                 # back
    for s in (-1, 1):
        x0, x1 = (s * w - 0.04, s * w) if s > 0 else (s * w, s * w + 0.04)
        m.box('rust_sheet', (x0, 0.15, -d), (x1, h, d))
    m.box('paint_blue', (-w, 0.15, d - 0.04), (w, 1.0, d))                # front below the windows
    m.box('paint_blue', (-w, h - 0.35, d - 0.04), (w, h, d))              # sign band
    for x in np.linspace(-w, w, 5):
        m.box('rust_dark', (x - 0.03, 1.0, d - 0.04), (x + 0.03, h - 0.35, d))
    if lod == 0:
        # A few shards left in the frames.
        for k in range(4):
            x = rng.uniform(-w + 0.2, w - 0.6)
            m.poly('glass', [(x, 1.0, d - 0.02), (x + rng.uniform(0.2, 0.5), 1.0, d - 0.02), (x + 0.1, 1.0 + rng.uniform(0.2, 0.6), d - 0.02)])
        m.box('wood', (-w + 0.05, 0.15, -d + 0.05), (w - 0.05, 0.95, -d + 0.5))  # counter inside
        m.box('interior_dark', (-w + 0.05, 0.15, -d + 0.05), (w - 0.05, 0.16, d - 0.05))
    m.push((0, h, 0), rot_x(rng.uniform(-2, 2)))
    m.box('roof_tar', (-w - 0.35, 0, -d - 0.25), (w + 0.35, 0.12, d + 0.5))
    m.pop()
    return m
