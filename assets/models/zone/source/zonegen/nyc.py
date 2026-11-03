"""New York street furniture for the city generator (original low-poly designs, CC0).

water_tower(): the wooden roof tank of a Manhattan tenement on its steel stand, decades
abandoned: staves gone grey, iron hoops rusted, a loose hoop, the conical roof sometimes
fallen in, a ladder up the side. Origin at the centre of the stand's feet (on the roof).
hydrant(): a squat cast-iron fire hydrant, red paint flaking to rust. Origin at its foot."""
import math

import numpy as np

from .mats import PALETTE
from .mesh import Mesh


def water_tower(seed=0, lod=0, broken=False):
    rng = np.random.default_rng(seed)
    m = Mesh(PALETTE)
    seg = 20 if lod == 0 else 10
    R = rng.uniform(1.9, 2.3)          # tank radius
    stand = rng.uniform(3.0, 3.8)      # height of the platform deck
    tank_h = rng.uniform(4.0, 4.8)
    lean = math.radians(rng.uniform(-1.5, 1.5)) if not broken else math.radians(rng.uniform(4.0, 7.0))
    # Legs: six rusty steel posts round a ring, with cross bracing; the platform deck on top.
    legs = 6
    lr = R * 0.82
    tops = []
    for i in range(legs):
        a = 2 * math.pi * i / legs
        foot = (math.cos(a) * lr * 1.05, 0.0, math.sin(a) * lr * 1.05)
        top = (math.cos(a) * lr + math.sin(lean) * stand, stand, math.sin(a) * lr)
        m.oriented_box('iron_black', foot, top, 0.2, 0.2)
        m.box_c('rust_dark', foot, (0.45, 0.06, 0.45))          # base plate
        tops.append(top)
    if lod == 0:
        for i in range(legs):
            a, b = tops[i], tops[(i + 1) % legs]
            fa = (a[0] * 1.03 - math.sin(lean) * stand, 0.4, a[2] * 1.03)
            fb = (b[0] * 1.03 - math.sin(lean) * stand, 0.4, b[2] * 1.03)
            if rng.random() < 0.85:
                m.oriented_box('rust_dark', fa, b, 0.05, 0.05)    # diagonal brace
            m.oriented_box('rust_dark', (a[0], stand * 0.55, a[2]), (b[0], stand * 0.55, b[2]), 0.06, 0.06)
    dx = math.sin(lean) * stand
    m.push((dx, stand, 0.0))
    # Deck: steel beams and a plank floor a little wider than the tank, a rail round it.
    m.cylinder('rust_dark', (0, -0.25, 0), (0, 0.0, 0), R + 0.45, seg)
    if lod == 0:
        for i in range(12):
            a = 2 * math.pi * i / 12
            p = (math.cos(a) * (R + 0.4), 0.0, math.sin(a) * (R + 0.4))
            if rng.random() < 0.8:
                m.box_c('iron_black', (p[0], 0.5, p[2]), (0.05, 1.0, 0.05))
        m.lathe('iron_black', [(R + 0.4, 0.97), (R + 0.4, 1.03)], seg=seg, inside=True, outside=True)
    # Tank: grey wooden staves (cylinder of 'wood' + hoops), conical roof with a finial.
    m.cylinder('wood', (0, 0.0, 0), (0, tank_h, 0), R, seg, caps=False)
    m.cylinder('wood_dark', (0, 0.02, 0), (0, 0.12, 0), R - 0.05, seg)   # tank floor
    hoops = 7 if lod == 0 else 3
    loose = int(rng.integers(0, hoops)) if lod == 0 and rng.random() < 0.6 else -1
    for k in range(hoops):
        y = 0.3 + (tank_h - 0.6) * k / max(1, hoops - 1) * (1 - 0.25 * (k / hoops))
        if k == loose:
            # A hoop snapped and slid down: it hangs slanted.
            m.push((0, 0, 0))
            m.cylinder('rust', (0, y - 0.35, 0.0), (0.0, y - 0.3, 0.06), R + 0.03, seg, caps=False)
            m.pop()
            continue
        m.cylinder('rust', (0, y, 0), (0, y + 0.06, 0), R + 0.025, seg, caps=False)
    if broken:
        # The roof fell in: only a few rafters left at a slant, the rim of the cone.
        m.cylinder('rust_sheet', (0, tank_h, 0), (0, tank_h + 0.25, 0), R + 0.08, seg, r2=R * 0.85, caps=False)
        for i in range(3):
            a = rng.uniform(0, 2 * math.pi)
            m.oriented_box('wood_dark', (math.cos(a) * R * 0.9, tank_h + 0.2, math.sin(a) * R * 0.9),
                           (math.cos(a + 2.4) * R * 0.3, tank_h - rng.uniform(0.3, 1.2), math.sin(a + 2.4) * R * 0.3), 0.1, 0.08)
    else:
        roof = R * rng.uniform(0.45, 0.6)
        mat = 'rust_sheet'
        m.cylinder(mat, (0, tank_h, 0), (0, tank_h + roof, 0), R + 0.12, seg, r2=0.08, caps=True)
        m.cylinder('iron_black', (0, tank_h + roof, 0), (0, tank_h + roof + 0.4, 0), 0.04, 6)
        if lod == 0:
            # Roof hatch.
            m.box_c('wood_dark', (R * 0.45, tank_h + roof * 0.6, 0.0), (0.5, 0.06, 0.5))
    # Ladder from the deck to the roof up the side (and down the stand to the roof).
    if lod == 0:
        lx = R + 0.18
        for z in (-0.22, 0.22):
            m.box_c('iron_black', (lx, (tank_h + 0.5) / 2, z), (0.04, tank_h + 0.5, 0.04))
            m.box_c('iron_black', (lx + 0.25, -stand / 2, z), (0.04, stand, 0.04))
        for k in range(int((tank_h + 0.5) / 0.3)):
            m.box_c('iron_black', (lx, 0.2 + k * 0.3, 0.0), (0.03, 0.03, 0.44))
        for k in range(int(stand / 0.3)):
            m.box_c('iron_black', (lx + 0.25, -stand + 0.2 + k * 0.3, 0.0), (0.03, 0.03, 0.44))
        # Outlet pipe down to the roof.
        m.cylinder('rust_dark', (0.0, 0.0, 0.0), (0.0, -stand, 0.0), 0.12, 8)
    m.pop()
    return m


def hydrant(seed=0, lod=0):
    rng = np.random.default_rng(seed)
    m = Mesh(PALETTE)
    seg = 12 if lod == 0 else 8
    paint = 'paint_red' if rng.random() < 0.75 else 'paint_yellow'
    m.cylinder('rust_dark', (0, 0, 0), (0, 0.06, 0), 0.17, seg)               # flange on the pavement
    m.cylinder(paint, (0, 0.06, 0), (0, 0.55, 0), 0.12, seg)                   # barrel
    m.cylinder(paint, (0, 0.55, 0), (0, 0.6, 0), 0.15, seg)                    # top flange
    m.cylinder(paint, (0, 0.6, 0), (0, 0.72, 0), 0.12, seg, r2=0.06)          # bonnet
    m.cylinder('rust_dark', (0, 0.72, 0), (0, 0.78, 0), 0.03, 5)               # operating nut
    for s in (-1, 1):                                                          # hose nozzles
        m.cylinder(paint, (s * 0.1, 0.42, 0), (s * 0.2, 0.42, 0), 0.05, seg // 2 + 2)
        m.cylinder('rust_dark', (s * 0.2, 0.42, 0), (s * 0.23, 0.42, 0), 0.06, 6)
    m.cylinder(paint, (0, 0.38, 0.1), (0, 0.38, 0.2), 0.07, seg // 2 + 2)       # pumper nozzle
    m.cylinder('rust_dark', (0, 0.38, 0.2), (0, 0.38, 0.24), 0.085, 6)
    return m
