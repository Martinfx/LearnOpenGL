#!/usr/bin/env python3
"""Writes assets/levels/skrunda1.level: the abandoned Soviet radar town Skrunda-1 (Mezaine, Latvia),
built 1:1 from OpenStreetMap data (map data (c) OpenStreetMap contributors, ODbL 1.0).

    python3 -I assets/models/zone/source/build_skrunda.py            # buildings
    python3 -I assets/models/zone/source/build_skrunda.py terrain    # terrain tiles
    python3 -I assets/levels/source/make_skrunda_level.py            # this level

Real layout: 13 five-storey blocks on Novaya street, the garrison hospital, the "Venta" department store,
the canteen, the HQ building in its compound, the brick water tower and the reed pond at the west edge.
The story and the people are fictional.

Story: stalkers camp in the old HQ (Штаб). Lyokha went to the hospital (Госпиталь) for medicine three
days ago and did not come back. Find his PDA on the top floor of the hospital and return to the HQ.
Bandits hold the "Venta" store across the street from the hospital and keep a sniper on the 5th floor
of the block at the town entrance. Lyokha's stash is hidden in the water tower (optional).

Routes: main = along Novaya street (fast, under the sniper); flank = through the overgrown courtyards
behind the blocks to the hospital's back door; west = past the bandit store to the water tower.
"""
import json
import math
import os
import random
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, '..', '..', '..'))
sys.path.insert(0, os.path.join(ROOT, 'assets', 'models', 'zone', 'source'))

import build_skrunda                                  # noqa: E402
from zonegen import skrunda_site as site              # noqa: E402
from zonegen.skrunda import BASE                      # noqa: E402
from zonegen.buildings import STOREY                  # noqa: E402

OUT = os.path.join(HERE, '..', 'skrunda1.level')
PLAN = os.path.join(HERE, 'skrunda1_plan.json')
SK = '../models/zone/skrunda/'
G = '../models/zone/gen/'
V = '../models/zone/vegetation/'
H = '../models/characters/humans/'
P = '../models/props/'

rng = random.Random(1991)
T = site.terrain(build_skrunda.BY_ID)
L = []
used = {}


def uname(base):
    used[base] = used.get(base, 0) + 1
    return base if used[base] == 1 else f'{base} {used[base]}'


def h(x, z):
    return float(T.height(x, z))


def group(name, parent=''):
    L.append(f'group "{name}" "{parent}"')


def model(name, path, x, y, z, parent, yaw=0.0, scale=1.0, lod=None, lod_dist=60, rx=0.0, rz=0.0, collision=True,
          cull=0):
    L.append(f'model "{uname(name)}" "{path}" {x:.2f} {y:.2f} {z:.2f}')
    L.append(f'  parent "{parent}"')
    if yaw or scale != 1.0 or rx or rz:
        L.append(f'  transform {rx:.1f} {yaw:.1f} {rz:.1f} {scale:g}')
    if lod:
        L.append(f'  lod "{lod}" {lod_dist}')
    if not collision:
        L.append('  collision 0')
    if cull:
        L.append(f'  cull_distance {cull:g}')


def gen(name, file, x, z, parent, yaw=0.0, lod_dist=60, has_lod=True, dy=0.0, **kw):
    model(name, G + file + '.glb', x, h(x, z) + dy, z, parent, yaw, lod=(G + file + '_lod1.glb') if has_lod else None,
          lod_dist=lod_dist, **kw)


def veg(name, file, x, z, parent, lod_dist=25, cull=60, collision=False, yaw=None, scale=1.0, dy=-0.03):
    model(name, V + file + '.glb', x, h(x, z) + dy, z, parent, rng.uniform(0, 360) if yaw is None else yaw, scale=scale,
          lod=V + file + '_lod1.glb', lod_dist=lod_dist, collision=collision, cull=cull)


def prop(name, folder, x, y, z, parent, yaw=0.0, **kw):
    model(name, f'{P}{folder}/{folder}.gltf', x, y, z, parent, yaw, **kw)


def gprop(name, folder, x, z, parent, yaw=0.0, dy=0.0, **kw):
    prop(name, folder, x, h(x, z) + dy, z, parent, yaw, **kw)


def entity(name, etype, x, y, z, yaw, char, parent, script='../scripts/enemy.lua', clip='Idle_Loop', props=(), size=None):
    sx, sy, sz = size or (0.6, 1.8, 0.6)
    L.append(f'entity "{uname(name)}" "{etype}" {x:.2f} {y:.2f} {z:.2f} {yaw:g} {sx:g} {sy:g} {sz:g} 0.55 0.5 0.5 "{script}"')
    L.append(f'  parent "{parent}"')
    if char:
        L.append(f'  character "{H}{char}.glb" 1 0 "{clip}"')
    for k, v in props:
        L.append(f'  prop "{k}" "{v}"')


# ---------------------------------------------------------------------------- buildings
BUILD = {}           # osm id -> dict with model, x, y, z, yaw, n, length, width
for b in T.blist:
    yaw, n = site.placement(T, b)
    BUILD[b['id']] = dict(b, yaw=yaw, n=n)


def to_world(bid, lx, ly, lz):
    """Building-local point -> world (local +X along the long side, +Z out of the entrance side, y = 0 ground)."""
    b = BUILD[bid]
    a = math.radians(b['yaw'])
    c, s = math.cos(a), math.sin(a)
    return b['cx'] + lx * c + lz * s, b['y'] + ly, b['cz'] - lx * s + lz * c


def facing(bid, lx_dir, lz_dir):
    """Entity yaw (0 = +X, 90 = +Z) for a building-local direction."""
    b = BUILD[bid]
    a = math.radians(b['yaw'])
    c, s = math.cos(a), math.sin(a)
    wx, wz = lx_dir * c + lz_dir * s, -lx_dir * s + lz_dir * c
    return math.degrees(math.atan2(wz, wx)) % 360


def floor_y(f):
    return BASE + f * STOREY


HOSP, VENTA, HQ, TOWER = '184415688', '184415682', '184415657', '184415695'
SNIPER_BLOCK, CANTEEN = '184415675', '184415665'

# ---------------------------------------------------------------------------- header
px, py, pz = to_world(HQ, 3.0, 0.0, 12.0)
L += [
    '# FPS engine level',
    '# Skrunda-1 (Mezaine, Latvia): abandoned Soviet radar town, 1:1 from OpenStreetMap',
    '# (map data (c) OpenStreetMap contributors, ODbL 1.0). Story and people are fictional.',
    '# Generated by assets/levels/source/make_skrunda_level.py, then edited in the editor.',
    'version 5',
    f'player_start {px:.2f} {py + 0.1:.2f} {pz:.2f}',
    f'map_bounds {site.X0:g} {site.Z0:g} {site.SIZE:g}',
    'sun 0.55 -0.42 -0.35',              # overcast late afternoon, sun low in the south-west
    'env sun_color 1 0.84 0.66',
    'env sun_intensity 1.9',
    'env sky_zenith 0.36 0.4 0.44',
    'env sky_horizon 0.72 0.7 0.64',
    'env ground_color 0.2 0.21 0.16',
    'env cloud_cover 0.82',
    'env ambient_intensity 0.8',
    'env fog_color 0.62 0.63 0.6',
    'env fog_density 0.0065',
    'env fog_height 3',
    'env fog_height_falloff 0.04',
    'env fog_sun_scatter 1.2',
    'env exposure 1.05',
    'env bloom_intensity 0.05',
    'env bloom_threshold 1.3',
    'env ssao_radius 0.8',
    'env ssao_intensity 1.3',
    'env shadow_distance 80',
    'env saturation 0.78',
    'env contrast 1.1',
    'env tint 1.0 1.0 0.96',
    'env vignette 0.32',
    'env grain 0.03',
    'weapon "../weapons/ak74.weapon"',
    'weapon "../weapons/pistol.weapon"',
    'weapon "../weapons/knife.weapon"',
    'weapon "../weapons/smg.weapon"',
]
for g, p in (('Terrain', ''), ('Buildings', ''), ('Blocks', 'Buildings'), ('Public', 'Buildings'), ('Small', 'Buildings'),
             ('Fences', ''), ('Street', ''), ('Wrecks', 'Street'), ('Lamps', 'Street'), ('Camp', ''), ('Bandit base', ''),
             ('Vegetation', ''), ('Forest', 'Vegetation'), ('Trees', 'Vegetation'), ('Bushes', 'Vegetation'),
             ('Grass', 'Vegetation'), ('Weeds', 'Vegetation'), ('Debris', ''), ('Loot', ''), ('Markers', ''),
             ('NPC', ''), ('Stalkers', 'NPC'), ('Bandits', 'NPC')):
    group(g, p)

# ---------------------------------------------------------------------------- terrain tiles
for j in range(site.NT):
    for i in range(site.NT):
        cx = site.X0 + (i + 0.5) * site.TILE
        cz = site.Z0 + (j + 0.5) * site.TILE
        model(f'Terrain {i} {j}', f'{SK}terrain/tile_{i}_{j}.glb', cx, 0, cz, 'Terrain',
              lod=f'{SK}terrain/tile_{i}_{j}_lod1.glb', lod_dist=110)

# ---------------------------------------------------------------------------- buildings
NAMES = {HOSP: 'Hospital (Госпиталь)', VENTA: 'Store Venta', HQ: 'HQ (Штаб)', TOWER: 'Water tower', CANTEEN: 'Canteen'}
for bid, b in BUILD.items():
    nm = NAMES.get(bid) or ('Block ' + (b['tags'].get('note', '').replace('former address ', '') or bid[-3:]))
    parent = 'Blocks' if b['model'].startswith('block') else ('Public' if bid in NAMES else 'Small')
    has_lod = os.path.exists(os.path.join(ROOT, 'assets', 'models', 'zone', 'skrunda', b['model'] + '_lod1.glb'))
    model(nm, SK + b['model'] + '.glb', b['cx'], b['y'], b['cz'], parent, b['yaw'],
          lod=(SK + b['model'] + '_lod1.glb') if has_lod else None, lod_dist=90)


def free(x, z, r=1.5):
    """True when (x, z) is clear of buildings, asphalt, paths and water."""
    if T.field('bdist', x, z) < r + 1.0:
        return False
    if T.field('road', x, z) > -0.3 or T.field('path', x, z) > -0.2 or T.field('apron', x, z) > -0.2:
        return False
    for k in ('unclassified', 'service', 'primary', 'secondary'):
        if T.field('road_' + k, x, z) < site.ROAD_HALF[k] + r:
            return False
    for poly, level in T.pond:
        if h(x, z) < level + 0.25:
            return False
    return True


# ---------------------------------------------------------------------------- HQ compound: stalker camp
# PO-2 fence around the HQ yard, gate towards the town, one fallen panel at the back
b = BUILD[HQ]
hx, hz = b['length'] / 2 + 14, b['width'] / 2 + 16


def fence_line(x0, z0, x1, z1, parent, gap=None, broken_at=None):
    """PO-2 panels from (x0, z0) to (x1, z1) in 20 m runs; gap = (t0, t1) along the line left open."""
    L_ = math.hypot(x1 - x0, z1 - z0)
    ux, uz = (x1 - x0) / L_, (z1 - z0) / L_
    yaw = -math.degrees(math.atan2(uz, ux))
    t = 0.0
    k = 0
    while t < L_ - 0.5:
        run = 20.0 if L_ - t >= 20.0 else 4.0
        if gap and gap[0] - run < t < gap[1]:
            t += 4.0
            continue
        x, z = x0 + ux * t, z0 + uz * t
        f = 'fence_po2_x5' if run == 20.0 else 'fence_po2_x1'
        if run == 20.0 and broken_at is not None and k == broken_at:
            f = 'fence_po2_x5_broken'
        y = min(h(x, z), h(x + ux * run, z + uz * run)) - 0.05
        model('Fence PO-2', SK + f + '.glb', x, y, z, parent, yaw, lod=SK + f + '_lod1.glb', lod_dist=50)
        t += run
        k += 1


corners = [to_world(HQ, sx * hx, 0, sz * hz) for sx, sz in ((-1, -1), (1, -1), (1, 1), (-1, 1))]
(ax, _, az), (bx, _, bz), (cx_, _, cz_), (dx, _, dz) = corners
fence_line(ax, az, bx, bz, 'Fences', broken_at=1)                       # back side
fence_line(bx, bz, cx_, cz_, 'Fences', gap=(hz - 4, hz + 4))         # side gate towards the town
fence_line(cx_, cz_, dx, dz, 'Fences', gap=(hx - 4, hx + 4))           # front with the gate in the middle
fence_line(dx, dz, ax, az, 'Fences')
# gate: concrete blocks and an old barrier
for s in (-1, 1):
    gx, gy, gz = to_world(HQ, s * 4.6, 0, hz + 0.6)
    gprop('Gate block', 'concrete_road_barrier', gx, gz, 'Camp', yaw=b['yaw'])
    gx, gy, gz = to_world(HQ, hx + 0.6, 0, s * 4.6)
    gprop('Gate block', 'concrete_road_barrier', gx, gz, 'Camp', yaw=b['yaw'] + 90)
# camp in the yard in front of the HQ entrance
cxw, cyw, czw = to_world(HQ, 2.0, 0, b['width'] / 2 + 8.0)
gprop('Camp fire', 'stone_fire_pit', cxw, czw, 'Camp')
for k, (lx, lz, f, ry) in enumerate(((-2.2, 0.4, 'old_tyre', 0), (2.3, -0.3, 'wooden_crate_02', 20), (0.3, 2.4, 'old_military_crate', 80),
                                    (-1.4, -2.0, 'cement_bag', 10))):
    x, y, z = to_world(HQ, 2.0 + lx, 0, b['width'] / 2 + 8.0 + lz)
    gprop('Camp seat', f, x, z, 'Camp', yaw=ry, rx=90 if f == 'old_tyre' else 0, dy=0.08 if f == 'old_tyre' else 0)
for lx in (-3.0, 3.0):
    x, y, z = to_world(HQ, lx, floor_y(0) + 0.0, b['width'] / 2 - 1.5)
    prop('Lantern', 'Lantern_01', x, y, z, 'Camp')
x, y, z = to_world(HQ, -6.0, floor_y(0), 0.0)
prop('Barrel stove', 'barrel_stove', x, y, z, 'Camp')
x, y, z = to_world(HQ, -5.0, floor_y(0), -3.0)
prop('Radio', 'vintage_radio_transceiver', x, y + 0.75, z, 'Camp', yaw=b['yaw'])
prop('Desk', 'metal_office_desk', x, y, z, 'Camp', yaw=b['yaw'])
# sandbag positions at the gate (behind the fence, both sides of the opening)
for lx in (-8.0, 8.0):
    x, y, z = to_world(HQ, lx, 0, hz - 1.2)
    model('Sandbags', SK + 'sandbags_5m.glb', x, h(x, z) - 0.03, z, 'Camp', b['yaw'])

# stalkers
sx, sy, sz = to_world(HQ, 0.5, 0, b['width'] / 2 + 6.4)
entity('Stalker Mitya', 'empty', sx, h(sx, sz), sz, facing(HQ, 0.3, 1), 'stalker', 'Stalkers', script='', clip='Crouch_Idle_Loop')
sx, sy, sz = to_world(HQ, 3.9, 0, b['width'] / 2 + 9.6)
entity('Stalker Sych', 'empty', sx, h(sx, sz), sz, facing(HQ, -1, -0.5), 'worker', 'Stalkers', script='', clip='Idle_Talking_Loop')
sx, sy, sz = to_world(HQ, -1.5, 0, hz + 1.5)
entity('Stalker at the gate', 'empty', sx, h(sx, sz), sz, facing(HQ, 0, 1), 'soldier', 'Stalkers', script='')
sx, sy, sz = to_world(HQ, -5.5, floor_y(0), -1.5)
entity('Stalker radio operator', 'empty', sx, sy, sz, facing(HQ, 0, -1), 'stalker', 'Stalkers', script='', clip='Idle_Loop')

# ---------------------------------------------------------------------------- Venta: bandit base
b = BUILD[VENTA]
for lx, lz, f in ((-4, 3.0, 'Barrel_01'), (-3.2, 3.6, 'Barrel_02'), (5, 2.5, 'wooden_crate_01'), (6, 3.2, 'old_tyre'),
                  (9, 2.0, 'trashbag'), (-9, 2.2, 'trashbag')):
    x, y, z = to_world(VENTA, lx, 0, b['width'] / 2 + lz)
    gprop('Bandit junk', f, x, z, 'Bandit base', yaw=rng.uniform(0, 360))
for lx in (-4.5, 4.5):                 # sandbag walls in front of the entrance
    x, y, z = to_world(VENTA, lx, 0, b['width'] / 2 + 4.8)
    model('Sandbags', SK + 'sandbags_3m.glb', x, h(x, z) - 0.03, z, 'Bandit base', b['yaw'] + (8 if lx > 0 else -8))
x, y, z = to_world(VENTA, 0, floor_y(0), 0)
prop('Bandit fire barrel', 'barrel_stove', x, y, z, 'Bandit base')
for lx, lz in ((-2, 1.5), (2.2, -1.0)):
    x, y, z = to_world(VENTA, lx, floor_y(0), lz)
    prop('Mattress crate', 'wooden_military_crate', x, y, z, 'Bandit base', yaw=rng.uniform(0, 360))

# ---------------------------------------------------------------------------- street furniture, wrecks
# lamp posts along the main street (Novaya) and the entrance road, every ~30 m on the east side
for r in site.load()['roads']:
    if r['tags'].get('highway') != 'unclassified':
        continue
    pts = r['pts']
    acc = 0.0
    for (ax, az), (bx, bz) in zip(pts[:-1], pts[1:]):
        seg = math.hypot(bx - ax, bz - az)
        ux, uz = (bx - ax) / seg, (bz - az) / seg
        t = (30.0 - acc) % 30.0
        while t < seg:
            x, z = ax + ux * t + uz * 5.2, az + uz * t - ux * 5.2
            if site.PLAY[0] < x < site.PLAY[2] and site.PLAY[1] < z < site.PLAY[3] and T.field('bdist', x, z) > 3:
                lean = rng.random() < 0.25
                gen('Lamp post', 'lamp_post_leaning' if lean else 'lamp_post', x, z, 'Lamps',
                    yaw=-math.degrees(math.atan2(uz, ux)) + (180 if lean else 90), has_lod=False, dy=-0.1)
            t += 30.0
        acc = (acc + seg) % 30.0

# abandoned cars where people left them: parked at the blocks, one in the ditch, a bus at the stop
CARS = ['car_sedan_green', 'car_sedan_blue', 'car_sedan_cream', 'car_estate_rust', 'car_estate_red', 'car_burnt', 'car_van_green']
wrecks = [(-74.0, 40.0, 92, 'car_sedan_blue'), (-74.5, 46.5, 88, 'car_estate_rust'), (-66.6, -12.0, 94, 'car_burnt'),
          (-140.5, -36.6, 2, 'car_van_green'), (-104.0, -34.0, 178, 'car_sedan_blue'), (-157.0, 24.0, 85, 'car_estate_red'),
          (-61.0, -150.0, 95, 'car_sedan_green'), (-12.0, 45.0, 36, 'bus_small_cream'), (-58.0, 74.0, 120, 'car_burnt'),
          (-160.0, -100.0, 86, 'car_sedan_green')]
for x, z, yaw, f in wrecks:
    tilt = 0.0
    if f == 'car_burnt':
        tilt = rng.uniform(-3, 3)
    gen('Wreck', f, x, z, 'Wrecks', yaw=yaw, rz=tilt, lod_dist=70, dy=-0.05)
# bus stop shelter-like props and kiosk debris at the junction
gprop('Concrete barrier', 'concrete_road_barrier_02', -66.0, 60.0, 'Street', yaw=20)
gprop('Concrete barrier', 'concrete_road_barrier', -63.6, 61.2, 'Street', yaw=35)

# puddles in the potholes of the main roads
for k in range(70):
    x, z = rng.uniform(-200, 120), rng.uniform(-260, 150)
    if T.field('road', x, z) > 0.4 and T.field('road_unclassified', x, z) < 2.5:
        gen('Puddle', 'puddle_a' if rng.random() < 0.6 else 'puddle_b', x, z, 'Street', yaw=rng.uniform(0, 360),
            has_lod=False, dy=0.035, collision=False, cull=120)

# ---------------------------------------------------------------------------- vegetation
FOREST = [a['pts'] for a in site.load()['areas'] if a['tags'].get('landuse') == 'forest' or a['tags'].get('natural') == 'wood']


def in_poly(x, z, poly):
    inside = False
    n = len(poly)
    for i in range(n):
        (x0, z0), (x1, z1) = poly[i], poly[(i + 1) % n]
        if (z0 > z) != (z1 > z) and x < (x1 - x0) * (z - z0) / (z1 - z0 + 1e-12) + x0:
            inside = not inside
    return inside


def edge_dist(x, z):
    return min(x - site.X0, site.X0 + site.SIZE - x, z - site.Z0, site.Z0 + site.SIZE - z)


TOWN = (-200.0, -255.0, 10.0, 60.0)        # x0, z0, x1, z1 of the block grid (courtyards)


def in_town(x, z):
    return TOWN[0] < x < TOWN[2] and TOWN[1] < z < TOWN[3]


trees = []
step = 4.2
z = site.Z0 + 2
while z < site.Z0 + site.SIZE - 2:
    x = site.X0 + 2
    while x < site.X0 + site.SIZE - 2:
        jx, jz = x + rng.uniform(-1.8, 1.8), z + rng.uniform(-1.8, 1.8)
        if free(jx, jz, 2.5):
            ed = edge_dist(jx, jz)
            grove = float(T.field('grove', jx, jz))
            forest = ed < 45 or any(in_poly(jx, jz, p) for p in FOREST)
            if forest:
                p = 0.75 if ed < 30 else 0.45
            elif in_town(jx, jz):
                p = 0.22 if grove > 0.58 else 0.035
            else:
                p = 0.3 if grove > 0.6 else 0.05
            if rng.random() < p:
                trees.append((jx, jz, forest, grove))
        x += step
    z += step

for x, z, forest, grove in trees:
    r = rng.random()
    if forest:
        f = ('spruce_a' if r < 0.3 else 'spruce_b' if r < 0.45 else 'pine_a' if r < 0.6 else 'pine_b' if r < 0.68 else
             'birch_a' if r < 0.82 else 'birch_b' if r < 0.92 else 'spruce_young')
        parent = 'Forest'
    else:
        f = ('birch_a' if r < 0.35 else 'birch_b' if r < 0.6 else 'birch_young' if r < 0.75 else 'broadleaf_a' if r < 0.85 else
             'broadleaf_b' if r < 0.93 else 'dead_a')
        parent = 'Trees'
    veg('Tree', f, x, z, parent, lod_dist=45, cull=320, scale=rng.uniform(0.85, 1.15))

# bushes along walls and fences, undergrowth at forest edges
for k in range(2600):
    x, z = rng.uniform(site.X0 + 5, site.X0 + site.SIZE - 5), rng.uniform(site.Z0 + 5, site.Z0 + site.SIZE - 5)
    if not free(x, z, 1.2):
        continue
    bd = float(T.field('bdist', x, z))
    near_tree = bool(trees) and rng.random() < 0.5
    if 2.5 < bd < 7.0 or (edge_dist(x, z) < 60 and rng.random() < 0.5) or rng.random() < 0.12:
        r = rng.random()
        f = 'bush_a' if r < 0.3 else 'bush_b' if r < 0.5 else 'bush_c' if r < 0.65 else 'bush_wide_a' if r < 0.85 else 'bush_dense_a'
        veg('Bush', f, x, z, 'Bushes', lod_dist=30, cull=150)

# ground cover: tall grass everywhere it is not trodden, nettles and weeds at the walls, dry grass patches.
# Dense (2.6 m grid, one 2.5 m patch per cell = closed sward) in the playable town, sparse outside it.
z = site.Z0 + 1
while z < site.Z0 + site.SIZE - 1:
    inside_z = site.PLAY[1] < z < site.PLAY[3]
    x = site.X0 + 1
    while x < site.X0 + site.SIZE - 1:
        inside = inside_z and site.PLAY[0] < x < site.PLAY[2]
        g = 2.6 if inside else 5.0
        jx, jz = x + rng.uniform(-0.45, 0.45) * g, z + rng.uniform(-0.45, 0.45) * g
        if free(jx, jz, 0.6):
            bd = float(T.field('bdist', jx, jz))
            dry = T.field('dry', jx, jz) > 0
            if bd < 5.0 and rng.random() < (0.3 if inside else 0.55):
                veg('Weeds', 'weed_patch' if rng.random() < 0.6 else 'nettle_tall_a', jx, jz, 'Weeds', lod_dist=18, cull=55)
            elif rng.random() < (0.88 if inside else 0.55):
                veg('Tall grass', 'grass_patch_tall', jx, jz, 'Grass', lod_dist=20, cull=60,
                    scale=rng.uniform(0.85, 1.15))
                if dry and rng.random() < 0.7:
                    for _ in range(2):
                        dx, dz = jx + rng.uniform(-1.2, 1.2), jz + rng.uniform(-1.2, 1.2)
                        veg('Dry grass', rng.choice(['grass_dry_a', 'grass_dry_b', 'grass_dry_c']), dx, dz, 'Grass',
                            lod_dist=15, cull=40, scale=rng.uniform(1.4, 2.0))
            elif inside and rng.random() < 0.5:
                veg('Grass tuft', rng.choice(['grass_tall_a', 'grass_tall_b']), jx, jz, 'Grass', lod_dist=12, cull=40,
                    scale=rng.uniform(1.0, 1.4))
        x += g
    z += 2.6 if inside_z else 5.0

# grass and weeds in the cracks and potholes of the asphalt, along the kerbs
for k in range(5000):
    x, z = rng.uniform(-220, 160), rng.uniform(-270, 200)
    rd = T.field('road', x, z)
    near = min(float(T.field('road_' + kk, x, z)) - site.ROAD_HALF[kk] for kk in ('unclassified', 'service'))
    if near < 0.5 and -0.6 < rd < 0.0:
        veg('Crack grass', rng.choice(['grass_clump_a', 'grass_short_a', 'weed_a', 'weed_b', 'grass_tall_b']), x, z,
            'Grass', lod_dist=12, cull=40, dy=0.02)

# ---------------------------------------------------------------------------- debris at the blocks
for bid, b in BUILD.items():
    if not b['model'].startswith('block'):
        continue
    for k in range(int(b['length'] / 12)):
        lx = rng.uniform(-b['length'] / 2 + 2, b['length'] / 2 - 2)
        side = rng.choice((-1, 1))
        x, y, z = to_world(bid, lx, 0, side * (b['width'] / 2 + rng.uniform(2.5, 5)))
        r = rng.random()
        if r < 0.4:
            gen('Rubble', rng.choice(['rubble_a', 'rubble_b']), x, z, 'Debris', yaw=rng.uniform(0, 360), lod_dist=40)
        elif r < 0.6:
            gprop('Tyre', 'old_tyre', x, z, 'Debris', yaw=rng.uniform(0, 360), rx=90, dy=0.1)
        elif r < 0.8:
            gprop('Trash', 'trashbag', x, z, 'Debris', yaw=rng.uniform(0, 360))
        else:
            gprop('Bucket', 'wooden_bucket_02', x, z, 'Debris', yaw=rng.uniform(0, 360))

# ---------------------------------------------------------------------------- objective, loot, markers
b = BUILD[HOSP]
# Lyokha's PDA and body remains: top floor, last ward at the far end, back side
ox, oy, oz = to_world(HOSP, -b['length'] / 2 + 7.6, floor_y(2), -b['width'] / 2 + 2.6)
prop('Lyokha backpack', 'old_military_crate', ox, oy, oz, 'Loot', yaw=b['yaw'] + 10)
prop('Lyokha gas mask', 'old_gas_mask', ox + 0.6, oy, oz + 0.2, 'Loot', yaw=40)
prop('Lyokha ammo', 'ammo_box', ox - 0.4, oy + 0.36, oz, 'Loot', yaw=-20)
entity('Objective: Lyokha PDA', 'objective', ox, oy, oz, 0, None, 'Loot', script='../scripts/objective.lua',
       props=(('radius', '2'), ('message', 'PDA Ljochy nalezeno: "skrys je pod vodarnou". Vrat se do Stabu.')),
       size=(0.05, 0.05, 0.05))
b = BUILD[TOWER]
tx, ty, tz = b['cx'], b['y'], b['cz']
prop('Stash crate', 'wooden_military_crate', tx + 0.8, ty + 0.05, tz - 0.6, 'Loot', yaw=30)
prop('Stash jerrycan', 'metal_jerrycan_green', tx - 1.0, ty + 0.05, tz - 0.2, 'Loot', yaw=70)
prop('Stash ammo', 'ammo_box', tx + 0.8, ty + 0.5, tz - 0.6, 'Loot', yaw=10)
entity('Objective: water tower stash', 'objective', tx, ty + 0.1, tz, 0, None, 'Loot', script='../scripts/objective.lua',
       props=(('radius', '2.2'), ('message', 'Ljochova skrys nalezena.')), size=(0.05, 0.05, 0.05))
x, y, z = to_world(VENTA, 6, floor_y(0), -6.5)
prop('Bandit ammo', 'ammo_box', x, y, z, 'Loot', yaw=15)
x, y, z = to_world('845508052', 0, 0.15, 0)
prop('Medkit box', 'metal_tool_chest', x, y, z, 'Loot', yaw=20)

MARK = [('Stab (tabor)', HQ), ('Gospital', HOSP), ('TD Venta (bandite)', VENTA), ('Vodarna', TOWER), ('Jidelna', CANTEEN)]
for name, bid in MARK:
    entity(name, 'map_marker', BUILD[bid]['cx'], BUILD[bid]['y'], BUILD[bid]['cz'], 0, None, 'Markers', script='',
           size=(0.3, 0.3, 0.3))
entity('Ul. Novaja', 'map_marker', -68.0, h(-68, -100), -100.0, 0, None, 'Markers', script='', size=(0.3, 0.3, 0.3))
entity('Rybnik', 'map_marker', -255.0, 0, -150.0, 0, None, 'Markers', script='', size=(0.3, 0.3, 0.3))

# ---------------------------------------------------------------------------- bandits
patrol = lambda r: (('patrol_radius', str(r)),)
b = BUILD[SNIPER_BLOCK]
# sniper: 5th floor, flat window over the junction of the entrance road and Novaya street
sx, sy, sz = to_world(SNIPER_BLOCK, b['length'] / 2 - 4.6, floor_y(4), b['width'] / 2 - 1.1)
entity('Bandit sniper', 'enemy', sx, sy, sz, facing(SNIPER_BLOCK, 0, 1), 'bandit', 'Bandits',
       props=(('patrol_radius', '0.5'), ('view_range', '70'), ('shoot_range', '60'), ('accuracy', '0.45')))
for lx, lz, r in ((-3, 6.5, 3), (4, 7.0, 2), (0, 1.0, 4), (-8, -4, 3)):
    x, y, z = to_world(VENTA, lx, 0 if lz > 5 else floor_y(0), BUILD[VENTA]['width'] / 2 + lz - 5 if lz > 5 else lz)
    entity('Bandit', 'enemy', x, h(x, z) if lz > 5 else y, z, facing(VENTA, 0, 1), 'bandit', 'Bandits', props=patrol(r))
b = BUILD[HOSP]
for lx, lz in ((-2.5, b['width'] / 2 + 3.0), (2.5, b['width'] / 2 + 3.5)):
    x, y, z = to_world(HOSP, lx + 0.0, 0, lz)
    entity('Bandit hospital guard', 'enemy', x, h(x, z), z, facing(HOSP, 0, 1), 'bandit', 'Bandits', props=patrol(2))
x, y, z = to_world(HOSP, 8.0, floor_y(1), 0.0)
entity('Bandit hospital patrol', 'enemy', x, y, z, facing(HOSP, -1, 0), 'bandit', 'Bandits', props=patrol(8))
x, y, z = to_world(HOSP, -b['length'] / 2 + 10, floor_y(2), 0.0)
entity('Bandit at the PDA', 'enemy', x, y, z, facing(HOSP, 1, 0), 'bandit', 'Bandits', props=patrol(4))
x, y, z = -120.0, 0, -33.0
entity('Bandit street patrol', 'enemy', x, h(x, z), z, 0, 'bandit', 'Bandits', props=patrol(10))
x, z = BUILD[TOWER]['cx'] + 5, BUILD[TOWER]['cz'] + 4
entity('Bandit at the tower', 'enemy', x, h(x, z), z, 200, 'bandit', 'Bandits', props=patrol(4))

with open(OUT, 'w', encoding='utf-8') as f:
    f.write('\n'.join(L) + '\n')

hq = BUILD[HQ]
plan = {
    'routes': {
        'main': [(px, pz), to_world(HQ, hx - 3, 0, 0)[::2], to_world(HQ, hx + 4, 0, 0)[::2], (-20, 49), (-66, 62), (-70, 25), (-74, -30), (-100, -28), (-150, -36),
                 to_world(HOSP, 0, 0, BUILD[HOSP]['width'] / 2 + 3)[::2]],
        'flank': [(-66, 62), (-70, 40), (-62, -30), (-57, -96), (-120, -96), (-150, -100), (-170, -104), (-187, -102),
                  to_world(HOSP, -BUILD[HOSP]['length'] / 2 + 8, 0, -BUILD[HOSP]['width'] / 2 - 2)[::2]],
        'tower': [(-150, -36), (-215, -34), (BUILD[TOWER]['cx'] + 4, BUILD[TOWER]['cz'] + 3)],
    },
    'labels': [(px + 4, pz + 6, 'START: tábor ve Štábu'), (ox - 70, oz - 10, 'CÍL: PDA, 3. patro'),
               (-250, -5, 'ТД «Вента» (bandité)'), (-300, -45, 'vodárna (skrýš)'), (sx + 6, sz, 'odstřelovač, 5. patro'),
               (-140, -130, 'boční cesta dvory'), (-300, -180, 'rybník a bažina')],
}
with open(PLAN, 'w') as f:
    json.dump(plan, f, indent=1)
print('wrote', os.path.normpath(OUT), sum(1 for l in L if l.startswith('model')), 'models',
      sum(1 for l in L if l.startswith('entity')), 'entities', len(trees), 'trees')
