"""Soviet panel point towers ("tochka") for the city generator (original design, CC0).

A free-standing 12 to 24 storey tower of large concrete panels, as they stand around every post-Soviet town:
a grid of 3 m panels with a window each and visible joints, two stacks of recessed loggias on each long facade,
an entrance with a concrete canopy, a flat roof with a parapet and a lift machine room. Decades empty: most
windows black holes, some frames with shards, a few loggia parapets fallen, rain streaks under every sill and
from the roof edge (docs/LEVEL_ART_GUIDE.md).

Frame (engine/CityGen.h kit buildings): length along X centred on 0, the entrance facade facing +Z at z = depth / 2,
y = 0 the ground. Storey 2.8 m (panel series), ground floor 3.0 m. Not enterable: the entrance door is shut.
"""
import numpy as np

from .buildings import wall_x, slab, railing_x
from .mats import PALETTE
from .mesh import Mesh, rot_y
from .skrunda import _frame, facade_grime, grime

GROUND = 3.0
STOREY = 2.8
T = 0.35            # outer panel
PANEL = 3.0         # panel width along the facade


def _kind(rng, f):
    r = rng.random()
    if r < 0.1:
        return 'glass'
    if r < 0.48:
        return 'broken'
    if f == 0 and r < 0.75:
        return 'boarded'
    return 'empty'


def panel_tower(length=24.0, depth=18.0, floors=16, seed=0, lod=0, skin='panel'):
    rng = np.random.default_rng(seed)
    m = Mesh(PALETTE)
    hx, hz = length / 2, depth / 2
    inn = 'interior_dark'

    def floor_y(f):
        return 0.0 if f == 0 else GROUND + (f - 1) * STOREY

    top = floor_y(floors)
    # ---------------------------------------------------------------- slabs and the dark core (windows show rooms)
    for f in range(floors + 1):
        slab(m, (-hx + T, -hz + T, hx - T, hz - T), [], floor_y(f) + (0.02 if f == 0 else 0.0), 0.3 if f == 0 else 0.22,
             'roof_tar' if f == floors else 'flat_floor', inn, inn)
    m.box(inn, (-3.0, 0.0, -2.6), (3.0, top, 2.6))

    # ---------------------------------------------------------------- the four facades
    # side: (rotation, half length along the facade, distance of the face, wall runs full width?)
    sides = [('+z', 0, hx, hz, True), ('-z', 180, hx, hz, True), ('+x', 90, hz, hx, False), ('-x', -90, hz, hx, False)]
    for side, ang, half, face, full in sides:
        L = 2 * half
        nb = max(2, int(round(L / PANEL)))
        bay = L / nb
        bays = [-half + bay * (i + 0.5) for i in range(nb)]
        loggias = set()
        if full and nb >= 6:                       # long facades: two symmetric stacks of loggias
            k = max(1, nb // 4)
            loggias = {k, nb - 1 - k}
        entrance = nb // 2 if side == '+z' else None
        x0, x1 = (-half, half) if full else (-half + T, half - T)
        wins = []
        m.push((0, 0, 0), rot_y(ang))
        for f in range(floors):
            y0, y1 = floor_y(f), floor_y(f + 1)
            ops = []
            for i, c in enumerate(bays):
                if f == 0:
                    if i == entrance:
                        ops.append((c, 1.6, 0.0, 2.4))
                        wins.append((c, 1.6, 0.0, 2.4, f, 'door'))
                    elif i % 2 == 0:
                        ops.append((c, 1.2, 1.0, 2.3))
                        wins.append((c, 1.2, 1.0, 2.3, f, 'window'))
                    continue
                if i in loggias:
                    ops.append((c, bay - 0.4, y0 + 0.02, y0 + 2.55))
                    wins.append((c, bay - 0.4, y0, y0 + 2.55, f, 'loggia'))
                else:
                    ops.append((c, 1.5, y0 + 0.85, y0 + 2.3))
                    wins.append((c, 1.5, y0 + 0.85, y0 + 2.3, f, 'window'))
            if lod == 0:
                wall_x(m, x0, x1, face - T / 2, y0, y1, T, skin, inn, ops, out_side=1)
            else:                                   # far view: solid wall, openings as dark insets on the face
                m.box(skin, (x0, y0, face - T), (x1, y1, face), faces='Z')
                for (c, w, ya, yb) in ops:
                    m.box(inn, (c - w / 2, ya, face), (c + w / 2, yb, face + 0.02), faces='Z')
        # parapet on the roof
        m.box(skin, (x0, top, face - T), (x1, top + 1.0, face), faces='xXyYzZ')
        m.box('concrete', (x0 - 0.05, top + 1.0, face - T - 0.05), (x1 + 0.05, top + 1.08, face + 0.05))

        # loggias: recessed back wall with a door and a window, side walls, a concrete parapet panel in front
        for (c, w, ya, yb, f, kind) in wins:
            if kind != 'loggia' or lod:
                continue
            d = 1.3
            back = face - T - d
            m.box(inn, (c - w / 2, ya, back - 0.15), (c + w / 2, ya + 2.55, back), faces='Z')
            m.box(skin, (c - w / 2, ya, back), (c - w / 2 + 0.12, ya + 2.55, face - T), faces='X')
            m.box(skin, (c + w / 2 - 0.12, ya, back), (c + w / 2, ya + 2.55, face - T), faces='x')
            m.box('concrete', (c - w / 2, ya - 0.2, back), (c + w / 2, ya + 0.02, face - T), faces='Y')
            if lod == 0:
                m.box('plaster', (c - w / 2 + 0.12, ya + 0.02, back), (c + w / 2 - 0.12, ya + 2.53, back + 0.02), faces='Z')
                m.box(inn, (c - w / 2 + 0.5, ya + 0.02, back + 0.02), (c - w / 2 + 1.3, ya + 2.2, back + 0.04), faces='Z')   # door
                m.box(inn, (c - w / 2 + 1.5, ya + 0.9, back + 0.02), (c + w / 2 - 0.4, ya + 2.2, back + 0.04), faces='Z')    # window
            if rng.random() < 0.9:                  # parapet panel, some fell off long ago
                m.box(skin, (c - w / 2, ya, face - 0.12), (c + w / 2, ya + 1.05, face + 0.02))
                if lod == 0:
                    grime(m, c - w / 2, c + w / 2, ya, max(0.1, ya - rng.uniform(0.8, 2.2)), face + 0.03, 'sill',
                          int(rng.integers(0, 4)))
            elif lod == 0:
                railing_x(m, c - w / 2 + 0.1, c + w / 2 - 0.1, face - 0.05, ya, 1.0)

        if lod == 0:
            # windows and the entrance
            for (c, w, ya, yb, f, kind) in wins:
                if kind == 'window':
                    _frame(m, np.eye(3), c, face, ya, yb, w, rng, _kind(rng, f))
                elif kind == 'door':
                    m.box('st_metal_paint', (c - w / 2, 0.0, face - T + 0.05), (c + w / 2, yb, face - T + 0.11))
                    m.box('concrete', (c - 1.6, 2.6, face), (c + 1.6, 2.8, face + 1.6))                      # canopy
                    for sx in (-1, 1):
                        m.box('concrete', (c + sx * 1.45 - 0.1, 0.0, face + 1.35), (c + sx * 1.45 + 0.1, 2.6, face + 1.55))
                    m.box('concrete', (c - 1.8, -0.2, face), (c + 1.8, 0.15, face + 1.9))                   # steps
            # panel joints: the grid that makes a panel house read as one
            jx0, jx1 = (x0, x1)
            for f in range(1, floors + 1):
                y = floor_y(f)
                m.box(inn, (jx0, y - 0.02, face), (jx1, y + 0.02, face + 0.015), faces='yYZ')
            for i in range(1, nb):
                x = -half + bay * i
                m.box(inn, (x - 0.02, GROUND, face), (x + 0.02, top, face + 0.015), faces='xXZ')
            facade_grime(m, x0, x1, top + 0.9, face + 0.02, rng, plinth=(0.0, 1.2, face + 0.02))
        # plinth
        m.box('concrete', (x0 - 0.05, -0.6, face - 0.05), (x1 + 0.05, 0.5, face + 0.06), faces='xXYZ')
        m.pop()

    # ---------------------------------------------------------------- roof: machine room, vents, antennas
    m.box('concrete', (-3.2, top, -2.8), (3.2, top + 3.2, 2.8))
    m.box('roof_tar', (-3.4, top + 3.2, -3.0), (3.4, top + 3.35, 3.0))
    if lod == 0:
        for k in range(int(rng.integers(2, 5))):
            x, z = rng.uniform(-hx + 2, hx - 2), rng.uniform(-hz + 2, hz - 2)
            if abs(x) < 4 and abs(z) < 3.5:
                continue
            m.box('concrete', (x - 0.5, top, z - 0.5), (x + 0.5, top + 1.2, z + 0.5))
        for k in range(int(rng.integers(1, 4))):
            x, z = rng.uniform(-2.5, 2.5), rng.uniform(-2.0, 2.0)
            h = rng.uniform(2.5, 5.0)
            m.cylinder('rust_dark', (x, top + 3.35, z), (x, top + 3.35 + h, z), 0.04, 6, caps=False)
            m.cylinder('rust_dark', (x - 0.8, top + 3.0 + h, z), (x + 0.8, top + 3.0 + h, z), 0.025, 5, caps=False)
    return m
