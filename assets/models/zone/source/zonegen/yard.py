"""Back yards and vacant lots of Mulberry Street (original, CC0): fences, hoardings, clotheslines.

Frame of each model: it runs along X centred on 0, its street (or front) side faces +Z, y = 0 is the ground.
"""
import numpy as np

from .mats import PALETTE
import math

from .mesh import Mesh, rot_x, rot_y, rot_z


def hoarding(length=7.6, seed=1, gap=(0.8, 2.4)):
    """The fence the city put round a vacant lot: corrugated sheets and plywood on timber posts, 2.4 m, sprayed
    over, a gap kicked in (gap: from / to as a fraction of nothing: metres from the left end) where people go in."""
    rng = np.random.default_rng(seed)
    m = Mesh(PALETTE)
    hx = length / 2
    x = -hx
    g0, g1 = -hx + gap[0], -hx + gap[1]
    while x < hx - 0.05:
        w = min(rng.uniform(0.9, 1.25), hx - x)
        if not (g0 < x + w / 2 < g1):
            mat = 'corr_rust' if rng.random() < 0.6 else 'wood'
            lean = rng.uniform(-4, 4)
            m.push((x + w / 2, 0.0, 0.0), rot_x(lean))
            m.box(mat, (-w / 2, 0.02, -0.02), (w / 2, rng.uniform(2.1, 2.45), 0.02))
            m.pop()
        x += w
    for px in np.arange(-hx + 0.05, hx, 2.4):
        if not (g0 + 0.2 < px < g1 - 0.2):
            m.box('wood_dark', (px - 0.05, 0.0, -0.14), (px + 0.05, 2.3, -0.04))
    for y in (0.4, 1.9):
        for a, b in ((-hx, g0), (g1, hx)):
            if b - a > 0.2:
                m.box('wood_dark', (a, y, -0.1), (b, y + 0.08, -0.03))
    # the kicked-in sheet lying in the gap
    m.push(((g0 + g1) / 2, 0.06, 0.6), rot_y(rng.uniform(-20, 20)) @ rot_x(-84))
    m.box('corr_rust', (-0.55, 0.0, -0.01), (0.55, 2.2, 0.01))
    m.pop()
    return m


def board_fence(length=6.0, seed=2, broken=False):
    """A yard fence of grey boards on rails between two lots, some boards gone (broken: a whole stretch down)."""
    rng = np.random.default_rng(seed)
    m = Mesh(PALETTE)
    hx = length / 2
    x = -hx
    while x < hx - 0.05:
        w = 0.15
        gone = rng.random() < (0.12 if not broken else 0.05) or (broken and -0.8 < x < 0.9)
        if not gone:
            m.push((x + w / 2, 0.0, 0.0), rot_x(rng.uniform(-3, 3)))
            m.box('wood', (-w / 2 + 0.01, 0.0, -0.012), (w / 2 - 0.01, rng.uniform(1.75, 1.9), 0.012))
            m.pop()
        x += w
    for px in np.arange(-hx + 0.05, hx, 2.0):
        m.box('wood_dark', (px - 0.05, 0.0, -0.1), (px + 0.05, 1.95, -0.012))
    for y in (0.35, 1.5):
        m.box('wood_dark', (-hx, y, -0.07), (hx, y + 0.07, -0.012))
    if broken:                                               # the boards that fell, flat in the grass
        for k in range(6):
            m.push((rng.uniform(-0.8, 0.9), 0.03, rng.uniform(0.3, 1.6)), rot_y(rng.uniform(-30, 30)) @ rot_x(-90))
            m.box('wood', (-0.07, 0.0, -0.012), (0.07, 1.8, 0.012))
            m.pop()
    return m


def clothesline(span=9.0, seed=3):
    """A line across a yard from wall to wall at the second floor, rags still pegged on it (sags in the middle)."""
    rng = np.random.default_rng(seed)
    m = Mesh(PALETTE)
    pts = [(-span / 2 + span * t, -0.9 * (1 - (2 * t - 1) ** 2), 0.0) for t in np.linspace(0, 1, 9)]
    for a, b in zip(pts, pts[1:]):
        m.cylinder('iron_black', a, b, 0.006, 3, caps=False)
    for k in range(4):
        t = rng.uniform(0.15, 0.85)
        x, y = -span / 2 + span * t, -0.9 * (1 - (2 * t - 1) ** 2)
        m.box('paint_cream' if k % 2 else 'paint_blue', (x - 0.3, y - rng.uniform(0.4, 0.8), -0.01), (x + 0.3, y, 0.01))
    return m


def cobra_lamp(lean=0.0, lod=0):
    """The New York street light: a tapered grey steel pole, 9 m, on a cast-iron base, a mast arm curving out over
    the road (toward +X) and a cobra-head luminaire, its lens long dead. lean: degrees, the pole hit by a car."""
    m = Mesh(PALETTE)
    m.push((0, 0, 0), rot_z(-lean))
    m.cylinder('iron_black', (0, 0, 0), (0, 0.25, 0), 0.26, 8, r2=0.25)                 # the cast base and its collar
    m.cylinder('iron_black', (0, 0.25, 0), (0, 1.3, 0), 0.22, 8, r2=0.16)
    m.cylinder('iron_black', (0, 1.3, 0), (0, 1.42, 0), 0.19, 8)
    m.box('rust_dark', (-0.1, 0.45, 0.16), (0.1, 0.75, 0.2))                          # the hand hole, its cover gone
    m.cylinder('metal_plate', (0, 1.42, 0), (0, 8.3, 0), 0.13, 8, caps=False, r2=0.085)
    # the mast arm: a quarter curve out of the pole top, then a long gentle rise
    pts = [(0.0, 7.6)] + [(1.2 * (1 - math.cos(a)), 7.6 + 1.2 * math.sin(a)) for a in np.linspace(0.25, math.pi / 2, 5 if lod == 0 else 3)]
    pts.append((3.1, 8.95))
    for (x0, y0), (x1, y1) in zip(pts, pts[1:]):
        m.cylinder('metal_plate', (x0, y0, 0), (x1, y1, 0), 0.06, 6, caps=False)
    m.cylinder('metal_plate', (0, 8.3, 0), (0, 8.5, 0), 0.085, 8, r2=0.03)
    if lod == 0:
        m.cylinder('metal_plate', (0.0, 7.0, 0), (0.6, 7.75, 0), 0.03, 4, caps=False)         # the brace
    # the cobra head: a long rounded housing, lens underneath
    hx = 3.1
    m.cylinder('metal_plate', (hx - 0.1, 8.97, 0), (hx + 0.85, 8.9, 0), 0.2, 8 if lod == 0 else 6, r2=0.12)
    m.box('interior_dark', (hx + 0.05, 8.72, -0.15), (hx + 0.8, 8.78, 0.15))
    m.pop()
    return m


MODELS = {
    'fence_lot_hoarding': lambda: hoarding(7.6, 1),
    'fence_board_6m': lambda: board_fence(6.0, 2),
    'fence_board_6m_broken': lambda: board_fence(6.0, 3, broken=True),
    'clothesline_9m': lambda: clothesline(9.0, 4),
    'lamp_cobra': lambda: cobra_lamp(),
    'lamp_cobra_leaning': lambda: cobra_lamp(lean=12),
}
