"""Street furniture of Lower Manhattan (original, CC0): what stands on a New York pavement besides lamps and hydrants.

A subway entrance (stairs down between green railings, two green globe lamps at the mouth, a closed gate at the
bottom), street trees in pits with iron guards, the blue mailbox, a row of coin newspaper boxes, parking meters, the
red fire alarm call box, a payphone pedestal and a bus shelter. Twenty years of nobody: rust, broken glass, leaves.
All generic shapes, no names or logos.

Frames: the origin is the foot on the pavement (y = 0); the side a passer-by uses faces +Z.
- subway_entrance(): the stairs go down toward -Z from the mouth at z = 0. The opening is |x| < 0.75 wide, its
  walls reach |x| = 1.0, a granite apron lies flush round it to |x| = 1.55 and z = -L - 0.55, a sill to z = +0.5.
  The level lowers the ground at the terrain grid vertices with |lx| <= 1.0 and -L <= lz <= -0.5 (L = 7); place it
  on the 0.5 m terrain grid so the drop hides under the apron and inside the walls. m.stash: a spot on the landing.
- tree_pit(): the pit is 1.8 m along X (the kerb) by 1.2 m, its centre at 0.
- bus_shelter(): 4.4 m along X, the open side +Z (toward the kerb), the back at z = -0.75.
"""
import math

import numpy as np

from .mats import M, PALETTE
from .mesh import Mesh, rot_x, rot_y, rot_z

PAL = dict(PALETTE)
PAL['subway_green'] = M('paint_green', 1.2, color=(0.55, 0.72, 0.58))
PAL['globe_green'] = M(None, 1.0, color=(0.08, 0.3, 0.14), rough=0.12, metal=0.0, alpha=0.82)
PAL['globe_red'] = M(None, 1.0, color=(0.45, 0.06, 0.03), rough=0.12, metal=0.0, alpha=0.85)
PAL['granite'] = M('concrete', 1.6, color=(0.78, 0.76, 0.74))
PAL['mail_blue'] = M('paint_blue', 1.0, color=(0.55, 0.7, 1.0))
PAL['alarm_red'] = M('paint_red', 1.0, color=(1.2, 0.75, 0.7))
PAL['alu'] = M('metal_plate', 1.2, color=(0.8, 0.82, 0.85))
PAL['steel_dark'] = M('metal_plate', 1.2, color=(0.42, 0.43, 0.45))
PAL['poster'] = M('paint_cream', 1.2, color=(0.9, 0.86, 0.78))
PAL['news_red'] = M('paint_red', 1.0)
PAL['news_yellow'] = M('paint_yellow', 1.0)
PAL['news_blue'] = M('paint_blue', 1.0)
PAL['news_green'] = M('paint_green', 1.0)
PAL['news_white'] = M('paint_cream', 1.0, color=(1.1, 1.1, 1.1))
PAL['arch_white'] = M('paint_cream', 1.0, color=(1.15, 1.12, 1.05))
PAL['bulb'] = M(None, 1.0, color=(0.78, 0.74, 0.62), rough=0.15, metal=0.0, alpha=0.9)
PAL['bulb_red'] = M(None, 1.0, color=(0.42, 0.16, 0.12), rough=0.15, metal=0.0, alpha=0.9)
PAL['bulb_green'] = M(None, 1.0, color=(0.16, 0.32, 0.18), rough=0.15, metal=0.0, alpha=0.9)
PAL['canvas'] = M('paint_cream', 1.3, color=(0.85, 0.8, 0.7))
PAL['canvas_green'] = M('paint_green', 1.3, color=(0.7, 0.85, 0.7))


def _mesh():
    return Mesh(PAL)


def _globe(m, mat, c, r, seg=10, broken=False):
    """A glass globe centred at c; broken: only its lower half is left in the holder."""
    n = 6
    top = n // 2 if broken else n
    prof = [(max(r * math.sin(math.pi * i / n), 0.004), -r * math.cos(math.pi * i / n)) for i in range(top + 1)]
    m.push(c)
    m.lathe(mat, prof, seg=seg, inside=broken, outside=True)
    m.pop()


# ------------------------------------------------------------------------------------------------ subway entrance
SUBWAY_L = 7.0


def subway_entrance(lod=0, seed=1, broken_globe=1):
    """Stairs down to a station that has been shut for twenty years. broken_globe: which lamp lost its glass
    (-1 left, 1 right, 0 none)."""
    rng = np.random.default_rng(seed)
    m = _mesh()
    N, RISE, RUN, L = 20, 0.18, 0.28, SUBWAY_L
    BOT = -N * RISE                       # -3.6: the landing
    DEEP = BOT - 0.25
    hw, ww, aw, base = 0.75, 1.0, 1.55, 0.3
    # granite apron flush with the pavement (the ground drops under it), the sill at the mouth
    for s in (-1, 1):
        x0, x1 = sorted((s * ww, s * aw))
        m.box('granite', (x0, -0.3, -L - 0.55), (x1, 0.04, 0.5), faces='xXYzZ')
    m.box('granite', (-ww, -0.3, -L - 0.55), (ww, 0.04, -L - 0.25), faces='YzZ')
    m.box('granite', (-ww, -0.3, -0.05), (ww, 0.04, 0.5), faces='YzZ')
    # the side walls: tiled inside, a stone base above the pavement for the railings
    for s in (-1, 1):
        x0, x1 = sorted((s * hw, s * ww))
        inner = 'X' if s < 0 else 'x'
        m.box('concrete', (x0, DEEP, -L), (x1, 0.04, 0.0), faces=inner, mats={inner: 'tiles'})
        m.box('granite', (x0, 0.04, -L - 0.25), (x1, base, 0.0), faces='xXYzZ')
    m.box('granite', (-ww, 0.04, -L - 0.25), (ww, base, -L), faces='YzZ')
    # the steps (solid, so the ground under them never shows) with steel nosings, then the landing
    for k in range(1, N):
        z0, z1 = -0.05 - (k - 1) * RUN, -0.05 - k * RUN
        yk = -k * RISE
        m.box('concrete', (-hw, DEEP, z1), (hw, yk, z0), faces='YZ')
        if lod == 0:
            m.box('steel_dark', (-hw, yk - 0.03, z0 - 0.045), (hw, yk + 0.004, z0 + 0.004), faces='YZ')
    zl = -0.05 - (N - 1) * RUN
    m.box('concrete', (-hw, DEEP, -L), (hw, BOT, zl), faces='Y')
    # the far wall with the passage under the pavement, its gate pulled shut and padlocked
    dw, dh = 0.6, 2.3
    m.box('concrete', (-ww, DEEP, -L - 0.25), (-dw, base, -L), faces='Z', mats={'Z': 'tiles'})
    m.box('concrete', (dw, DEEP, -L - 0.25), (ww, base, -L), faces='Z', mats={'Z': 'tiles'})
    m.box('concrete', (-dw, BOT + dh, -L - 0.25), (dw, base, -L), faces='Zy', mats={'Z': 'tiles'})
    m.box('interior_dark', (-dw - 0.2, BOT, -L - 2.6), (dw + 0.2, BOT + dh + 0.2, -L - 2.4), faces='Z')
    m.box('interior_dark', (-dw - 0.2, BOT, -L - 2.4), (-dw, BOT + dh, -L - 0.25), faces='X')
    m.box('interior_dark', (dw, BOT, -L - 2.4), (dw + 0.2, BOT + dh, -L - 0.25), faces='x')
    m.box('interior_dark', (-dw, BOT + dh, -L - 2.4), (dw, BOT + dh + 0.2, -L - 0.25), faces='y')
    m.box('tiles_floor', (-dw, BOT - 0.2, -L - 2.4), (dw, BOT, -L), faces='Y')
    gz = -L - 0.1
    m.box('steel_dark', (-dw, BOT + dh - 0.08, gz - 0.03), (dw, BOT + dh, gz + 0.03))           # the gate's track
    step = 0.11 if lod == 0 else 0.3
    for x in np.arange(-dw + 0.03, dw, step):
        m.box('steel_dark', (x - 0.01, BOT, gz - 0.01), (x + 0.01, BOT + dh - 0.08, gz + 0.01), faces='xXZ')
    if lod == 0:                                                       # the scissor lattice, two rows of diamonds
        for y0 in (BOT + 0.5, BOT + 1.4):
            for x in np.arange(-dw + 0.03, dw - step, step):
                m.oriented_box('steel_dark', (x, y0, gz + 0.02), (x + step, y0 + 0.5, gz + 0.02), 0.015, 0.008)
                m.oriented_box('steel_dark', (x + step, y0, gz + 0.02), (x, y0 + 0.5, gz + 0.02), 0.015, 0.008)
        m.box('rust_dark', (-0.05, BOT + 1.0, gz + 0.02), (0.05, BOT + 1.12, gz + 0.06))           # the padlock
    # what the wind brought down: leaves drifted on the steps and the landing, a puddle, a fallen globe
    m.box('water', (-hw + 0.05, BOT, -L + 0.1), (hw - 0.2, BOT + 0.012, zl - 0.2), faces='Y')
    for k in range(1, N):
        if rng.random() < 0.45:
            z0 = -0.05 - (k - 1) * RUN
            side = rng.choice((-1, 1))
            w = rng.uniform(0.2, 0.55)
            x0, x1 = (hw - w, hw) if side > 0 else (-hw, -hw + w)
            m.box('ground_leaves', (x0, -k * RISE, z0 - RUN), (x1, -k * RISE + 0.012, z0), faces='Y')
    m.box('ground_leaves', (-hw, BOT, zl - 0.6), (hw, BOT + 0.02, zl), faces='Y')
    m.box('ground_leaves', (-hw, BOT, -L), (-0.1, BOT + 0.025, -L + 0.5), faces='Y')
    # the railings: posts, a top rail and a bottom rail, round bars between; one bar kicked out here and there
    rx = hw + 0.13
    H = 1.05
    runs = [((-rx, -0.05), (-rx, -L - 0.12)), ((rx, -0.05), (rx, -L - 0.12)), ((-rx, -L - 0.12), (rx, -L - 0.12))]
    for (ax, az), (bx, bz) in runs:
        a, b = np.array([ax, 0.0, az]), np.array([bx, 0.0, bz])
        L_ = float(np.linalg.norm(b - a))
        d = (b - a) / L_
        npost = max(2, int(round(L_ / 1.75)) + 1)
        for t in np.linspace(0, L_, npost):
            p = a + d * t
            m.cylinder('subway_green', (p[0], base, p[2]), (p[0], base + H + 0.05, p[2]), 0.03, 6)
            if lod == 0:
                m.cylinder('subway_green', (p[0], base + H + 0.05, p[2]), (p[0], base + H + 0.11, p[2]), 0.045, 6)
        for y in (base + 0.12, base + H):
            m.oriented_box('subway_green', (a[0], y, a[2]), (b[0], y, b[2]), 0.04, 0.035)
        if lod == 0:
            for t in np.arange(0.12, L_ - 0.05, 0.13):
                if rng.random() < 0.05:
                    continue
                p = a + d * t
                m.box('subway_green', (p[0] - 0.009, base + 0.12, p[2] - 0.009), (p[0] + 0.009, base + H, p[2] + 0.009),
                      faces='xXzZ')
    # the two lamp posts at the mouth: fluted green cast iron, glass globes (one broken, its glass on the steps)
    for s in (-1, 1):
        x, z = s * rx, -0.05
        m.cylinder('subway_green', (x, base, z), (x, base + 0.25, z), 0.09, 8, r2=0.075)
        m.cylinder('subway_green', (x, base + 0.25, z), (x, base + 2.15, z), 0.055, 8 if lod == 0 else 6, r2=0.04)
        m.cylinder('subway_green', (x, base + 2.15, z), (x, base + 2.3, z), 0.08, 8, r2=0.05)
        _globe(m, 'globe_green', (x, base + 2.48, z), 0.19, seg=10 if lod == 0 else 6, broken=(s == broken_globe))
    if broken_globe and lod == 0:
        for k in range(7):
            gx, gz2 = rng.uniform(-0.6, 0.6), -0.05 - rng.integers(1, 6) * RUN + 0.1
            y = -round((-0.05 - gz2) / RUN + 0.5) * RISE
            m.push((gx, y + 0.006, gz2), rot_y(rng.uniform(0, 360)))
            m.box('globe_green', (-0.05, 0, -0.03), (0.05, 0.004, 0.03), faces='Y')
            m.pop()
    m.stash = (0.35, BOT + 0.02, -L + 0.45)
    return m


# ---------------------------------------------------------------------------------------------------- tree pits
def tree_pit(seed=1, guard=True, broken=False):
    """A square of earth cut in the pavement for a street tree, a granite edge, and the low iron guard the block
    association put round it (broken: one side flattened by a car)."""
    rng = np.random.default_rng(seed)
    m = _mesh()
    hx, hz = 0.9, 0.6
    m.box('ground_dirt', (-hx, -0.1, -hz), (hx, 0.02, hz), faces='Y')
    for (x0, z0, x1, z1) in ((-hx - 0.1, -hz - 0.1, hx + 0.1, -hz), (-hx - 0.1, hz, hx + 0.1, hz + 0.1),
                             (-hx - 0.1, -hz, -hx, hz), (hx, -hz, hx + 0.1, hz)):
        m.box('granite', (x0, -0.1, z0), (x1, 0.045, z1), faces='xXYzZ')
    if not guard:
        return m
    gh = 0.46
    sides = [((-hx, -hz), (hx, -hz)), ((hx, -hz), (hx, hz)), ((hx, hz), (-hx, hz)), ((-hx, hz), (-hx, -hz))]
    flat = rng.integers(0, 4) if broken else -1
    for i, ((ax, az), (bx, bz)) in enumerate(sides):
        a, b = np.array([ax, 0.0, az]), np.array([bx, 0.0, bz])
        d = b - a
        L_ = float(np.linalg.norm(d))
        mid = (a + b) / 2
        if i == flat:                                            # pushed over into the pit, lying almost flat
            m.push((ax, 0.03, az), rot_y(math.degrees(math.atan2(d[0], d[2])) - 90) @ rot_x(-78))
            a, b = np.array([0.0, 0.0, 0.0]), np.array([L_, 0.0, 0.0])
            d = b - a
        else:
            m.push((0, 0, 0))
        for y in (0.1, gh):
            m.oriented_box('iron_black', (a[0], y, a[2]), (b[0], y, b[2]), 0.025, 0.025)
        for t in np.arange(0.0, L_ + 0.01, 0.15):
            p = a + d / L_ * t
            top = gh + (0.06 if abs(t - L_ / 2) < L_ / 2 - 0.05 else 0.0) * math.sin(math.pi * t / L_)
            m.box('iron_black', (p[0] - 0.008, 0.0, p[2] - 0.008), (p[0] + 0.008, top + 0.02, p[2] + 0.008), faces='xXzZ')
        m.pop()
    return m


# --------------------------------------------------------------------------------------------- the small things
def mailbox(seed=1, tipped=False):
    """The blue collection box on four short legs, its top a half barrel, the chute handle on the front."""
    rng = np.random.default_rng(seed)
    m = _mesh()
    if tipped:                                                   # knocked over onto its side
        m.push((0, 0.265, 0), rot_z(rng.uniform(86, 94)) @ rot_x(rng.uniform(-6, 6)))
    else:
        m.push((0, 0, 0))
    w, d = 0.265, 0.26
    for sx in (-1, 1):
        for sz in (-1, 1):
            m.box_c('mail_blue', (sx * (w - 0.03), 0.11, sz * (d - 0.03)), (0.06, 0.22, 0.06), faces='xXzZ')
    m.box('mail_blue', (-w, 0.22, -d), (w, 1.0, d))
    m.cylinder('mail_blue', (0, 1.0, -d), (0, 1.0, d), w, 10)
    m.push((0, 0.95, d), rot_x(-25))
    m.box('mail_blue', (-0.19, 0.0, 0.0), (0.19, 0.17, 0.06))                       # the pull-down chute
    m.box('steel_dark', (-0.12, 0.13, 0.06), (0.12, 0.155, 0.08))
    m.pop()
    m.box('news_white', (-0.15, 0.55, d), (0.15, 0.72, d + 0.004), faces='Z')     # the collection times, unreadable
    m.pop()
    return m


NEWS = ('news_red', 'news_yellow', 'news_blue', 'news_green', 'news_white')


def news_boxes(n=3, seed=1):
    """A row of coin newspaper boxes chained to each other, along X; the window shows yellowed paper or nothing,
    one box knocked over on its back."""
    rng = np.random.default_rng(seed)
    m = _mesh()
    fallen = rng.integers(0, n) if n > 2 else -1
    x = -(n - 1) * 0.5 / 2
    for i in range(n):
        mat = NEWS[rng.integers(0, len(NEWS))]
        if i == fallen:
            m.push((x + rng.uniform(-0.1, 0.1), 0.0, 0.55), rot_y(rng.uniform(-25, 25)) @ rot_x(90))
            m.push((0, 0, -0.2))
        else:
            m.push((x, 0.0, rng.uniform(-0.03, 0.03)), rot_y(rng.uniform(-4, 4)))
            m.push((0, 0, 0))
        m.box(mat, (-0.21, 0.0, -0.2), (-0.17, 0.3, -0.16), faces='xXzZ')               # the legs
        m.box(mat, (0.17, 0.0, -0.2), (0.21, 0.3, -0.16), faces='xXzZ')
        m.box(mat, (-0.21, 0.0, 0.16), (-0.17, 0.3, 0.2), faces='xXzZ')
        m.box(mat, (0.17, 0.0, 0.16), (0.21, 0.3, 0.2), faces='xXzZ')
        m.box(mat, (-0.21, 0.3, -0.2), (0.21, 1.0, 0.2))
        m.box('glass', (-0.15, 0.62, 0.2), (0.15, 0.93, 0.205), faces='Z')
        if rng.random() < 0.6:
            m.box('poster', (-0.13, 0.64, 0.17), (0.13, 0.9, 0.19), faces='Z')
        m.box('steel_dark', (0.06, 0.48, 0.2), (0.17, 0.58, 0.23))                    # the coin slot
        m.box('steel_dark', (-0.18, 0.95, 0.2), (0.18, 0.99, 0.25))                   # the handle
        m.pop()
        m.pop()
        x += 0.5
    return m


def parking_meter(heads=1, seed=1, lean=0.0, headless=False):
    """A post with one or two clockwork meter heads, the domes scratched blind."""
    rng = np.random.default_rng(seed)
    m = _mesh()
    m.push((0, 0, 0), rot_z(lean) @ rot_x(rng.uniform(-3, 3)))
    m.cylinder('steel_dark', (0, 0, 0), (0, 1.08, 0), 0.035, 6)
    if headless:
        m.cylinder('rust_dark', (0, 1.08, 0), (0, 1.12, 0), 0.05, 6)
        m.pop()
        return m
    offs = (0.0,) if heads == 1 else (-0.13, 0.13)
    if heads == 2:
        m.box('steel_dark', (-0.15, 1.06, -0.04), (0.15, 1.12, 0.04))
    for ox in offs:
        m.box('steel_dark', (ox - 0.08, 1.1, -0.07), (ox + 0.08, 1.36, 0.07))
        m.cylinder('steel_dark', (ox, 1.36, -0.07), (ox, 1.36, 0.07), 0.08, 8)
        m.box('glass', (ox - 0.06, 1.3, 0.07), (ox + 0.06, 1.42, 0.08), faces='Z')
        m.box('interior_dark', (ox - 0.05, 1.31, 0.065), (ox + 0.05, 1.41, 0.07), faces='Z')
        m.box('rust_dark', (ox - 0.02, 1.18, 0.07), (ox + 0.02, 1.24, 0.1))
    m.pop()
    return m


def fire_alarm_box(seed=1):
    """The red cast-iron pillar of a fire alarm call box: fluted shaft, the box with its pull door, a thin post
    above with a red glass globe so the firemen could find it at night."""
    rng = np.random.default_rng(seed)
    m = _mesh()
    m.cylinder('alarm_red', (0, 0, 0), (0, 0.12, 0), 0.17, 10, r2=0.15)
    m.cylinder('alarm_red', (0, 0.12, 0), (0, 0.3, 0), 0.13, 10, r2=0.1)
    m.cylinder('alarm_red', (0, 0.3, 0), (0, 1.12, 0), 0.08, 12, r2=0.07)
    m.cylinder('alarm_red', (0, 1.12, 0), (0, 1.18, 0), 0.13, 10)
    m.box('alarm_red', (-0.15, 1.18, -0.1), (0.15, 1.56, 0.12))
    m.cylinder('alarm_red', (0, 1.56, -0.1), (0, 1.56, 0.12), 0.15, 8)
    m.box('alarm_red', (-0.11, 1.24, 0.12), (0.11, 1.5, 0.15))                          # the door
    m.box('news_white', (-0.07, 1.42, 0.15), (0.07, 1.47, 0.153), faces='Z')
    m.box('steel_dark', (-0.02, 1.3, 0.15), (0.02, 1.36, 0.19))                          # the pull handle
    m.cylinder('alarm_red', (0, 1.68, 0), (0, 2.3, 0), 0.03, 6)
    m.cylinder('alarm_red', (0, 2.3, 0), (0, 2.36, 0), 0.07, 8)
    _globe(m, 'globe_red', (0, 2.48, 0), 0.11, seg=8, broken=rng.random() < 0.5)
    return m


def phone_pedestal(seed=1):
    """A steel payphone pedestal with two phones back to back under hoods; the handsets cut off, the cords hang."""
    rng = np.random.default_rng(seed)
    m = _mesh()
    m.box('steel_dark', (-0.07, 0.0, -0.07), (0.07, 2.25, 0.07))
    m.box('alu', (-0.38, 2.05, -0.42), (0.38, 2.3, 0.42))
    m.box('poster', (-0.36, 2.08, 0.42), (0.36, 2.27, 0.425), faces='Z')
    m.box('poster', (-0.36, 2.08, -0.425), (0.36, 2.27, -0.42), faces='z')
    for s in (-1, 1):
        m.push((0, 0, 0), rot_y(0 if s > 0 else 180))
        m.box('alu', (-0.33, 0.95, 0.07), (0.33, 1.0, 0.42))                            # shelf
        m.box('alu', (-0.33, 1.0, 0.07), (-0.3, 1.85, 0.42), faces='xXZ')               # side hoods
        m.box('alu', (0.3, 1.0, 0.07), (0.33, 1.85, 0.42), faces='xXZ')
        m.box('alu', (-0.33, 1.85, 0.07), (0.33, 1.9, 0.42))
        m.box('steel_dark', (-0.14, 1.1, 0.07), (0.14, 1.65, 0.17))                     # the phone
        m.box('interior_dark', (-0.09, 1.38, 0.17), (0.09, 1.56, 0.175), faces='Z')
        cx = rng.uniform(-0.1, 0.1)
        m.cylinder('rubber', (0.12, 1.25, 0.15), (0.12 + cx, 0.7, 0.2), 0.008, 4, caps=False)
        m.pop()
    return m


def bus_shelter(seed=1, lod=0):
    """A shelter of steel posts and glass, the roof a thin slab, a bench along the back, an ad box at one end; most
    of the glass is gone, its crumbs still on the pavement."""
    rng = np.random.default_rng(seed)
    m = _mesh()
    hx, z0, z1, H = 2.2, -0.75, 0.6, 2.4
    for x in (-hx, -hx / 3, hx / 3, hx):
        m.box('steel_dark', (x - 0.04, 0.0, z0 - 0.04), (x + 0.04, H, z0 + 0.04))
    for x in (-hx, hx):
        m.box('steel_dark', (x - 0.04, 0.0, z1 - 0.04), (x + 0.04, H, z1 + 0.04))
    m.box('steel_dark', (-hx - 0.15, H, z0 - 0.15), (hx + 0.15, H + 0.12, z1 + 0.2))
    m.box('alu', (-hx - 0.16, H + 0.03, z1 + 0.2), (hx + 0.16, H + 0.12, z1 + 0.21), faces='Z')
    # back glass, three panes, two broken out
    xs = (-hx, -hx / 3, hx / 3, hx)
    gone = set(rng.choice(3, size=2, replace=False).tolist())
    for i in range(3):
        if i in gone:
            m.box('steel_dark', (xs[i], 0.0, z0 - 0.02), (xs[i + 1], 0.06, z0 + 0.02))
            continue
        m.box('glass', (xs[i] + 0.04, 0.06, z0 - 0.006), (xs[i + 1] - 0.04, H - 0.05, z0 + 0.006), faces='zZ')
    # the end panels: glass at one end, the lit ad box (long dark) at the other
    m.box('glass', (-hx - 0.006, 0.3, z0 + 0.04), (-hx + 0.006, H - 0.05, z1 - 0.04), faces='xX')
    m.box('steel_dark', (hx - 0.08, 0.15, z0 + 0.04), (hx + 0.08, H - 0.1, z1 - 0.04))
    m.box('poster', (hx + 0.08, 0.35, z0 + 0.12), (hx + 0.085, H - 0.3, z1 - 0.12), faces='X')
    m.box('interior_dark', (hx - 0.085, 0.35, z0 + 0.12), (hx - 0.08, H - 0.3, z1 - 0.12), faces='x')
    # the bench
    m.box('steel_dark', (-1.4, 0.42, z0 + 0.05), (1.0, 0.47, z0 + 0.42))
    for x in (-1.3, 0.9):
        m.box('steel_dark', (x - 0.03, 0.0, z0 + 0.2), (x + 0.03, 0.42, z0 + 0.26), faces='xXzZ')
    # glass crumbs on the pavement in front of the broken panes
    if lod == 0:
        for i in gone:
            for k in range(10):
                x, z = rng.uniform(xs[i], xs[i + 1]), z0 + rng.uniform(-0.5, 0.9)
                m.push((x, 0.006, z), rot_y(rng.uniform(0, 360)))
                m.box('glass', (-0.04, 0, -0.03), (0.04, 0.004, 0.03), faces='Y')
                m.pop()
    return m


# ------------------------------------------------------------------------------------------------- Little Italy
def _bulb(m, mat, p, r=0.034):
    """A light bulb as a small octahedron (8 triangles), long dead."""
    x, y, z = p
    t, b = (x, y + r, z), (x, y - r * 1.3, z)
    ring = [(x + r, y, z), (x, y, z + r), (x - r, y, z), (x, y, z - r)]
    for i in range(4):
        a, c = ring[i], ring[(i + 1) % 4]
        m.poly(mat, [a, t, c][::-1])
        m.poly(mat, [a, c, b][::-1])


def _curve(m, mat, pts, w, bulbs=None, step=0.3, bulb_mats=('bulb',), rng=None, gone=0.15):
    """A bent tube through pts (x, y) at z = 0, bulbs every `step` along it, some gone."""
    acc = 0.0
    for (x0, y0), (x1, y1) in zip(pts, pts[1:]):
        m.oriented_box(mat, (x0, y0, 0.0), (x1, y1, 0.0), w, w)
        if bulbs is None:
            continue
        L = math.hypot(x1 - x0, y1 - y0)
        while acc < L:
            t = acc / L
            if rng.random() > gone:
                bulbs.append((bulb_mats[rng.integers(0, len(bulb_mats))], (x0 + (x1 - x0) * t, y0 + (y1 - y0) * t, 0.05)))
            acc += step
        acc -= L


def feast_arch(lod=0, seed=1, span=10.0):
    """The festival arches of Mulberry Street: a white tube frame from kerb to kerb, a pointed arch over the street
    with scrolls and a crest, studded with bulbs. Nobody switched them on for twenty years. Poles at x = +-span/2."""
    rng = np.random.default_rng(seed)
    m = _mesh()
    hx = span / 2
    bulbs = [] if lod == 0 else None
    bm = ('bulb', 'bulb', 'bulb', 'bulb_red', 'bulb_green')
    n = 14 if lod == 0 else 7
    for s in (-1, 1):
        m.cylinder('arch_white', (s * hx, 0.0, 0.0), (s * hx, 5.4, 0.0), 0.06, 6)
        m.cylinder('arch_white', (s * hx, 0.0, 0.0), (s * hx, 0.35, 0.0), 0.12, 6, r2=0.07)
        _curve(m, 'arch_white', [(s * hx, 2.6), (s * hx, 5.3)], 0.04, bulbs, 0.3, bm, rng)
    # the outer arch, pointed: two arcs meeting at the crest
    outer = [(hx * math.cos(t), 5.3 + 2.3 * math.sin(t) ** 0.8) for t in np.linspace(0, math.pi, 2 * n + 1)]
    inner = [(0.78 * hx * math.cos(t), 5.0 + 1.6 * math.sin(t) ** 0.8) for t in np.linspace(0, math.pi, 2 * n + 1)]
    _curve(m, 'arch_white', outer, 0.05, bulbs, 0.28, bm, rng)
    _curve(m, 'arch_white', inner, 0.04, bulbs, 0.28, bm, rng)
    _curve(m, 'arch_white', [(-hx, 5.0), (hx, 5.0)], 0.04, bulbs, 0.32, bm, rng)
    # struts between the arcs, scrolls in the spandrels, a ring with a star in the crest
    for i in range(0, 2 * n + 1, 2 if lod == 0 else 4):
        m.oriented_box('arch_white', (outer[i][0], outer[i][1], 0), (inner[i][0], inner[i][1], 0), 0.03, 0.03)
    if lod == 0:
        for sx in (-1, 1):
            for cx, cy, r in ((sx * 0.5 * hx, 5.45, 0.32), (sx * 0.2 * hx, 5.35, 0.22)):
                ring = [(cx + r * math.cos(t), cy + r * math.sin(t)) for t in np.linspace(0, 2 * math.pi, 11)]
                _curve(m, 'arch_white', ring, 0.03, bulbs, 0.25, bm, rng)
    cy = 7.6 + 0.62
    ring = [(0.62 * math.cos(t), cy + 0.62 * math.sin(t)) for t in np.linspace(0, 2 * math.pi, 13 if lod == 0 else 7)]
    _curve(m, 'arch_white', ring, 0.04, bulbs, 0.25, bm, rng)
    for k in range(5 if lod == 0 else 0):
        a = math.pi / 2 + 2 * math.pi * k / 5
        _curve(m, 'arch_white', [(0, cy), (0.55 * math.cos(a), cy + 0.55 * math.sin(a))], 0.025, bulbs, 0.2, ('bulb_red',), rng)
    for mat, p in (bulbs or []):
        _bulb(m, mat, p)
        _bulb(m, mat, (p[0], p[1], -0.1))
    return m


def _chair(m, rng, x, z, yaw, fallen=False):
    m.push((x, 0.0, z), rot_y(yaw) @ (rot_x(-85) if fallen else np.eye(3)))
    if fallen:
        m.push((0, 0.2, 0))
    else:
        m.push((0, 0, 0))
    for lx in (-0.18, 0.18):
        for lz in (-0.18, 0.18):
            m.box('steel_dark', (lx - 0.012, 0.0, lz - 0.012), (lx + 0.012, 0.45, lz + 0.012), faces='xXzZ')
    m.box('wood_dark', (-0.21, 0.45, -0.21), (0.21, 0.48, 0.21))
    m.box('steel_dark', (-0.19, 0.48, -0.21), (-0.17, 0.9, -0.19), faces='xXzZ')
    m.box('steel_dark', (0.17, 0.48, -0.21), (0.19, 0.9, -0.19), faces='xXzZ')
    m.box('wood_dark', (-0.19, 0.72, -0.22), (0.19, 0.88, -0.2))
    m.pop()
    m.pop()


def cafe_terrace(length=4.4, seed=1):
    """A restaurant's sidewalk cafe in front of its shop: planter boxes round it (the plants long dead), bistro
    tables, chairs knocked over, a folded umbrella, its canvas rotted. Back against the house front at z = 0, it
    takes 1.9 m of the pavement toward +Z; the way in is at the -X end."""
    rng = np.random.default_rng(seed)
    m = _mesh()
    hx, D = length / 2, 1.9

    def planter(x0, z0, x1, z1):
        m.box('wood', (x0, 0.0, z0), (x1, 0.5, z1))
        m.box('ground_dirt', (x0 + 0.04, 0.5, z0 + 0.04), (x1 - 0.04, 0.505, z1 - 0.04), faces='Y')
        for _ in range(int(max(x1 - x0, z1 - z0) / 0.35)):
            px, pz = rng.uniform(x0 + 0.08, x1 - 0.08), rng.uniform(z0 + 0.08, z1 - 0.08)
            h = rng.uniform(0.4, 1.0)
            m.cylinder('bark_dead', (px, 0.5, pz), (px + rng.uniform(-0.15, 0.15), 0.5 + h, pz + rng.uniform(-0.15, 0.15)), 0.012, 3)
    planter(-hx + 0.9, D - 0.35, hx, D)
    planter(hx - 0.35, 0.2, hx, D - 0.35)
    m.box('steel_dark', (-hx, 0.0, D - 0.05), (-hx + 0.9, 0.04, D))           # where the gate was
    n = max(1, int(length / 1.4))
    for i in range(n):
        x = -hx + 0.7 + (length - 1.2) * (i + 0.5) / n
        z = rng.uniform(0.75, 1.1)
        if rng.random() < 0.25:                                                 # a table on its side
            m.push((x, 0.36, z), rot_y(rng.uniform(0, 360)) @ rot_z(88))
            m.cylinder('steel_dark', (0, -0.05, 0), (0, 0.0, 0), 0.36, 10)
            m.cylinder('steel_dark', (0, 0.0, 0), (0, 0.72, 0), 0.025, 6)
            m.cylinder('steel_dark', (0, 0.68, 0), (0, 0.72, 0), 0.22, 8)
            m.pop()
        else:
            m.cylinder('steel_dark', (x, 0.0, z), (x, 0.03, z), 0.22, 8)
            m.cylinder('steel_dark', (x, 0.03, z), (x, 0.72, z), 0.025, 6)
            m.cylinder('alu', (x, 0.72, z), (x, 0.75, z), 0.36, 10)
        for a in (rng.uniform(-30, 30), 180 + rng.uniform(-30, 30)):
            cx, cz = x + 0.55 * math.sin(math.radians(a + 90)), z + 0.55 * math.cos(math.radians(a + 90))
            _chair(m, rng, cx, cz, a - 90, fallen=rng.random() < 0.35)
    # the folded umbrella in its stand
    ux, uz = hx - 0.7, 0.45
    m.cylinder('steel_dark', (ux, 0.0, uz), (ux, 0.08, uz), 0.2, 8)
    m.cylinder('alu', (ux, 0.0, uz), (ux, 2.5, uz), 0.02, 5)
    m.cylinder('canvas_green' if seed % 2 else 'canvas', (ux, 1.35, uz), (ux, 2.4, uz), 0.12, 6, r2=0.03)
    return m


MODELS = {
    'subway_entrance': lambda lod=0: subway_entrance(lod, seed=1, broken_globe=1),
    'subway_entrance_b': lambda lod=0: subway_entrance(lod, seed=2, broken_globe=0),
    'tree_pit': lambda lod=0: tree_pit(1),
    'tree_pit_broken': lambda lod=0: tree_pit(2, broken=True),
    'tree_pit_bare': lambda lod=0: tree_pit(3, guard=False),
    'mailbox': lambda lod=0: mailbox(1),
    'mailbox_tipped': lambda lod=0: mailbox(2, tipped=True),
    'news_boxes_3': lambda lod=0: news_boxes(3, 1),
    'news_boxes_4': lambda lod=0: news_boxes(4, 2),
    'parking_meter': lambda lod=0: parking_meter(1, 1),
    'parking_meter_twin': lambda lod=0: parking_meter(2, 2),
    'parking_meter_bent': lambda lod=0: parking_meter(1, 3, lean=24),
    'parking_meter_headless': lambda lod=0: parking_meter(1, 4, headless=True),
    'fire_alarm_box': lambda lod=0: fire_alarm_box(1),
    'phone_pedestal': lambda lod=0: phone_pedestal(1),
    'bus_shelter': lambda lod=0: bus_shelter(1, lod),
    'feast_arch': lambda lod=0: feast_arch(lod, 1),
    'feast_arch_b': lambda lod=0: feast_arch(lod, 2),
    'cafe_terrace_3m': lambda lod=0: cafe_terrace(3.2, 1),
    'cafe_terrace_4m': lambda lod=0: cafe_terrace(4.4, 2),
    'cafe_terrace_4m_b': lambda lod=0: cafe_terrace(4.4, 3),
}
LODS = ('subway_entrance', 'subway_entrance_b', 'bus_shelter', 'feast_arch', 'feast_arch_b')
