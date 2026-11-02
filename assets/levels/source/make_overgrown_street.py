#!/usr/bin/env python3
"""The overgrown street (assets/levels/overgrown_street.level + .terrain), original, CC0.

    python3 -I assets/levels/source/make_overgrown_street.py [--models]

--models first (re)builds the houses and street fixtures into assets/models/zone/street/.

The mood follows martin's reference picture (used as a mood board only, nothing copied): a narrow street canyon
between tall derelict brick and plaster houses, the road broken into slabs with thick grass in every joint,
shrubs and young trees growing on the carriageway, rusty wrecks sunk in the green, sagging cables and lamp posts,
a bright day with a blue sky and a long view down the street.

Design (docs/LEVEL_ART_GUIDE.md):
  - One path with a strong vanishing point: Почтовая street runs 250 m from south (start, z = +105) to north. The
    view is closed far away by a tall house straddling the axis, so the eye always has a goal.
  - Edges: continuous house fronts 16 m apart (4.5 m of carriageway each way of the axis, 3.5 m pavements), houses
    4 to 8 floors, so the skyline steps. Both ends are blocked by what really blocks streets here: a barricade of
    wrecks and concrete blocks behind the start, the rubble of a collapsed corner house at the north end.
  - A node in the middle: the crossing with Заводская street (14 m wide), the place to look sideways.
  - Nature takes the street back from the edges: the strip along each kerb, the foot of the walls, the cracks
    between the slabs; the middle keeps a trodden line of bare slabs, where people still walk.
"""
import json
import math
import os
import random
import struct
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, '..', '..'))                 # assets/
LEVELS = os.path.join(ROOT, 'levels')
sys.path.insert(0, os.path.join(ROOT, 'models', 'zone', 'source'))
from zonegen.terrain import Noise, smoothstep                          # noqa: E402

NAME = 'overgrown_street'
ST = '../models/zone/street/'
V = '../models/zone/vegetation/'
G = '../models/zone/gen/'
SK = '../models/zone/skrunda/'
PB = '../materials/pbr/'

FACADE = 8.0             # house fronts at x = +-8
KERB = 4.5               # kerb line (road side) at x = +-4.5
WALK = 0.15              # pavement above the road
CROSS = (-24.0, -10.0)   # cross street between these z (house fronts)
CROSS_C = sum(CROSS) / 2
NORTH_END, SOUTH_END = -138.0, 120.0     # barricades
X0, Z0, SIZE, RES = -165.0, -195.0, 330.0, 0.5     # engine terrain
SPLAT_N = 1536

# layers: (material, tiling = 1 / real size)
LAYERS = [(PB + 'cracked_concrete.material', 0.5), (PB + 'asphalt_02.material', 0.3333),
          (PB + 'concrete_pavement.material', 0.5556), (PB + 'concrete_moss.material', 0.3333),
          ('../materials/skrunda/terrain_dirt.material', 0.5), ('../materials/skrunda/terrain_lush.material', 0.5),
          ('../materials/skrunda/terrain_leaves.material', 0.33), (PB + 'rubble.material', 0.5)]
SLAB, ASPH, PAVE, MOSS, SOIL, GRASS, LEAVES, RUBBLE = range(8)


# ----------------------------------------------------------------------------------------------------------- houses
# (name, length, floors, skin, roof, seed): a small set reused along the street, never twice side by side
HOUSES = [('house_a', 36.0, 5, 'st_brick_red', 'pitched', 1), ('house_b', 30.0, 7, 'st_plaster_brick', 'flat', 2),
          ('house_c', 42.0, 6, 'st_brick_patch', 'pitched', 3), ('house_d', 24.0, 4, 'st_paint_peel', 'pitched', 4),
          ('house_e', 33.0, 8, 'st_brick_dark', 'flat', 5), ('house_f', 27.0, 6, 'st_plaster_crack', 'pitched', 6),
          ('house_g', 39.0, 5, 'st_brick_slumped', 'flat', 7), ('house_h', 30.0, 6, 'st_brick_worn', 'pitched', 8),
          ('house_i', 36.0, 7, 'st_plaster_beige', 'flat', 9), ('house_j', 45.0, 8, 'st_brick_red', 'flat', 10),
          ('house_end', 48.0, 9, 'st_brick_patch', 'flat', 11)]
DEPTH = 14.0
# shelled houses (second reference): (from, to) of the break as fractions of the length, first storey gone
SPOTS = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'overgrown_street_spots.json')  # from --models
RUINS = {'house_c': (0.55, 0.75, 4), 'house_e': (0.2, 0.36, 5), 'house_i': (0.62, 0.8, 4)}


def build_models():
    from zonegen import mats, glb, tenement, fixtures
    out = os.path.join(ROOT, 'models', 'zone', 'street')
    os.makedirs(out, exist_ok=True)
    mats.ensure_textures()
    fixtures.make_sign_atlas(force=True)
    spots = {}
    for name, L, fl, skin, roof, seed in HOUSES:
        for lod in (0, 1):
            m = tenement.tenement(L, fl, DEPTH, seed=seed, lod=lod, skin=skin, roof=roof,
                                  ruin=RUINS.get(name))
            if lod == 0:
                spots[name] = [[round(float(v), 2) for v in p[:3]] + [p[3]] for p in m.spots]
            fn = f'{name}.glb' if lod == 0 else f'{name}_lod1.glb'
            glb.write_glb(os.path.join(out, fn), m, tex_rel_dir='../textures', name=name)
        print(name, m.triangles(), 'tris (lod1)')
    with open(SPOTS, 'w') as f:
        json.dump(spots, f, separators=(',', ':'))
    for s in fixtures.CABLE_SPANS:
        glb.write_glb(os.path.join(out, f'cable_{s}m.glb'), fixtures.cable(s), tex_rel_dir='../textures', name=f'cable_{s}m')
    glb.write_glb(os.path.join(out, 'kerb_6m.glb'), fixtures.kerb(6.0, seed=3), tex_rel_dir='../textures', name='kerb_6m')
    glb.write_glb(os.path.join(out, 'kerb_6m_b.glb'), fixtures.kerb(6.0, seed=8), tex_rel_dir='../textures', name='kerb_6m_b')


def house_height(name):
    h = next(x for x in HOUSES if x[0] == name)
    return 4.0 + (h[2] - 1) * 3.2


# ----------------------------------------------------------------------------------------------------------- level
class Level:
    def __init__(self):
        self.lines, self.groups = [], []
        self.n = {}

    def group(self, name, parent=''):
        self.groups.append(f'group "{name}" "{parent}"')

    def model(self, label, path, x, y, z, yaw=0.0, scale=1.0, group=None, lod=None, cull=None, coll=True, shadows=True,
              tilt=(0.0, 0.0), sway=0.0, impostor=0.0):
        k = self.n.get(label, 0) + 1
        self.n[label] = k
        name = label if k == 1 else f'{label} {k}'
        self.lines.append(f'model "{name}" "{path}" {x:.2f} {y:.3f} {z:.2f}')
        if group:
            self.lines.append(f'  parent "{group}"')
        if yaw or scale != 1.0 or tilt != (0.0, 0.0):
            self.lines.append(f'  transform {tilt[0]:g} {yaw:.1f} {tilt[1]:g} {scale:.3g}')
        if lod:
            self.lines.append(f'  lod "{lod[0]}" {lod[1]:g}')
        if cull:
            self.lines.append(f'  cull_distance {cull:g}')
        if not coll:
            self.lines.append('  collision 0')
        if not shadows:
            self.lines.append('  cast_shadows 0')
        if sway:
            self.lines.append(f'  sway {sway:g}')
        if impostor:
            self.lines.append(f'  impostor {impostor:g}')


def build():
    rng = random.Random(1957)
    nz = Noise(77)
    lv = Level()
    for g, p in (('Houses', ''), ('Street', ''), ('Kerbs', 'Street'), ('Lamps and cables', 'Street'), ('Wrecks', 'Street'),
                 ('Rubble', 'Street'), ('Barricades', 'Street'), ('Plates', 'Houses'), ('Ivy', 'Houses'), ('Far', '')):
        lv.group(g, p)

    # ------------------------------------------------------------------ houses along both sides
    houses = []                 # (x0, z0, x1, z1) footprints, for the ground and the vegetation
    order = [h[0] for h in HOUSES if h[0] != 'house_end']
    fronts = []                 # (side, facade x, south corner z) of the houses along the playable street
    facades = []                # (side, facade x, centre z, length, house) of every house along the street
    ruins = []                  # (facade x, z from, z to) of the shelled houses: their rubble spills onto the street

    def row(side, z_from, z_to, start):
        """Houses along x = side * FACADE from z_from down to z_to (z_from > z_to); facade towards the axis."""
        z = z_from
        k = start
        while z > z_to + 6:
            name = order[k % len(order)]
            k += 1
            L = next(h[1] for h in HOUSES if h[0] == name)
            if z - L < z_to:
                L_fit = z - z_to
                if L_fit < 18:
                    break
            setback = rng.choice((0.0, 0.0, 0.25, -0.2, 0.4))
            cx = side * (FACADE + setback + DEPTH / 2)
            cz = z - L / 2
            lv.model(name.replace('house_', 'House ').upper()[:7] if False else f'House {name[-1].upper()}',
                     ST + name + '.glb', cx, WALK, cz, 90.0 if side < 0 else -90.0, group='Houses',
                     lod=(ST + name + '_lod1.glb', 70), cull=600)
            houses.append((cx - DEPTH / 2, cz - L / 2, cx + DEPTH / 2, cz + L / 2, name))
            if name in RUINS:           # where the break is, along the street (local +x runs along side * z)
                za, zb = sorted(cz + side * (-L / 2 + f * L) for f in RUINS[name][:2])
                ruins.append((side * (FACADE + setback), za, zb))
            facades.append((side, side * (FACADE + setback), cz, L, name))
            if NORTH_END - 10 < cz < SOUTH_END + 10:
                fronts.append((side, side * (FACADE + setback), cz + L / 2))
            z -= L - 0.2           # 20 cm overlap: no sunlit slit between the party walls
        return k

    k = row(-1, 175.0, CROSS[1], 0)
    k = row(-1, CROSS[0], -330.0, k + 3)
    k = row(1, 172.0, CROSS[1], 5)
    k = row(1, CROSS[0], -330.0, k + 2)
    # the cross street: corner houses face it, the far ends are closed by houses across it
    for sx in (-1, 1):
        for sz, zf in ((1, CROSS[1]), (-1, CROSS[0])):
            name = 'house_c' if sx * sz > 0 else 'house_g'
            L = next(h[1] for h in HOUSES if h[0] == name)
            cx = sx * (FACADE + DEPTH + L / 2 + 0.5)
            cz = zf + sz * DEPTH / 2
            lv.model(f'House {name[-1].upper()}', ST + name + '.glb', cx, WALK, cz, 180.0 if sz > 0 else 0.0, group='Houses',
                     lod=(ST + name + '_lod1.glb', 70), cull=600)
            houses.append((cx - L / 2, cz - DEPTH / 2, cx + L / 2, cz + DEPTH / 2, name))
        name = 'house_e'
        cx = sx * 78.0
        lv.model('House E', ST + name + '.glb', cx, WALK, CROSS_C, 90.0 if sx > 0 else -90.0, group='Houses',
                 lod=(ST + name + '_lod1.glb', 70), cull=600)
    # the far end of the view: a taller house across the axis, towers beyond it in the haze
    lv.model('House at the end', ST + 'house_end.glb', 0.0, WALK, -338.0, 0.0, group='Far', lod=(ST + 'house_end_lod1.glb', 70), cull=900)
    for (tx, tz, t) in ((-40, -420, 'a'), (35, -460, 'c'), (90, -380, 'b'), (-110, -400, 'b'), (-190, -640, 'c'),
                        (160, -600, 'a'), (-20, -720, 'c'), (230, -480, 'b')):
        lv.model('Tower', G + f'skyline_tower_{t}.glb', tx, -0.2, tz, rng.uniform(0, 90), group='Far', cull=1500, coll=False)
    # sixteen-storey panel blocks of the newer town behind the old street: they stand over the roofs of the
    # tenements on both sides and close the view at both ends, grey in the mist (the second reference picture)
    for (bx, bz, yaw) in ((-72, -380, 8), (64, -440, -6), (150, -370, 85), (-160, -470, 30), (15, -540, 2),
                          (-62, -130, 90), (70, -60, 90), (-68, 70, 92), (74, 140, 88), (-20, 260, 180), (60, 300, 175),
                          (-110, 230, 120)):
        lv.model('Panel block', '../models/zone/panel_block_16.glb', bx, 0.0, bz, yaw, group='Far',
                 lod=('../models/zone/panel_block_16_lod1.glb', 120), cull=1500, coll=False)
    lv.lines.append(f'box "Far street ground" 0 -0.06 -265 140 0.1 150 1 1 1')
    lv.lines.append('  parent "Far"')
    lv.lines.append('  material "../materials/skrunda/terrain_lush.material"')

    # ivy and wild vine climbing the fronts from the pavement: mostly low patches round the ground floor, here and
    # there a sheet up to the third or fifth floor (never over a break: there is no wall left to climb)
    irng = random.Random(4242)  # own stream: the rest of the street stays where it was
    for side, fx, cz, L, name in facades:
        brk = [(a, b) for (rx, a, b) in ruins if abs(rx - fx) < 1.0 and cz - L / 2 <= a <= cz + L / 2]
        for _ in range(max(1, int(L / 8.0 * irng.uniform(0.5, 1.4)))):
            z = irng.uniform(cz - L / 2 + 1.5, cz + L / 2 - 1.5)
            r = irng.random()
            key = 'ivy_low' if r < 0.42 else 'ivy_a' if r < 0.72 else 'ivy_b' if r < 0.92 else 'ivy_tall'
            if key in ('ivy_b', 'ivy_tall') and any(a - 4 < z < b + 4 for a, b in brk):
                key = 'ivy_low'
            lv.model('Ivy', V + key + '.glb', fx - side * 0.04, WALK, z, 90.0 if side < 0 else -90.0,
                     irng.uniform(0.85, 1.15), group='Ivy', lod=(V + key + '_lod1.glb', 30), cull=140 if key != 'ivy_low' else 70,
                     coll=False, shadows=key != 'ivy_low', sway=0.03)

    # self-seeded birches, maples and grass on balconies, flat roofs and the open floors of the broken houses (the
    # spots come from the house generator; local x runs along side * z, local +z (the front) toward the street)
    spots = json.load(open(SPOTS)) if os.path.exists(SPOTS) else {}
    for side, fx, cz, L, name in facades:
        cx = fx + side * DEPTH / 2
        for lx, ly, lz, kind in spots.get(name, []):
            p = {'balcony': 0.3, 'roof': 0.3, 'ledge': 0.8, 'cornice': 0.22}[kind]
            if irng.random() > p:
                continue
            x, z = cx - side * lz, cz + side * lx
            r = irng.random()
            if kind == 'balcony':
                key, sc = (('sapling_c', irng.uniform(0.6, 0.95)) if r < 0.45 else ('birch_young', irng.uniform(0.35, 0.55))
                           if r < 0.7 else (irng.choice(('grass_clump_a', 'weed_a', 'nettle_tall_a')), irng.uniform(0.8, 1.1)))
            elif kind == 'cornice':
                key, sc = ('birch_young', irng.uniform(0.25, 0.45)) if r < 0.7 else ('sapling_c', irng.uniform(0.4, 0.7))
            else:
                key, sc = (('birch_young', irng.uniform(0.5, 0.9)) if r < 0.45 else ('sapling_c', irng.uniform(0.7, 1.1))
                           if r < 0.75 else ('sapling_a', irng.uniform(0.5, 0.8)))
            lv.model('Roof plant', V + key + '.glb', x, WALK + ly, z, irng.uniform(0, 360), sc, group='Ivy',
                     lod=(V + key + '_lod1.glb', 25), cull=120 if kind != 'balcony' or sc > 0.6 else 60,
                     coll=False, shadows=kind != 'balcony', sway=0.7 if key.startswith(('sapling', 'birch')) else 1.0)

    # enamel plates at the south corner of each house: the street name over the house number (odd west, even east)
    num = {-1: 1, 1: 2}
    for side, fx, zc in sorted(fronts, key=lambda f: -f[2]):
        x = fx - side * 0.02
        yaw = 90.0 if side < 0 else -90.0
        if num[side] <= 24:
            lv.model('House number', SK + f'plate_house_{num[side]}.glb', x, WALK + 2.4, zc - 0.95, yaw, group='Plates',
                     cull=45, coll=False, shadows=False)
        if rng.random() < 0.6:
            lv.model('Street plate', SK + 'plate_street_pochta.glb', x, WALK + 2.8, zc - 0.95, yaw, group='Plates',
                     cull=45, coll=False, shadows=False)
        num[side] += 2

    # ------------------------------------------------------------------ street furniture
    def kerb_run(z_a, z_b, side):
        z = z_a
        while z - 6.0 >= z_b - 0.01:
            m = ST + ('kerb_6m.glb' if rng.random() < 0.5 else 'kerb_6m_b.glb')
            lv.model('Kerb', m, side * KERB, 0.0, z - 3.0, 90.0 if side < 0 else -90.0, group='Kerbs', cull=90, coll=False,
                     shadows=False)
            z -= 6.0
    for side in (-1, 1):
        kerb_run(SOUTH_END + 30, CROSS[1] - 0.0 + 6, side)
        kerb_run(CROSS[0] - 6, NORTH_END - 30, side)
    for sz, zk in ((1, CROSS[1] + 5.5 - 5.5 - 0.0), (-1, CROSS[0])):
        pass
    for sx in (-1, 1):
        for zk, yaw in ((CROSS_C + KERB, 180.0), (CROSS_C - KERB, 0.0)):
            x = sx * (FACADE + 1.0)
            while abs(x) + 6.0 <= 72.0:
                lv.model('Kerb', ST + 'kerb_6m.glb', x + sx * 3.0, 0.0, zk, yaw, group='Kerbs', cull=90, coll=False, shadows=False)
                x += sx * 6.0

    lamps = []
    z = SOUTH_END - 8
    side = 1
    while z > NORTH_END - 60:
        if CROSS[0] - 3 < z < CROSS[1] + 3:
            z -= 9
            continue
        lean = rng.random() < 0.25
        x = side * (KERB + 0.7)
        yaw = 180.0 if side > 0 else 0.0
        lv.model('Lamp post', G + ('lamp_post_leaning.glb' if lean else 'lamp_post.glb'), x, WALK - 0.02, z,
                 yaw + rng.uniform(-6, 6), group='Lamps and cables', cull=300)
        lamps.append((x, z, side, lean))
        z -= rng.uniform(26, 32)
        side = -side
    # cables: from lamp to lamp along the street at 8 m, and across the street between the houses at 7-11 m
    spans = (6, 10, 14, 18, 24, 32)

    def cable(xa, za, ya, xb, zb, yb):
        L = math.hypot(xb - xa, zb - za)
        s = min(spans, key=lambda t: abs(t - L))
        sc = L / s
        yaw = math.degrees(math.atan2(-(zb - za), xb - xa))       # local +X onto the span
        pitch = math.degrees(math.atan2(yb - ya, L))
        lv.model('Cable', ST + f'cable_{s}m.glb', (xa + xb) / 2, (ya + yb) / 2, (za + zb) / 2, yaw, sc,
                 group='Lamps and cables', cull=160, coll=False, tilt=(0.0, pitch))
    for (xa, za, _s, la), (xb, zb, _t, lb) in zip(lamps, lamps[1:]):
        if rng.random() < 0.7:
            cable(xa, za, 8.1, xb, zb, 8.1)
    for z in np.arange(SOUTH_END - 20, NORTH_END - 40, -17.0):
        if CROSS[0] - 2 < z < CROSS[1] + 2:
            continue
        if rng.random() < 0.75:
            dz = rng.uniform(-4, 4)
            cable(-FACADE + 0.05, z, rng.uniform(7.0, 11.0), FACADE - 0.05, z + dz, rng.uniform(7.0, 11.0))

    # ------------------------------------------------------------------ wrecks, rubble, barricades
    reclaim_pts = []            # places nature had time to grow over: around wrecks and heaps
    wrecks = []                 # (x, z, yaw of the length axis) for the grass packed around them
    cars = ['car_estate_rust', 'car_burnt', 'car_estate_rust', 'car_sedan_green', 'car_van_green', 'car_sedan_cream',
            'car_estate_red', 'car_burnt']
    spots = [(3.0, 96.0, 172), (-3.1, 74.0, 8), (2.6, 41.0, 186), (-2.8, 12.0, -10), (0.8, -48.0, 150), (-3.0, -66.0, 2),
             (3.2, -92.0, 176), (-2.5, -112.0, 25), (6.1, 60.0, 182), (-6.0, -84.0, 3)]
    pirozhok = {0: 'car_pirozhok_rust_green', 2: 'car_pirozhok_wreck', 4: 'car_pirozhok_rust_blue', 7: 'car_pirozhok_rust_green'}
    for i, (x, z, yaw) in enumerate(spots):
        c = cars[i % len(cars)]
        y = (WALK if abs(x) > KERB else 0.0) - 0.06
        path = G + c + '.glb'
        if i in pirozhok:              # the scanned Izh vans (length along X): sunk deeper, the tyres are flat
            path, yaw, y = '../models/zone/' + pirozhok[i] + '.glb', yaw + 90.0, y - 0.05
        jitter = rng.uniform(-6, 6)
        lv.model('Wreck', path, x, y, z, yaw + jitter, group='Wrecks', cull=260,
                 tilt=(rng.uniform(-2, 2), rng.uniform(-2.5, 2.5)))
        reclaim_pts.append((x, z, 3.5))
        wrecks.append((x, z, spots[i][2] + jitter))
    lv.model('Bus', G + 'bus_small_cream.glb', 1.0, -0.08, -126.0, 62.0, group='Barricades', cull=320)
    for (x, z, s, yaw) in ((-4.0, -132, 2.4, 0), (3.5, -136, 2.1, 70), (-1.0, -140, 2.8, 30), (6.0, -130, 1.6, 120),
                           (-6.5, -127, 1.4, 200)):
        lv.model('Rubble heap', G + 'rubble_b.glb', x, -0.2, z, yaw, s, group='Barricades', cull=320)
    for (x, z, yaw) in ((-2.0, -129.0, 15), (2.5, -133.0, 100)):
        lv.model('Fallen slab', G + 'slab_fallen.glb', x, 0.6, z, yaw, 1.0, group='Barricades', cull=200, tilt=(12, 0))
    reclaim_pts += [(0.0, -128.0, 9.0)]
    # south: wrecks pushed together across the street, concrete blocks in the gaps
    for (x, z, yaw, c) in ((-4.2, 117.0, 78, 'car_burnt'), (-0.6, 118.5, 96, 'car_estate_rust'), (3.4, 117.4, 70, 'car_van_green')):
        lv.model('Barricade wreck', G + c + '.glb', x, -0.06, z, yaw, group='Barricades', cull=260)
        wrecks.append((x, z, yaw))
    for (x, z) in ((6.3, 118.0), (-6.6, 118.4)):
        lv.model('Rubble heap', G + 'rubble_a.glb', x, 0.0, z, rng.uniform(0, 360), 1.3, group='Barricades', cull=200)
    reclaim_pts += [(0.0, 118.0, 7.0)]
    # debris below the facades, more where a balcony or a cornice came down
    for _ in range(26):
        side = rng.choice((-1, 1))
        z = rng.uniform(NORTH_END + 5, SOUTH_END - 8)
        if CROSS[0] - 1 < z < CROSS[1] + 1:
            continue
        x = side * rng.uniform(6.0, 7.4)
        lv.model('Rubble', G + rng.choice(('rubble_a.glb', 'rubble_b.glb')), x, WALK - 0.25, z, rng.uniform(0, 360),
                 rng.uniform(0.35, 0.6), group='Rubble', cull=120)
    for _ in range(5):
        x, z = rng.uniform(-3, 3), rng.uniform(NORTH_END + 15, SOUTH_END - 15)
        if CROSS[0] - 4 < z < CROSS[1] + 4:
            continue
        lv.model('Slab', G + 'slab_fallen.glb', x, -0.3, z, rng.uniform(0, 180), rng.uniform(0.5, 0.75), group='Rubble', cull=140,
                 tilt=(rng.uniform(-8, 8), rng.uniform(-8, 8)))
        reclaim_pts.append((x, z, 1.5))

    # ------------------------------------------------------------------ ground: heights and layers
    n = int(round(SIZE / RES)) + 1
    xs = X0 + np.arange(n) * RES
    zs = Z0 + np.arange(n) * RES
    X, Z = np.meshgrid(xs, zs)
    road_d = road_dist(X, Z)              # signed: < 0 on the carriageway
    H = WALK * smoothstep(-0.05, 0.25, road_d)
    H = H + 0.06 * (nz.fbm(X + 3, Z - 5, 3.0, 2) - 0.5) * smoothstep(0.3, 1.5, road_d)     # pavement settled unevenly
    for (x, z, r) in reclaim_pts:
        d = np.hypot(X - x, Z - z)
        H = H + 0.12 * smoothstep(r, 0.0, d)                                               # earth blown against
    S = splat(nz, reclaim_pts)

    # ------------------------------------------------------------------ vegetation (terrain foliage)
    inst, types = vegetation(nz, rng, S, H, reclaim_pts, lamps, wrecks, ruins)

    # ------------------------------------------------------------------ the street beyond the ground: trees and bushes
    # Past the north barricade the street runs on for 140 m to the house at its end; the terrain stops at z = -195,
    # so the green that fills the far view there is made of models (impostors far away), on the far street ground.
    far = random.Random(4242)
    placed = []
    for _ in range(4000):
        if len(placed) >= 46:
            break
        x, z = far.uniform(-FACADE + 1.0, FACADE - 1.0), far.uniform(-330.0, Z0 + 1.0)
        if any(math.hypot(x - a, z - b) < 4.2 for a, b in placed):
            continue
        placed.append((x, z))
        key = far.choice(('maple_a', 'maple_b', 'ash_a', 'ash_b', 'birch_a', 'broadleaf_a', 'broadleaf_b', 'maple_b'))
        lv.model('Far tree', V + key + '.glb', x, -0.05, z, far.uniform(0, 360), far.uniform(0.75, 1.05), group='Far',
                 lod=(V + key + '_lod1.glb', 45), cull=900, coll=False, sway=0.3, impostor=120)
    for _ in range(70):
        x, z = far.uniform(-FACADE + 0.5, FACADE - 0.5), far.uniform(-330.0, Z0 + 1.0)
        key = far.choice(('bush_street_a', 'bush_street_b', 'bush_street_c', 'bush_street_a', 'sapling_a'))
        lv.model('Far bush', V + key + '.glb', x, -0.03, z, far.uniform(0, 360), far.uniform(0.6, 1.0), group='Far',
                 lod=(V + key + '_lod1.glb', 25 if 'sapling_a' not in key else 35), cull=600, coll=False, shadows=False,
                 sway=1.0)

    # ------------------------------------------------------------------ header and entities
    head = ['# FPS engine level', '# Overgrown street (Почтовая): made by assets/levels/source/make_overgrown_street.py;',
            '# edit in the editor afterwards (re-running the script rebuilds it).', '# Original CC0 content.',
            'version 5', 'player_start 1.5 0 104',
            'sun -0.5 -0.62 -0.6']
    # a storm coming (martin's second reference): heavy dark clouds with a pale band low over the horizon, grey
    # diffuse light with weak shadows, mist that swallows the far blocks, the road wet from the last shower
    env = {'sun_color': '0.82 0.86 0.92', 'sun_intensity': '1.5', 'sky_zenith': '0.085 0.095 0.11', 'sky_horizon': '0.46 0.48 0.48',
           'ground_color': '0.2 0.2 0.18', 'cloud_cover': '0.74', 'atmosphere': '0.6', 'atmosphere_haze': '3', 'sky_occlusion': '0.8', 'ambient_intensity': '0.95', 'fog_color': '0.36 0.39 0.4',
           'fog_density': '0.011', 'fog_height': '6', 'fog_height_falloff': '0.012', 'fog_sun_scatter': '0.25',
           'exposure': '1.05', 'bloom_intensity': '0.05', 'bloom_threshold': '1.2', 'ssao_radius': '0.8', 'ssao_intensity': '1.4',
           'shadow_distance': '110', 'saturation': '0.72', 'contrast': '1.12', 'tint': '0.96 1 1.02', 'vignette': '0.32',
           'grain': '0.03', 'auto_exposure': '0.5', 'exposure_min_ev': '-0.5', 'exposure_max_ev': '2', 'rain': '0.06',
           'puddles': '0.6', 'wind': '0.55'}
    head += [f'env {k} {v}' for k, v in env.items()]
    head += ['loading_title "Почтовая улица"', 'loading_hint "Projdi ulicí na sever, k domu na jejím konci."',
             'weapon "../weapons/ak74.weapon"', 'weapon "../weapons/pistol.weapon"', 'weapon "../weapons/knife.weapon"']
    head.append(f'terrain "{NAME}.terrain"')
    head += [f'terrain_layer {i} "{m}" {t:g}' for i, (m, t) in enumerate(LAYERS)]
    for key, _ in sorted(types.items(), key=lambda kv: kv[1]):
        path, dens, lo, hi, cull, coll, shadows, far, lods, imp = FOLIAGE[key]
        head.append(f'foliage_type "{path}" {dens:g} {lo:g} {hi:g} {cull:g} {coll} 0 {shadows} {far:g}')
        for lf, ld in lods:
            head.append(f'foliage_lod "{lf}" {ld:g}')
        if imp:
            head.append(f'foliage_impostor {imp:g}')
    ents = ['entity "Почтовая" "map_marker" 0 0 60 0 1 1 1 0.85 0.3 0.2 ""',
            'entity "Перекрёсток" "map_marker" 0 0 -17 0 1 1 1 0.85 0.3 0.2 ""']
    text = '\n'.join(head + lv.groups + lv.lines + ents) + '\n'
    open(os.path.join(LEVELS, NAME + '.level'), 'w', encoding='utf-8').write(text)
    with open(os.path.join(LEVELS, NAME + '.terrain'), 'wb') as f:
        f.write(struct.pack('<IIiifffI', 0x52524554, 2, n, SPLAT_N, X0, Z0, SIZE, len(inst)))
        f.write(np.asarray(H, dtype='<f4').tobytes())
        f.write(S.tobytes())
        for x, y, z, yaw, sc, t in inst:
            f.write(struct.pack('<7fI', x, y, z, yaw, sc, 0.0, 0.0, t))
    print(f'{NAME}: {len(lv.lines)} model lines, terrain {n} x {n}, splat {SPLAT_N}, {len(inst)} plants')


def road_dist(X, Z):
    """Signed distance to the edge of the carriageways (main street and the cross street): < 0 on the road."""
    d_main = np.abs(X) - KERB
    d_cross = np.where(np.abs(X) < 70.0, np.abs(Z - CROSS_C) - KERB, 1e3)
    return np.minimum(d_main, d_cross)


def facade_dist(X, Z):
    """Distance from the house fronts (0 at the wall, positive towards the street)."""
    d_main = FACADE - np.abs(X)
    d_cross = np.where(np.abs(X) > FACADE, (CROSS[1] - CROSS[0]) / 2 - np.abs(Z - CROSS_C), -1e3)
    in_cross = (Z > CROSS[0]) & (Z < CROSS[1])
    return np.where(in_cross, np.maximum(d_main, d_cross), d_main)


def reclaim(nz, X, Z, reclaim_pts):
    """0 .. 1: how far nature has taken the ground back. Strong along the kerbs and the walls, in slow blobs, around
    the wrecks and heaps; weak along a wandering trodden line in the middle of the road."""
    rd = road_dist(X, Z)
    fd = facade_dist(X, Z)
    kerb = np.exp(-np.abs(rd) / 1.4)
    wall = np.exp(-np.maximum(fd, 0) / 1.0) * (fd > -0.5)
    blob = smoothstep(0.42, 0.68, nz.fbm(X * 1.0 + 300, Z, 16.0, 3))
    fine = nz.fbm(X - 40, Z + 70, 3.0, 2) - 0.5
    trod = 0.8 * np.sin(Z / 37.0) + 0.5 * np.sin(Z / 11.0 + 1.3)
    on_main = np.abs(X) < KERB
    path = np.where(on_main, smoothstep(1.6, 0.3, np.abs(X - trod)), 0.0)
    near = np.zeros_like(X)
    for (x, z, r) in reclaim_pts:
        near = np.maximum(near, smoothstep(r + 2.0, r * 0.4, np.hypot(X - x, Z - z)))
    beyond = smoothstep(NORTH_END + 2, NORTH_END - 25, Z) + smoothstep(SOUTH_END - 2, SOUTH_END + 12, Z)   # nobody walks there
    R = 0.75 * kerb + 0.6 * wall + 0.75 * blob + 0.3 * fine + 0.6 * near + 0.12 - 0.5 * path * (1 - beyond) + 0.55 * beyond
    return np.clip(R, 0.0, 1.0)


def cracks(X, Z, cell=2.3, seed=5):
    """Distance to the nearest crack (a jittered Voronoi network, slab size about `cell`) and a value per slab."""
    gx, gz = X / cell, Z / cell
    ix, iz = np.floor(gx).astype(np.int64), np.floor(gz).astype(np.int64)
    d1 = np.full(X.shape, 9.0)
    d2 = np.full(X.shape, 9.0)
    cid = np.zeros(X.shape)
    for ox in (-1, 0, 1):
        for oz in (-1, 0, 1):
            cx, cz = ix + ox, iz + oz
            h = (cx * 73856093) ^ (cz * 19349663) ^ seed
            jx = ((h * 2654435761) & 0xffff) / 65535.0
            jz = ((h * 40503) & 0xffff) / 65535.0
            px, pz = cx + 0.1 + 0.8 * jx, cz + 0.1 + 0.8 * jz
            d = np.hypot(gx - px, gz - pz)
            closer = d < d1
            d2 = np.where(closer, d1, np.minimum(d2, d))
            cid = np.where(closer, ((h * 9973) & 0xff) / 255.0, cid)
            d1 = np.where(closer, d, d1)
    return (d2 - d1) * 0.5 * cell, cid


def splat(nz, reclaim_pts):
    s = (np.arange(SPLAT_N) + 0.5) / SPLAT_N * SIZE
    X, Z = np.meshgrid(X0 + s, Z0 + s)
    rd = road_dist(X, Z)
    fd = facade_dist(X, Z)
    R = reclaim(nz, X, Z, reclaim_pts)
    ce, cid = cracks(X, Z)
    W = np.zeros(X.shape + (8,), np.float32)
    road = rd < 0
    # carriageway: concrete slabs with patches of old asphalt
    asph = smoothstep(0.6, 0.615, nz.fbm(X + 11, Z - 90, 9.0, 2))     # a few old patches, the rest slabs
    W[..., SLAB] = np.where(road, 1.0 - asph, 0.0)
    W[..., ASPH] = np.where(road, asph, 0.0)
    W[..., PAVE] = np.where(road, 0.0, 1.0)

    def put(layer, cover):
        cover = np.clip(cover, 0.0, 1.0).astype(np.float32)
        W[...] *= (1.0 - cover)[..., None]
        W[..., layer] += cover
    # moss creeping out of the joints, soil and grass in them; whole slabs gone under the grass where R is high
    # (the slab joints stop at the old asphalt patches: poured in one piece, they crack in their own texture, and a
    # grass-filled joint running straight through them read as a painted yellow line)
    cw = 0.05 + 0.5 * R ** 1.4
    joint = road * (1.0 - asph)
    put(MOSS, smoothstep(cw + 0.35, cw, ce) * (0.1 + 0.12 * R) * joint)
    put(SOIL, smoothstep(cw * 0.5 + 0.05, cw * 0.5 - 0.03, ce) * joint * 0.8)
    heaved = (R + 0.3 * cid > 0.85) & road
    put(SOIL, heaved * 0.35)
    put(GRASS, (smoothstep(cw * 0.7 + 0.04, cw * 0.7 - 0.04, ce) * smoothstep(0.05, 0.4, R) * joint + heaved * 0.85 * road))
    # pavement: moss along its joints near the walls, a band of soil, leaves and plaster rubble at the foot of the walls
    walk = ~road & (fd > 0)
    put(MOSS, smoothstep(0.35, 0.8, R + 0.3 * (nz(X + 7, Z, 1.3) - 0.5)) * 0.7 * walk)
    foot = smoothstep(1.4, 0.3, fd + 0.6 * (nz(X, Z + 3, 2.0) - 0.5))
    put(SOIL, foot * walk)
    put(RUBBLE, foot * smoothstep(0.55, 0.7, nz(X - 9, Z + 1, 4.0)) * walk * 0.8)
    put(GRASS, smoothstep(0.45, 0.75, R) * walk * 0.9)
    put(LEAVES, smoothstep(0.62, 0.8, nz.fbm(X + 500, Z, 6.0, 2)) * smoothstep(0.3, 0.6, R) * 0.7 * (fd > 0))
    # behind the houses (only seen through gaps): yards gone wild
    put(GRASS, (fd <= 0) * 0.9)
    put(SOIL, (fd <= 0) * smoothstep(0.55, 0.7, nz(X, Z, 7.0)) * 0.6)
    # the strip of earth blown against the kerb on the road side
    put(SOIL, smoothstep(-0.6, -0.05, rd) * road * (0.3 + 0.3 * R))
    put(GRASS, smoothstep(-0.7, -0.1, rd) * road * smoothstep(0.3, 0.6, R) * 0.9)
    tot = np.maximum(W.sum(-1, keepdims=True), 1e-6)
    return np.clip(np.round(W / tot * 255), 0, 255).astype(np.uint8)


# foliage types: key -> (model, brush density, min scale, max scale, cull m, collision, shadows, far density, lods, impostor m)
def _f(path, dens=0.1, lo=0.85, hi=1.15, cull=50, coll=0, shadows=0, far=0.6, lods=(), imp=0):
    return (path, dens, lo, hi, cull, coll, shadows, far, list(lods), imp)


FOLIAGE = {
    'grass_patch': _f(SK + 'veg/grass_fresh.glb', 0.25, cull=70, far=0.5, lods=[(SK + 'veg/grass_fresh_lod1.glb', 10), (SK + 'veg/grass_fresh_far.glb', 24)]),
    'grass_tall_a': _f(V + 'grass_tall_a.glb', lods=[(V + 'grass_tall_a_lod1.glb', 10)], cull=55),
    'grass_tall_b': _f(V + 'grass_tall_b.glb', lods=[(V + 'grass_tall_b_lod1.glb', 10)], cull=55),
    'grass_clump': _f(V + 'grass_clump_a.glb', lods=[(V + 'grass_clump_a_lod1.glb', 10), (V + 'grass_clump_a_far.glb', 22)], cull=45),
    'grass_short': _f(V + 'grass_short_a.glb', lods=[(V + 'grass_short_a_lod1.glb', 10)], cull=40),
    'weed_a': _f(V + 'weed_a.glb', lods=[(V + 'weed_a_lod1.glb', 10), (V + 'weed_a_far.glb', 22)], cull=45),
    'weed_b': _f(V + 'weed_b.glb', lods=[(V + 'weed_b_lod1.glb', 10), (V + 'weed_b_far.glb', 22)], cull=45),
    'nettle': _f(V + 'nettle_tall_a.glb', lods=[(V + 'nettle_tall_a_lod1.glb', 10), (V + 'nettle_tall_a_far.glb', 22)], cull=50),
    'dandelion_a': _f(V + 'dandelion_a.glb', lods=[(V + 'dandelion_a_lod1.glb', 8), (V + 'dandelion_a_far.glb', 14)], cull=22),
    'dandelion_c': _f(V + 'dandelion_c.glb', lods=[(V + 'dandelion_c_lod1.glb', 8), (V + 'dandelion_c_far.glb', 14)], cull=22),
    'celandine_a': _f(V + 'celandine_a.glb', lods=[(V + 'celandine_a_lod1.glb', 8)], cull=22),
    'celandine_b': _f(V + 'celandine_b.glb', lods=[(V + 'celandine_b_lod1.glb', 8), (V + 'celandine_b_far.glb', 14)], cull=22),
    'shrub_a': _f(V + 'bush_street_b.glb', 0.03, cull=160, shadows=1, far=0.8, lods=[(V + 'bush_street_b_lod1.glb', 25)]),
    'shrub_b': _f(V + 'bush_street_a.glb', 0.03, cull=160, shadows=1, far=0.8, lods=[(V + 'bush_street_a_lod1.glb', 25)]),
    'bush_dense': _f(V + 'bush_street_c.glb', 0.02, cull=160, shadows=1, far=0.8, lods=[(V + 'bush_street_c_lod1.glb', 25)]),
    'bush_wide': _f(V + 'bush_street_a.glb', 0.02, cull=160, shadows=1, far=0.8, lods=[(V + 'bush_street_a_lod1.glb', 25)]),
    'sapling_a': _f(V + 'sapling_a.glb', 0.03, cull=220, shadows=1, far=1.0, lods=[(V + 'sapling_a_lod1.glb', 35)]),
    'sapling_b': _f(V + 'sapling_b.glb', 0.03, cull=220, shadows=1, far=1.0, lods=[(V + 'sapling_b_lod1.glb', 35)]),
    'sapling_c': _f(V + 'sapling_c.glb', 0.03, cull=200, shadows=1, far=1.0, lods=[(V + 'sapling_c_lod1.glb', 35)]),
    'birch_young': _f(V + 'birch_young.glb', 0.03, cull=220, shadows=1, far=1.0, lods=[(V + 'birch_young_lod1.glb', 35)]),
    'broadleaf_young': _f(V + 'broadleaf_young.glb', 0.03, cull=300, coll=1, shadows=1, far=1.0, lods=[(V + 'broadleaf_young_lod1.glb', 40)]),
    'broadleaf_a': _f(V + 'broadleaf_a.glb', 0.03, cull=400, coll=1, shadows=1, far=1.0, lods=[(V + 'broadleaf_a_lod1.glb', 45)], imp=120),
    'broadleaf_b': _f(V + 'broadleaf_b.glb', 0.03, cull=400, coll=1, shadows=1, far=1.0, lods=[(V + 'broadleaf_b_lod1.glb', 45)], imp=120),
    'maple_a': _f(V + 'maple_a.glb', 0.03, cull=400, coll=1, shadows=1, far=1.0, lods=[(V + 'maple_a_lod1.glb', 45)], imp=120),
    'maple_b': _f(V + 'maple_b.glb', 0.03, cull=400, coll=1, shadows=1, far=1.0, lods=[(V + 'maple_b_lod1.glb', 45)], imp=120),
    'ash_a': _f(V + 'ash_a.glb', 0.03, cull=400, coll=1, shadows=1, far=1.0, lods=[(V + 'ash_a_lod1.glb', 45)], imp=120),
    'ash_b': _f(V + 'ash_b.glb', 0.03, cull=400, coll=1, shadows=1, far=1.0, lods=[(V + 'ash_b_lod1.glb', 45)], imp=120),
    'birch_a': _f(V + 'birch_a.glb', 0.03, cull=400, coll=1, shadows=1, far=1.0, lods=[(V + 'birch_a_lod1.glb', 45)], imp=120),
}
# ground debris (zonegen/debris.py), scattered like plants: what fell off the houses, what the wind brought
DEBRIS = {'concrete_chunks': 60, 'plaster_chunks': 50, 'silicate_bricks': 50, 'glass_shards': 40, 'tin_cans': 40, 'bottles': 40,
          'twigs': 45, 'planks': 90, 'sheet': 90, 'brick_heap': 110}
for _k, _c in DEBRIS.items():
    FOLIAGE['deb_' + _k] = _f(SK + f'debris_{_k}.glb', 0.02, cull=_c, far=0.7, shadows=1 if _k in ('brick_heap', 'sheet', 'planks') else 0)


def vegetation(nz, rng, S, H, reclaim_pts, lamps, wrecks, ruins=()):
    """Plants from the splat (grass where the ground is soil or grass) and the reclaim field: crack grass and weeds in
    the joints, tall grass and flowers where the slabs are gone, shrubs and young trees where nature has had longest."""
    types, inst = {}, []
    W = S.astype(np.float32) / 255.0
    n = H.shape[0]

    def at(A, x, z):
        u = int(min(max((x - X0) / SIZE * SPLAT_N, 0), SPLAT_N - 1))
        v = int(min(max((z - Z0) / SIZE * SPLAT_N, 0), SPLAT_N - 1))
        return A[v, u]

    def height(x, z):
        fx, fz = (x - X0) / RES, (z - Z0) / RES
        i, j = int(min(max(fx, 0), n - 2)), int(min(max(fz, 0), n - 2))
        tx, tz = min(max(fx - i, 0), 1), min(max(fz - j, 0), 1)
        return float((H[j, i] * (1 - tx) + H[j, i + 1] * tx) * (1 - tz) + (H[j + 1, i] * (1 - tx) + H[j + 1, i + 1] * tx) * tz)

    def put(key, x, z, scale=1.0, sink=0.03):
        if key not in types:
            types[key] = len(types)
        inst.append((x, height(x, z) - sink, z, rng.uniform(0, 360), scale, types[key]))

    occupied = []               # (x, z, r) trunks and wrecks: no tree on top of another

    def free(x, z, r):
        return all(math.hypot(x - a, z - b) > r + c for a, b, c in occupied)
    for (x, z, r) in reclaim_pts:
        occupied.append((x, z, min(r, 3.0)))
    for (x, z, _s, _l) in lamps:
        occupied.append((x, z, 1.0))

    def street(x, z):
        main = abs(x) < FACADE - 0.3 and Z0 + 3 < z < SOUTH_END + 22
        cross = CROSS[0] + 0.3 < z < CROSS[1] - 0.3 and abs(x) < 70.0
        return main or cross

    # ground plants on a jittered 0.42 m grid
    step = 0.34
    for gz in np.arange(Z0 + 3, SOUTH_END + 22, step):
        for gx in np.arange(-72.0, 72.0, step):
            if abs(gx) > FACADE and not (CROSS[0] < gz < CROSS[1]):
                continue
            x, z = gx + rng.uniform(-0.18, 0.18), gz + rng.uniform(-0.18, 0.18)
            if not street(x, z):
                continue
            w = W[int(min(max((z - Z0) / SIZE * SPLAT_N, 0), SPLAT_N - 1)), int(min(max((x - X0) / SIZE * SPLAT_N, 0), SPLAT_N - 1))]
            g, s, mo = w[GRASS], w[SOIL], w[MOSS]
            p = 1.3 * g + 0.6 * s + 0.08 * mo
            if rng.random() > p:
                continue
            r = rng.random()
            if g > 0.5 and r < 0.16:
                put('grass_patch', x, z, rng.uniform(0.7, 1.05))
            elif r < 0.36:
                put(rng.choice(('grass_tall_a', 'grass_tall_b')), x, z, rng.uniform(0.8, 1.35))
            elif r < 0.56:
                put('grass_clump', x, z, rng.uniform(0.8, 1.3))
            elif r < 0.68:
                put('grass_short', x, z, rng.uniform(0.9, 1.4))
            elif r < 0.76:
                put(rng.choice(('dandelion_a', 'dandelion_a', 'dandelion_c')), x, z, rng.uniform(0.9, 1.3))
            elif r < 0.84:
                put(rng.choice(('celandine_a', 'celandine_b')), x, z, rng.uniform(0.8, 1.2))
            elif r < 0.93:
                put(rng.choice(('weed_a', 'weed_b')), x, z, rng.uniform(0.8, 1.2))
            elif abs(x) > 6.5 or s > 0.6:
                put('nettle', x, z, rng.uniform(0.8, 1.2))

    # shrubs: at the foot of the walls, along the kerbs, in the heaved patches of the road
    for _ in range(5200):
        x, z = rng.uniform(-FACADE, FACADE), rng.uniform(Z0 + 3, Z0 + SIZE - 3)
        if rng.random() < 0.18:
            x, z = rng.uniform(-70, 70), rng.uniform(CROSS[0] + 0.5, CROSS[1] - 0.5)
        if not street(x, z):
            continue
        w = at(W, x, z)
        if w[GRASS] + 0.5 * w[SOIL] < 0.55 or rng.random() > 0.075:
            continue
        if abs(x) < 2.2 and abs(z - CROSS_C) > 8 and NORTH_END < z < SOUTH_END and rng.random() < 0.8:
            continue                    # the bushes leave the trodden middle of the street open
        if not free(x, z, 0.7):
            continue
        put(rng.choice(('shrub_a', 'shrub_b', 'shrub_a', 'bush_dense', 'bush_wide')), x, z, rng.uniform(0.55, 0.95))
        occupied.append((x, z, 0.6))

    # wrecks sunk in the green: tall grass and nettles packed around the body up to the doors, a bush or a sapling
    # grown against one end
    for (wx, wz, yaw) in wrecks:
        a = math.radians(yaw)
        ax, az = math.sin(a), math.cos(a)               # length axis
        for _ in range(170):
            u, v = rng.uniform(-3.8, 3.8), rng.uniform(-2.6, 2.6)
            out = max(abs(u) - 2.1, abs(v) - 0.8, 0.0)  # distance out of the body's footprint
            if abs(u) < 1.9 and abs(v) < 0.65 or rng.random() > math.exp(-out / 0.9):
                continue
            x, z = wx + u * ax + v * az, wz + u * az - v * ax
            if not street(x, z):
                continue
            r = rng.random()
            key = (rng.choice(('grass_tall_a', 'grass_tall_b')) if r < 0.5 else 'nettle' if r < 0.68 else
                   'grass_clump' if r < 0.84 else rng.choice(('weed_a', 'weed_b')))
            put(key, x, z, rng.uniform(1.05, 1.5))
        if rng.random() < 0.75:
            u, v = rng.choice((-1, 1)) * rng.uniform(2.4, 2.9), rng.uniform(-0.7, 0.7)
            x, z = wx + u * ax + v * az, wz + u * az - v * ax
            if street(x, z):
                put(rng.choice(('shrub_a', 'shrub_b', 'bush_dense', 'sapling_a', 'sapling_c')), x, z, rng.uniform(0.6, 0.9))
                occupied.append((x, z, 0.8))

    # young trees on the road and pavements (self-seeded ash and maple, birch), big ones in a few places; past the
    # barricades, where nobody has walked for decades, the street is a young wood
    planted = 0
    tries = 0
    while planted < 95 and tries < 30000:
        tries += 1
        x, z = rng.uniform(-FACADE + 1.2, FACADE - 1.2), rng.uniform(NORTH_END + 6, SOUTH_END - 10)
        if rng.random() < 0.15:
            x, z = rng.uniform(-60, 60), rng.uniform(CROSS[0] + 1.5, CROSS[1] - 1.5)
        if not street(x, z):
            continue
        w = at(W, x, z)
        if w[GRASS] + w[SOIL] < 0.7 or not free(x, z, 2.4):
            continue
        if abs(x) < 1.8 and rng.random() < 0.7:
            continue                    # keep the trodden middle mostly open
        big = rng.random() < 0.32
        if big:
            key = rng.choice(('maple_a', 'maple_b', 'ash_a', 'ash_b', 'maple_b', 'birch_a', 'broadleaf_young'))
            sc = rng.uniform(0.75, 1.0)
        else:
            key = rng.choice(('sapling_a', 'sapling_b', 'sapling_c', 'sapling_a', 'birch_young', 'broadleaf_young'))
            sc = rng.uniform(0.8, 1.2)
        put(key, x, z, sc, sink=0.05)
        occupied.append((x, z, 2.8 if big else 1.6))
        planted += 1
    # debris: chunks of plaster, bricks and broken glass under the facades, planks, roofing sheets and rubbish on the
    # road, thicker toward the barricades
    for _ in range(2600):
        x, z = rng.uniform(-FACADE + 0.3, FACADE - 0.3), rng.uniform(Z0 + 3, Z0 + SIZE - 3)
        if rng.random() < 0.15:
            x, z = rng.uniform(-60, 60), rng.uniform(CROSS[0] + 0.5, CROSS[1] - 0.5)
        if not street(x, z):
            continue
        wall = FACADE - abs(x) < 1.6
        near_bar = min(abs(z - NORTH_END + 8), abs(z - SOUTH_END + 2)) < 14
        if rng.random() > (0.5 if near_bar else 0.28):
            continue
        if wall:
            key = rng.choice(('plaster_chunks', 'silicate_bricks', 'plaster_chunks', 'brick_heap', 'glass_shards', 'concrete_chunks'))
        else:
            key = rng.choice(('concrete_chunks', 'planks', 'sheet', 'glass_shards', 'tin_cans', 'bottles', 'twigs',
                              'concrete_chunks', 'plaster_chunks', 'glass_shards', 'concrete_chunks'))
        if key == 'brick_heap' and not free(x, z, 0.8):
            continue
        put('deb_' + key, x, z, rng.uniform(0.8, 1.3), sink=0.0)
    rrng = random.Random(77)    # own stream: everything else stays where it was
    for fx, za, zb in ruins:    # the collapse: brick heaps, slabs, planks and glass in front of the break
        for _ in range(int((zb - za + 4) * 2.2)):
            z = rrng.uniform(za - 2, zb + 2)
            x = fx - math.copysign(rrng.random() ** 1.3 * 6.5, fx)
            if not street(x, z):
                continue
            key = rrng.choice(('brick_heap', 'brick_heap', 'concrete_chunks', 'silicate_bricks', 'plaster_chunks',
                               'planks', 'glass_shards', 'sheet'))
            if 'deb_' + key not in types:
                types['deb_' + key] = len(types)
            inst.append((x, height(x, z), z, rrng.uniform(0, 360), rrng.uniform(0.9, 1.6), types['deb_' + key]))
    for (z_a, z_b, count) in ((Z0 + 3, NORTH_END - 8, 34), (SOUTH_END + 5, Z0 + SIZE - 3, 6)):
        planted = 0
        for _ in range(8000):
            if planted >= count:
                break
            x, z = rng.uniform(-FACADE + 1.0, FACADE - 1.0), rng.uniform(z_a, z_b)
            if not street(x, z) or not free(x, z, 2.2):
                continue
            big = rng.random() < 0.6
            key = (rng.choice(('maple_a', 'maple_b', 'ash_a', 'ash_b', 'birch_a', 'broadleaf_a', 'broadleaf_b')) if big else
                   rng.choice(('sapling_a', 'sapling_b', 'sapling_c', 'birch_young', 'broadleaf_young')))
            put(key, x, z, rng.uniform(0.75, 1.05) if big else rng.uniform(0.9, 1.3), sink=0.05)
            occupied.append((x, z, 3.0 if big else 1.6))
            planted += 1
    return inst, types


if __name__ == '__main__':
    if '--models' in sys.argv:
        build_models()
    build()
