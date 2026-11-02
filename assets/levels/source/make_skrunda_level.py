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
from zonegen.terrain import _poly_dist, _point_in_poly  # noqa: E402
import numpy as np                                    # noqa: E402
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import skrunda_probes                                 # noqa: E402

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
          cull=0, shadows=True, more_lods=()):
    L.append(f'model "{uname(name)}" "{path}" {x:.2f} {y:.2f} {z:.2f}')
    L.append(f'  parent "{parent}"')
    if yaw or scale != 1.0 or rx or rz:
        L.append(f'  transform {rx:.1f} {yaw:.1f} {rz:.1f} {scale:g}')
    if lod:
        L.append(f'  lod "{lod}" {lod_dist}')
        for f, d in more_lods:                  # further LOD levels, increasing distance
            L.append(f'  lod "{f}" {d:g}')
    if not collision:
        L.append('  collision 0')
    if cull:
        L.append(f'  cull_distance {cull:g}')
    if not shadows:
        L.append('  cast_shadows 0')


def gen(name, file, x, z, parent, yaw=0.0, lod_dist=60, has_lod=True, dy=0.0, **kw):
    model(name, G + file + '.glb', x, h(x, z) + dy, z, parent, yaw, lod=(G + file + '_lod1.glb') if has_lod else None,
          lod_dist=lod_dist, **kw)


# LOD2 thinned to a quarter of the blades (assets/models/zone/source/thin_glb.py): file, distance
FAR_LOD = {'grass_patch_tall': ('../models/zone/skrunda/veg/grass_patch_far.glb', 18),
           'sk:grass_fresh': ('../models/zone/skrunda/veg/grass_fresh_far.glb', 18),
           'sk:grass_dry': ('../models/zone/skrunda/veg/grass_dry_far.glb', 18),
           'weed_patch': ('../models/zone/skrunda/veg/weed_patch_far.glb', 16)}


FOLIAGE_TYPES = {}       # (file, lod_dist, cull, collision, shadows) -> type index
FOLIAGE = []             # (x, y, z, yaw, scale, type)
# brush density (instances / m2) and far density per kind, used when painting more in the editor
FOLIAGE_KIND = {'Tree': (0.03, 1.0), 'Bush': (0.02, 0.8), 'Sapling': (0.03, 0.8), 'Tall grass': (0.25, 0.5), 'Weeds': (0.1, 0.6),
                'Grass tuft': (0.3, 0.5), 'Crack grass': (0.2, 0.5), 'Rubble': (0.15, 0.5), 'Litter': (0.06, 0.5),
                'Twigs': (0.1, 0.5)}


def veg(name, file, x, z, parent, lod_dist=25, cull=60, collision=False, yaw=None, scale=1.0, dy=-0.03, shadows=False):
    """Vegetation becomes painted foliage of the engine terrain (instanced per 32 m cell); only trees and
    bushes cast shadows (ground cover is too small to matter)."""
    key = (name, file, lod_dist, cull, collision, shadows)
    if key not in FOLIAGE_TYPES:
        FOLIAGE_TYPES[key] = len(FOLIAGE_TYPES)
    FOLIAGE.append((x, h(x, z) + dy, z, rng.uniform(0, 360) if yaw is None else yaw, scale, FOLIAGE_TYPES[key]))


def foliage_lines():
    out = []
    for (name, file, lod_dist, cull, collision, shadows), _ in sorted(FOLIAGE_TYPES.items(), key=lambda kv: kv[1]):
        dens, far = FOLIAGE_KIND.get(name, (0.1, 1.0))
        if file.startswith('debris:'):      # ground debris (zonegen/debris.py): low poly, no LOD
            out.append(f'foliage_type "{SK}debris_{file[7:]}.glb" {dens:g} 0.8 1.2 {cull:g} 0 0 0 {far:g}')
            continue
        base = SK + 'veg/' + file[3:] if file.startswith('sk:') else V + file
        out.append(f'foliage_type "{base}.glb" {dens:g} 0.85 1.15 {cull:g} {int(collision)} 0 {int(shadows)} {far:g}')
        out.append(f'foliage_lod "{base}_lod1.glb" {lod_dist:g}')
        if file in FAR_LOD:
            out.append(f'foliage_lod "{FAR_LOD[file][0]}" {FAR_LOD[file][1]:g}')
        if name == 'Tree':
            out.append('foliage_impostor 90')     # baked impostor cards beyond 90 m (engine/Impostors)
    return out


def write_terrain(path):
    """Binary terrain next to the level (engine/src/Terrain.cpp, version 2): header, heights (row = Z),
    splat map (8 bytes per texel, one weight per layer), foliage instances."""
    import struct
    import numpy as np
    H = np.asarray(T.H, dtype='<f4')
    res = H.shape[0]
    assert H.shape == (res, res) and abs((res - 1) * T.res - site.SIZE) < 1e-6
    splat_n = 1024
    S = T.splat(splat_n)
    with open(path, 'wb') as f:
        f.write(struct.pack('<IIiifffI', 0x52524554, 2, res, splat_n, site.X0, site.Z0, site.SIZE, len(FOLIAGE)))
        f.write(H.tobytes())
        f.write(S.tobytes())
        for x, y, z, yaw, sc, t in FOLIAGE:
            f.write(struct.pack('<7fI', x, y, z, yaw, sc, 0.0, 0.0, t))


def prop(name, folder, x, y, z, parent, yaw=0.0, **kw):
    kw.setdefault('cull', 90)           # scanned props are 5k-40k triangles each and have no LOD
    model(name, f'{P}{folder}/{folder}.gltf', x, y, z, parent, yaw, **kw)


def gprop(name, folder, x, z, parent, yaw=0.0, dy=0.0, **kw):
    prop(name, folder, x, h(x, z) + dy, z, parent, yaw, **kw)


def entity(name, etype, x, y, z, yaw, char, parent, script='../scripts/enemy.lua', clip='Idle_Loop', props=(), size=None):
    sx, sy, sz = size or (0.6, 1.8, 0.6)
    L.append(f'entity "{uname(name)}" "{etype}" {x:.2f} {y:.2f} {z:.2f} {yaw:g} {sx:g} {sy:g} {sz:g} 0.55 0.5 0.5 "{script}"')
    L.append(f'  parent "{parent}"')
    if char:
        L.append(f'  character "{H}{char}.glb" 1 0 "{clip}"')
        L.append('  animgraph "../animgraphs/human.animgraph"')
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
    # engine terrain: heights, splat map and the painted vegetation are in skrunda1.terrain (Landscape and
    # Foliage modes in the editor); the asphalt and the ponds are the draped overlay models in group Terrain.
    # Layers in zonegen Terrain.SPLAT order, tiling = 1 / the photo's real size (Poly Haven) so grass blades,
    # leaves and pebbles keep their real scale.
    'terrain "skrunda1.terrain"',
    'terrain_layer 0 "../materials/skrunda/terrain_meadow.material" 0.4',
    'terrain_layer 1 "../materials/skrunda/terrain_dry.material" 0.5',
    'terrain_layer 2 "../materials/skrunda/terrain_lush.material" 0.5',
    'terrain_layer 3 "../materials/skrunda/terrain_dirt.material" 0.77',
    'terrain_layer 4 "../materials/skrunda/terrain_leaves.material" 0.33',
    'terrain_layer 5 "../materials/skrunda/terrain_gravel.material" 0.5',
    'terrain_layer 6 "../materials/skrunda/terrain_mud.material" 0.77',
    'terrain_layer 7 "../materials/skrunda/terrain_path.material" 0.32',
    '@FOLIAGE_TYPES@',
    # early spring day after the photos of Skrunda-1 on Wikimedia Commons: bright sky with fair-weather
    # clouds, crisp sun shadows, almost no haze, last year's straw-coloured grass
    'sun 0.42 -0.68 -0.6',
    'env sun_color 1 0.94 0.84',
    'env sun_intensity 3.4',
    'env sky_zenith 0.28 0.44 0.7',
    'env sky_horizon 0.74 0.8 0.86',
    'env ground_color 0.4 0.37 0.28',     # sunlit straw and walls bounce warm light into the shade
    'env cloud_cover 0.4',
    'env ambient_intensity 1.15',
    'env fog_color 0.72 0.78 0.85',
    'env fog_density 0.0022',
    'env fog_height 2',
    'env fog_height_falloff 0.03',
    'env fog_sun_scatter 0.8',
    'env exposure 0.85',
    'env bloom_intensity 0.04',
    'env bloom_threshold 1.4',
    'env ssao_radius 0.8',
    'env ssao_intensity 1.2',
    'env shadow_distance 90',
    'env saturation 0.95',
    'env contrast 1.06',
    'env tint 1.0 1.0 1.0',
    'env vignette 0.18',
    'env grain 0.02',
    # eyes adapt indoors: rooms lit through the windows stay readable, the windows burn out
    'env auto_exposure 0.8',
    'env exposure_min_ev -0.5',
    'env exposure_max_ev 3',
    'weapon "../weapons/ak74.weapon"',
    'weapon "../weapons/pistol.weapon"',
    'weapon "../weapons/knife.weapon"',
    'weapon "../weapons/smg.weapon"',
]
for g, p in (('Terrain', ''), ('Buildings', ''), ('Blocks', 'Buildings'), ('Public', 'Buildings'), ('Small', 'Buildings'),
             ('Fences', ''), ('Street', ''), ('Wrecks', 'Street'), ('Lamps', 'Street'), ('Camp', ''), ('Bandit base', ''),
             ('Clutter', ''), ('Flats', 'Clutter'), ('Wall writing', 'Clutter'), ('Dust', 'Clutter'), ('Radios', 'Clutter'),
             ('Vegetation', ''), ('Forest', 'Vegetation'), ('Trees', 'Vegetation'), ('Bushes', 'Vegetation'),
             ('Grass', 'Vegetation'), ('Weeds', 'Vegetation'), ('Debris', ''), ('Loot', ''), ('Markers', ''),
             ('NPC', ''), ('Stalkers', 'NPC'), ('Bandits', 'NPC')):
    group(g, p)

# ---------------------------------------------------------------------------- road / path overlays on the terrain
for j in range(site.NT):
    for i in range(site.NT):
        cx = site.X0 + (i + 0.5) * site.TILE
        cz = site.Z0 + (j + 0.5) * site.TILE
        if not os.path.exists(os.path.join(ROOT, 'assets', 'models', 'zone', 'skrunda', 'terrain', f'overlay_{i}_{j}.glb')):
            continue                    # no asphalt or pond on this tile
        model(f'Roads {i} {j}', f'{SK}terrain/overlay_{i}_{j}.glb', cx, 0, cz, 'Terrain',
              lod=f'{SK}terrain/overlay_{i}_{j}_lod1.glb', lod_dist=110, collision=False)

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
    for poly, level in T.pond:          # only near the pond: hollows elsewhere may lie lower and stay dry
        if h(x, z) < level + 0.25 and (_poly_dist(np.array([x]), np.array([z]), poly)[0] < 12.0 or
                                       _point_in_poly(np.array([x]), np.array([z]), poly)[0]):
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
    gprop('Gate block', 'concrete_road_barrier_02', gx, gz, 'Camp', yaw=b['yaw'])
    gx, gy, gz = to_world(HQ, hx + 0.6, 0, s * 4.6)
    gprop('Gate block', 'concrete_road_barrier_02', gx, gz, 'Camp', yaw=b['yaw'] + 90)
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
wrecks = [w for w in wrecks if w[3] in ('car_burnt', 'bus_small_cream')]
for x, z, yaw, f in wrecks:
    tilt = 0.0
    if f == 'car_burnt':
        tilt = rng.uniform(-3, 3)
    gen('Wreck', f, x, z, 'Wrecks', yaw=yaw, rz=tilt, lod_dist=70, dy=-0.05)
# bus stop shelter-like props and kiosk debris at the junction
gprop('Concrete barrier', 'concrete_road_barrier_02', -66.0, 60.0, 'Street', yaw=20)
gprop('Concrete barrier', 'concrete_road_barrier_02', -63.6, 61.2, 'Street', yaw=35)

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


# the three planned routes (also drawn on the plan); trees keep 2 m off them so the way stays open
ROUTES = {
    'main': [(px, pz), to_world(HQ, hx - 3, 0, 0)[::2], to_world(HQ, hx + 4, 0, 0)[::2], (-20, 49), (-66, 62), (-70, 25), (-74, -30), (-100, -28), (-150, -36),
             to_world(HOSP, 0, 0, BUILD[HOSP]['width'] / 2 + 3)[::2]],
    'flank': [(-66, 62), (-70, 40), (-62, -30), (-57, -96), (-120, -96), (-150, -100), (-170, -104), (-187, -102),
              to_world(HOSP, -BUILD[HOSP]['length'] / 2 + 8, 0, -BUILD[HOSP]['width'] / 2 - 2)[::2]],
    'tower': [(-150, -36), (-215, -34), (BUILD[TOWER]['cx'] + 4, BUILD[TOWER]['cz'] + 3)],
}


def near_route(x, z, r=2.0):
    for pts in ROUTES.values():
        for (ax, az), (bx, bz) in zip(pts, pts[1:]):
            vx, vz = bx - ax, bz - az
            t = max(0.0, min(1.0, ((x - ax) * vx + (z - az) * vz) / max(vx * vx + vz * vz, 1e-9)))
            if math.hypot(ax + vx * t - x, az + vz * t - z) < r:
                return True
    return False


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
                p = 0.3 if grove > 0.58 else 0.11
            else:
                p = 0.3 if grove > 0.6 else 0.05
            if rng.random() < p:
                trees.append((jx, jz, forest, grove))
        x += step
    z += step

def in_hq_yard(x, z, margin=1.5):
    """Inside the fenced stalker camp around the HQ (kept clear of trees by the stalkers)."""
    b = BUILD[HQ]
    a = math.radians(b['yaw'])
    c, s = math.cos(a), math.sin(a)
    dx, dz = x - b['cx'], z - b['cz']
    lx, lz = dx * c - dz * s, dx * s + dz * c
    return abs(lx) < b['length'] / 2 + 14 + margin and abs(lz) < b['width'] / 2 + 16 + margin


trees = [t for t in trees if not in_hq_yard(t[0], t[1]) and not near_route(t[0], t[1])]
for x, z, forest, grove in trees:
    r = rng.random()
    if forest:
        f = ('spruce_a' if r < 0.3 else 'spruce_b' if r < 0.45 else 'pine_a' if r < 0.6 else 'pine_b' if r < 0.68 else
             'birch_a' if r < 0.82 else 'birch_b' if r < 0.92 else 'spruce_young')
        parent = 'Forest'
    else:
        # photos of Skrunda-1 in spring: birches just leafing out, dark spruces between the blocks
        f = ('spruce_a' if r < 0.08 else 'spruce_b' if r < 0.13 else 'birch_a' if r < 0.43 else 'birch_b' if r < 0.65 else
             'birch_young' if r < 0.77 else 'broadleaf_a' if r < 0.86 else 'broadleaf_b' if r < 0.94 else 'dead_a')
        if f.startswith(('birch_a', 'birch_b', 'broadleaf')) and rng.random() < 0.5:
            f += '_spring'          # sparser, fresh-green crown (thin_glb + recolor_glb of the leaf variant)
        parent = 'Trees'
    veg('Tree', f if f == 'dead_a' else 'sk:' + f, x, z, parent, lod_dist=30, cull=230, shadows=True, collision=True, scale=rng.uniform(0.85, 1.15))

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
        veg('Bush', f, x, z, 'Bushes', lod_dist=20, cull=110, shadows=True)

# the town growing over: as in Pripyat after 25 years (Lacan et al. 2015), most of the new wood is seedlings and
# saplings, densest along the streets and around the mature trees (docs/LEVEL_ART_GUIDE.md). Own random stream
# (and own yaw), so everything placed after keeps its place.
srng = random.Random(1989)
for x, z, forest, grove in trees:
    if forest or srng.random() < 0.35:
        continue
    for _ in range(srng.randint(1, 3)):
        a, r = srng.uniform(0, 2 * math.pi), 2.5 + abs(srng.gauss(0.0, 4.0))
        jx, jz = x + r * math.cos(a), z + r * math.sin(a)
        if free(jx, jz, 1.0) and not near_route(jx, jz, 1.0) and not in_hq_yard(jx, jz) and T.field('ruts', jx, jz) < 0.0:
            veg('Sapling', srng.choice(['sk:birch_young', 'sk:birch_young', 'sk:spruce_young']), jx, jz, 'Trees',
                lod_dist=25, cull=150, shadows=True, collision=True, yaw=srng.uniform(0, 360), scale=srng.uniform(0.35, 0.9))
for k in range(12000):                  # street verges: a sapling every few tens of metres of kerb
    x, z = srng.uniform(TOWN[0], TOWN[2]), srng.uniform(TOWN[1], TOWN[3])
    rd = float(T.field('road', x, z))
    if -3.5 < rd < -1.0 and srng.random() < 0.12 and free(x, z, 1.0) and not near_route(x, z, 1.0) and not in_hq_yard(x, z):
        veg('Sapling', srng.choice(['sk:birch_young', 'sk:birch_young', 'sk:birch_young', 'sk:spruce_young']), x, z, 'Trees',
            lod_dist=25, cull=150, shadows=True, collision=True, yaw=srng.uniform(0, 360), scale=srng.uniform(0.5, 1.05))

# ground cover: tall grass everywhere it is not trodden, nettles and weeds at the walls. The grass follows the
# terrain layers under it (fresh green on the lush layer, straw elsewhere, little on bare dirt, none in wheel ruts)
# and grows in clumps: a slow noise thins it to open spots and thickens it to dense patches.
# Dense (2 m grid + a lower second layer) in the playable town, 3.5 m grid outside it.
grng = random.Random(4711)          # own stream: the rest of the level keeps its random sequence
z = site.Z0 + 1
while z < site.Z0 + site.SIZE - 1:
    inside_z = site.PLAY[1] < z < site.PLAY[3]
    x = site.X0 + 1
    while x < site.X0 + site.SIZE - 1:
        inside = inside_z and site.PLAY[0] < x < site.PLAY[2]
        g = 2.0 if inside else 3.5
        jx, jz = x + rng.uniform(-0.45, 0.45) * g, z + rng.uniform(-0.45, 0.45) * g
        if free(jx, jz, 0.6) and T.field('ruts', jx, jz) < 0.0:
            bd = float(T.field('bdist', jx, jz))
            clump = float(T.noise.fbm(jx + 333.0, jz - 777.0, 11.0, 2))
            dense = min(max((clump - 0.38) / 0.24, 0.0), 1.0)            # 0 open spot .. 1 dense patch
            bare = float(T.field('dirt', jx, jz)) > 0.2 or float(T.field('apron', jx, jz)) > -0.5
            lush = float(T.field('grass', jx, jz)) > -0.15

            def kind(dry_share):
                if lush:
                    return 'sk:grass_fresh' if grng.random() < 0.75 else 'sk:grass_dry'
                return 'sk:grass_dry' if grng.random() < dry_share else 'sk:grass_fresh'
            if bd < 5.0 and rng.random() < (0.3 if inside else 0.55):
                veg('Weeds', 'weed_patch' if rng.random() < 0.6 else 'nettle_tall_a', jx, jz, 'Weeds', lod_dist=8, cull=45)
            elif rng.random() < (0.62 + 0.36 * dense if inside else 0.12 + 0.75 * dense) * (0.5 if bare else 1.0):
                veg('Tall grass', kind(0.8), jx, jz, 'Grass', lod_dist=8, cull=50, scale=grng.uniform(0.75, 1.0) + 0.3 * dense)
                if inside and dense > 0.15:     # second, lower layer between the patches: a closed sward
                    veg('Tall grass', kind(0.85), jx + g * 0.5, jz + g * 0.5, 'Grass', lod_dist=8, cull=50,
                        scale=grng.uniform(0.55, 0.85))
            elif inside and rng.random() < 0.5:
                veg('Grass tuft', rng.choice(['grass_tall_a', 'grass_tall_b']), jx, jz, 'Grass', lod_dist=12, cull=40,
                    scale=rng.uniform(1.0, 1.4))
        x += g
    z += 2.0 if inside_z else 3.5

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

# ---------------------------------------------------------------------------- clutter (asset thread, CC0 / Poly Haven)
C = P + 'clutter/'


def clutter(name, file, x, y, z, parent, yaw=None, collision=False, cull=45, **kw):
    """Generated junk from models/props/clutter (origin at the bottom centre). Paper, rags and cardboard have
    no collision; bricks, blocks, pallets and rubble do."""
    model(name, C + file + '.glb', x, y, z, parent, rng.uniform(0, 360) if yaw is None else yaw, collision=collision,
          cull=cull, shadows=collision, **kw)


def scatter(bid, lx0, lx1, lz0, lz1, y, items, n, parent):
    for _ in range(n):
        f = rng.choice(items)
        x, yy, z = to_world(bid, rng.uniform(lx0, lx1), y, rng.uniform(lz0, lz1))
        solid = f.startswith(('brick', 'cinder', 'wooden_pallet', 'rubble', 'planks', 'cardboard_box'))
        clutter(f.replace('_', ' ').capitalize(), f, x, yy, z, parent, collision=solid)


frng = random.Random(1998)
PAPER = ['newspaper_sheet_a', 'newspaper_sheet_b', 'cardboard_flattened', 'rag_a', 'rag_b', 'newspaper_stack',
         'cardboard_box_crushed']
# inside the blocks: paper, rags and crushed boxes in the flats of every floor (people left in a hurry in 1998)
for bid, b in BUILD.items():
    if not b['model'].startswith('block'):
        continue
    sec = int(b['model'].split('_')[-1][:-1])
    sw = b['length'] / sec
    for k in range(sec):
        cx = -b['length'] / 2 + sw * (k + 0.5)
        for f in range(5):
            if rng.random() < 0.6:
                scatter(bid, cx - sw / 2 + 0.9, cx - 1.8, -b['width'] / 2 + 0.9, -0.9, floor_y(f) + 0.01, PAPER, 2, 'Clutter')
            if rng.random() < 0.6:
                scatter(bid, cx + 1.8, cx + sw / 2 - 0.9, -b['width'] / 2 + 0.9, -0.9, floor_y(f) + 0.01, PAPER, 2, 'Clutter')
    # left-behind furniture in some flats: a low table, a chair (sometimes knocked over), an old TV or a stool.
    # Own random stream so the rest of the level keeps its layout; only cheap scanned props (< 7k triangles).
    for k in range(sec):
        cx = -b['length'] / 2 + sw * (k + 0.5)
        for f in range(5):
            if frng.random() > 0.4:
                continue
            side = frng.choice((-1, 1))
            # the two big back rooms of a section: x between the outer wall and the partition at cx, z from the
            # back wall to the corridor wall at -0.5 (zonegen.skrunda.block); 2 m clear of every wall
            lx0, lx1 = (cx - sw / 2 + 2.2, cx - 2.2) if side < 0 else (cx + 2.2, cx + sw / 2 - 2.2)
            lx, lz = frng.uniform(lx0, lx1), frng.uniform(-b['width'] / 2 + 2.4, -2.2)
            yaw = b['yaw'] + frng.choice((0, 90, 180, 270)) + frng.uniform(-12, 12)
            x, y, z = to_world(bid, lx, floor_y(f) + 0.01, lz)
            prop('Flat table', 'WoodenTable_01', x, y, z, 'Flats', yaw=yaw, cull=25)
            for _ in range(frng.randint(2, 4)):     # plaster and bricks fallen off the outer wall, glass under the window
                f2 = frng.choice(['rubble_pile_small', 'rubble_pile_large', 'brick_pile', 'brick_pile', 'cinder_block_broken'])
                rx_, ry_, rz_ = to_world(bid, frng.uniform(lx0 - 1.0, lx1 + 1.0), floor_y(f) + 0.01, -b['width'] / 2 + frng.uniform(0.7, 1.1))
                clutter('Flat debris', f2, rx_, ry_, rz_, 'Flats', yaw=frng.uniform(0, 360), collision=False, cull=25)
            if frng.random() < 0.4:         # a worn bookshelf left standing against the back wall
                bx, by, bz = to_world(bid, frng.uniform(lx0, lx1), floor_y(f) + 0.01, -b['width'] / 2 + 0.25 + 0.4)
                prop('Flat bookshelf', 'wooden_bookshelf_worn', bx, by, bz, 'Flats', yaw=b['yaw'] + 90, cull=25)
            a = math.radians(-yaw)
            for item, d, coll in ((frng.choice(['SchoolChair_01', 'plastic_monobloc_chair_01', 'folding_wooden_stool']), 1.1, False),
                                  (frng.choice(['Television_01', 'television_02', 'folding_wooden_stool']), -1.2, False)):
                ix, iz = x + math.cos(a) * d, z + math.sin(a) * d
                tipped = frng.random() < 0.35
                prop('Flat ' + item.split('_')[0].lower(), item, ix, y + (0.25 if tipped and 'Chair' in item else 0), iz, 'Flats',
                     yaw=frng.uniform(0, 360), rz=(80 if tipped else 0), collision=coll, cull=25, shadows=False)
    # outside: rubble, bricks and broken cinder blocks under the balconies and at the gable ends
    for k in range(int(b['length'] / 9)):
        lx = rng.uniform(-b['length'] / 2 + 1, b['length'] / 2 - 1)
        x, y, z = to_world(bid, lx, 0, -(b['width'] / 2 + rng.uniform(0.8, 2.2)))
        f = rng.choice(['rubble_pile_small', 'rubble_pile_small', 'brick_pile', 'cinder_block_broken', 'rubble_pile_large'])
        clutter('Rubble', f, x, h(x, z) - 0.05, z, 'Debris', collision=True, cull=70)
    for s in (-1, 1):
        x, y, z = to_world(bid, s * (b['length'] / 2 + 1.5), 0, rng.uniform(-3, 3))
        clutter('Rubble', 'rubble_pile_small', x, h(x, z) - 0.05, z, 'Debris', collision=True, cull=70)

# hospital: boxes, papers and a medical box along the corridors (looted)
b = BUILD[HOSP]
for f in range(3):
    scatter(HOSP, -b['length'] / 2 + 6, b['length'] / 2 - 6, -1.0, 1.0, floor_y(f) + 0.01,
            ['newspaper_sheet_a', 'newspaper_sheet_b', 'cardboard_box_crushed', 'cardboard_flattened', 'rag_a'], 10, 'Clutter')
x, y, z = to_world(HOSP, -b['length'] / 2 + 8.4, floor_y(2), -b['width'] / 2 + 3.2)
prop('Medical box', 'medical_box', x, y, z, 'Loot', yaw=rng.uniform(0, 360))
x, y, z = to_world(HOSP, -b['length'] / 2 + 6.8, floor_y(2), -b['width'] / 2 + 2.0)
clutter('Lyokha rag', 'rag_a', x, y + 0.01, z, 'Loot')

# Venta: the bandits' loot corner in the back room, pallets and cinder blocks outside
b = BUILD[VENTA]
for i, (lx, lz, f) in enumerate(((-b['length'] / 2 + 1.6, -b['width'] / 2 + 1.4, 'plastic_crate_01'),
                                 (-b['length'] / 2 + 2.3, -b['width'] / 2 + 1.3, 'plastic_crate_02'),
                                 (-b['length'] / 2 + 1.6, -b['width'] / 2 + 2.2, 'russian_food_cans_01'),
                                 (-b['length'] / 2 + 3.2, -b['width'] / 2 + 1.2, 'long_life_food'),
                                 (-b['length'] / 2 + 3.9, -b['width'] / 2 + 1.3, 'wine_bottles_01'),
                                 (-b['length'] / 2 + 1.5, -b['width'] / 2 + 3.2, 'metal_toolbox'),
                                 (-b['length'] / 2 + 2.6, -b['width'] / 2 + 2.6, 'vintage_suitcase'))):
    x, y, z = to_world(VENTA, lx, floor_y(0), lz)
    prop('Bandit loot', f, x, y, z, 'Bandit base', yaw=rng.uniform(-20, 20) + b['yaw'])
for lx, lz, f in ((-4.0, -b['width'] / 2 - 1.2, 'wooden_pallet'), (-2.6, -b['width'] / 2 - 1.3, 'wooden_pallet_broken'),
                  (3.0, -b['width'] / 2 - 1.0, 'cinder_block'), (3.5, -b['width'] / 2 - 1.1, 'cinder_block_broken'),
                  (b['length'] / 2 + 1.2, 1.0, 'planks_scrap_pile')):
    x, y, z = to_world(VENTA, lx, 0, lz)
    clutter('Bandit junk', f, x, h(x, z) - 0.02, z, 'Bandit base', collision=True)
x, y, z = to_world(VENTA, 0.8, floor_y(0), 0.6)
for f, dx, dz in (('wine_bottles_01', 0.6, 0.2), ('cigarette_pack', -0.5, 0.4)):
    xx, yy, zz = to_world(VENTA, 0.8 + dx, floor_y(0), 0.6 + dz)
    prop('Bandit fire junk', f, xx, yy, zz, 'Bandit base', yaw=rng.uniform(0, 360))

# HQ camp: supplies stacked by the wall, rags and cans by the fire
b = BUILD[HQ]
for lx, lz, f in ((-7.5, b['width'] / 2 + 1.2, 'plastic_crate_03'), (-6.7, b['width'] / 2 + 1.1, 'plastic_crate_01'),
                  (-7.1, b['width'] / 2 + 1.9, 'industrial_pastic_container'), (7.2, b['width'] / 2 + 1.2, 'russian_food_cans_01'),
                  (2.0 + 1.1, b['width'] / 2 + 8.0 + 1.3, 'pot_enamel_01')):
    x, y, z = to_world(HQ, lx, 0, lz)
    gprop('Camp supplies', f, x, z, 'Camp', yaw=rng.uniform(0, 360))
for lx, lz, f in ((9.0, b['width'] / 2 + 1.0, 'wooden_pallet'), (9.2, b['width'] / 2 + 1.05, 'wooden_pallet'),
                  (2.0 - 1.6, b['width'] / 2 + 8.0 + 1.4, 'rag_b')):
    x, y, z = to_world(HQ, lx, 0, lz)
    clutter('Camp junk', f, x, h(x, z) + (0.15 if lx > 9.1 else 0.0), z, 'Camp', collision=f != 'rag_b')

# ---------------------------------------------------------------------------- objective, loot, markers
b = BUILD[HOSP]
# Lyokha's PDA and body remains: top floor, last ward at the far end, back side
ox, oy, oz = to_world(HOSP, -b['length'] / 2 + 7.6, floor_y(2), -b['width'] / 2 + 2.6)
prop('Lyokha backpack', 'old_military_crate', ox, oy, oz, 'Loot', yaw=b['yaw'] + 10)
prop('Lyokha gas mask', 'old_gas_mask', ox + 0.6, oy, oz + 0.2, 'Loot', yaw=40)
prop('Lyokha ammo', 'ammo_box', ox - 0.4, oy + 0.36, oz, 'Loot', yaw=-20)
entity('Objective: Lyokha PDA', 'objective', ox, oy, oz, 0, None, 'Loot', script='../scripts/objective.lua',
       props=(('radius', '2'), ('message', '@objective.skrunda1.lyokha_pda')),
       size=(0.05, 0.05, 0.05))
b = BUILD[TOWER]
tx, ty, tz = b['cx'], b['y'], b['cz']
prop('Stash crate', 'wooden_military_crate', tx + 0.8, ty + 0.05, tz - 0.6, 'Loot', yaw=30)
prop('Stash jerrycan', 'metal_jerrycan_green', tx - 1.0, ty + 0.05, tz - 0.2, 'Loot', yaw=70)
prop('Stash ammo', 'ammo_box', tx + 0.8, ty + 0.5, tz - 0.6, 'Loot', yaw=10)
entity('Objective: water tower stash', 'objective', tx, ty + 0.1, tz, 0, None, 'Loot', script='../scripts/objective.lua',
       props=(('radius', '2.2'), ('message', '@objective.skrunda1.lyokha_stash')), size=(0.05, 0.05, 0.05))
x, y, z = to_world(VENTA, 6, floor_y(0), -6.5)
prop('Bandit ammo', 'ammo_box', x, y, z, 'Loot', yaw=15)
x, y, z = to_world('845508052', 0, 0.15, 0)
prop('Medkit box', 'metal_tool_chest', x, y, z, 'Loot', yaw=20)

# ---------------------------------------------------------------------------- anomalies
# Each one guards a route decision (engine/Anomaly.h; edited in the editor's Anomaly section):
#  - a discharge just outside the camp teaches the detector and the bolt (G) on a safe stretch,
#  - a heat vent in the cracked asphalt of Novaya street: the fast route is visible from afar and must be timed,
#  - the "safe" flank through the courtyards pays with a gravity trap and a second discharge,
#  - sludge leaking from the hospital stores sits by its back door,
#  - the old X-ray room keeps a radiation hotspot, the water tower stash a contaminated area,
#  - a rift on the way to the tower throws the careless into the reeds.
group('Anomalies')


def anomaly(name, kind, near, side, size, props=(), y=None):
    """Anomaly next to a route point: `near` (x, z), shifted `side` metres sideways until clear of buildings."""
    x, z = near
    r = max(size[0], size[2]) / 2
    for k in range(24):
        a = math.radians(k * 15)
        cx, cz = x + math.cos(a) * side, z + math.sin(a) * side
        if T.field('bdist', cx, cz) > r + 0.5:
            x, z = cx, cz
            break
    gy = h(x, z) if y is None else y
    L.append(f'entity "{uname(name)}" "anomaly" {x:.2f} {gy:.2f} {z:.2f} 0 {size[0]:g} {size[1]:g} {size[2]:g} 0.6 0.6 0.6 ""')
    L.append('  parent "Anomalies"')
    L.append(f'  prop "kind" "{kind}"')
    for k_, v in props:
        L.append(f'  prop "{k_}" "{v}"')
    return x, z


def along(points, t):
    """Point at fraction t of a route polyline."""
    segs = list(zip(points, points[1:]))
    lens = [math.hypot(bx - ax, bz - az) for (ax, az), (bx, bz) in segs]
    d = t * sum(lens)
    for ((ax, az), (bx, bz)), l in zip(segs, lens):
        if d <= l:
            return ax + (bx - ax) * d / l, az + (bz - az) * d / l
        d -= l
    return points[-1]


main, flank, tower = ROUTES['main'], ROUTES['flank'], ROUTES['tower']
anomaly('Discharge at the camp', 'discharge', (-20.0, 49.0), 5.0, (3.5, 3, 3.5), (('damage', '20'),))
anomaly('Vent on Novaya', 'vent', ((-74 - 100) / 2, (-30 - 28) / 2), 3.0, (2.2, 5, 2.2), (('period', '7'), ('column', '6')))
anomaly('Crusher in the courtyard', 'crusher', along(flank, 0.3), 4.0, (6, 3, 6))
anomaly('Discharge in the courtyard', 'discharge', along(flank, 0.62), 2.5, (3, 3, 3))
anomaly('Acid by the back door', 'acid', flank[-1], 6.0, (7, 1, 7))
b = BUILD[HOSP]
xr, yr, zr = to_world(HOSP, b['length'] / 2 - 6, floor_y(0), 0.0)
anomaly('Radiation: X-ray room', 'radiation', (xr, zr), 0.0, (6, 3.5, 6), (('intensity', '15'),), y=yr)
b = BUILD[TOWER]
anomaly('Radiation: water tower', 'radiation', (b['cx'], b['cz']), 0.0, (16, 8, 16),
        (('area', '1'), ('intensity', '3'), ('fade', '4')), y=b['y'] - 1)
anomaly('Rift by the tower path', 'rift', along(tower, 0.55), 3.0, (2.5, 2.5, 2.5), (('jump', '14'),))

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

# player footsteps and the wind over the whole map (added by the audio work, kept when regenerating)
L.append('player_sounds "../sounds/player/step_gravel.sound" "../sounds/player/land.sound" "../sounds/player/hurt.sound"')
L.append('entity "Ambience wind" "sound" 0 -100 0 0 4000 400 4000 0.25 0.55 0.95 ""')
L += ['  prop "sound" "sounds/ambient/wind.sound"', '  prop "zone" "1"', '  prop "fade" "50"']

# ---------------------------------------------------------------------------- ground debris (terrain foliage)
# Paintable with the Foliage brush: brick and plaster fallen off the walls, broken glass under the windows, cans
# and bottles at the entrances and kerbs where people sat, twigs under the trees. Own random stream.
drng = random.Random(2024)


def debris(kind, name, x, z, scale=1.0):
    veg(kind, 'debris:' + name, x, z, 'Debris', cull=40, yaw=drng.uniform(0, 360), scale=scale, dy=-0.01)


def pick(table):
    r, acc = drng.random(), 0.0
    for name, p in table:
        acc += p
        if r < acc:
            return name
    return table[-1][0]


WALL = [('silicate_bricks', 0.33), ('plaster_chunks', 0.35), ('glass_shards', 0.2), ('concrete_chunks', 0.12)]
KERB = [('tin_cans', 0.35), ('bottles', 0.25), ('glass_shards', 0.2), ('concrete_chunks', 0.2)]
YARD = [('concrete_chunks', 0.3), ('plaster_chunks', 0.2), ('tin_cans', 0.2), ('bottles', 0.15), ('silicate_bricks', 0.15)]
for _ in range(42000):
    x, z = drng.uniform(TOWN[0], TOWN[2]), drng.uniform(TOWN[1], TOWN[3])
    if in_hq_yard(x, z, 0.0) or near_route(x, z, 1.0):
        continue
    d = T.field('bdist', x, z)
    on_hard = T.field('road', x, z) > -0.3 or T.field('path', x, z) > -0.2
    if 0.15 < d < 2.6 and not on_hard:
        if drng.random() < 0.55 * (1.0 - d / 3.0):
            debris('Rubble', pick(WALL), x, z, drng.uniform(0.8, 1.2))
    elif -1.2 < T.field('road', x, z) < -0.1:
        if drng.random() < 0.1:
            debris('Litter', pick(KERB), x, z)
    elif drng.random() < 0.012 and free(x, z, 0.5):
        debris('Litter', pick(YARD), x, z)
# where people sat: entrances of the blocks
for bid, b in BUILD.items():
    if not b['model'].startswith('block'):
        continue
    sec = int(b['model'].split('_')[-1][:-1])
    for s_ in range(sec):
        cx = -b['length'] / 2 + b['length'] / sec * (s_ + 0.5)
        for _ in range(drng.randint(1, 3)):
            x, _y, z = to_world(bid, cx + drng.uniform(-2.5, 2.5), 0, b['width'] / 2 + drng.uniform(1.4, 4.0))
            debris('Litter', pick(KERB), x, z)
# twigs under the trees
TREE_TYPES = {i for (kind, *_), i in FOLIAGE_TYPES.items() if kind == 'Tree'}
for fx, fy, fz, *_r, ft in list(FOLIAGE):
    if ft in TREE_TYPES and drng.random() < 0.45:
        for _ in range(drng.randint(1, 2)):
            a, r = drng.uniform(0, 2 * math.pi), drng.uniform(0.8, 3.0)
            debris('Twigs', 'twigs', fx + r * math.cos(a), fz + r * math.sin(a), drng.uniform(0.8, 1.3))

# ---------------------------------------------------------------------------- wall writing and stains
# Decal quads (zonegen/graffiti.py, build_skrunda.py decals) 2 cm off the walls, no collision. Each one has a
# reason: the last conscripts' slogans and the entrance numbers from Soviet times, soot over the flats that
# burnt, damp where the roof leaks, stalker marks along Lyokha's way to the hospital, the bandits' warnings
# at the Venta. Own random stream, so the rest of the level does not move.
wrng = random.Random(1991)


def writing(design, bid, lx, ly, lz, turn, scale=1.0, ground=False):
    """Decal on a building wall: building-local point (bottom centre), turn 0 = faces +Z, 180 = -Z,
    90 = +X, -90 = -X; ground=True puts ly above the ground outside the wall instead of the building origin."""
    x, y, z = to_world(bid, lx, ly, lz)
    if ground:
        a = math.radians(BUILD[bid]['yaw'] + turn)
        y = h(x + 0.6 * math.sin(a), z + 0.6 * math.cos(a)) + ly
    model(design.replace('_', ' ').capitalize(), SK + f'graffiti_{design}.glb', x, y, z, 'Wall writing',
          BUILD[bid]['yaw'] + turn, scale=scale, collision=False, cull=70, shadows=False)


SLOGANS = ['no_exit', 'tsoi', 'dmb', 'tsoi', 'no_exit']
OFF = 0.02
for bid, b in BUILD.items():
    if not b['model'].startswith('block'):
        continue
    sec = int(b['model'].split('_')[-1][:-1])
    L_, W_ = b['length'], b['width']
    sw = L_ / sec
    k = sw / 13.0
    hx, hz = L_ / 2, W_ / 2
    floors = 5
    if sec >= 2:                       # entrance 2, flats 11-20, stencilled beside the door
        writing('entrance', bid, -hx + sw * 1.5 + 1.2, floor_y(0) + 1.25, hz + OFF, 0)
    # slogans low on the plinth under the ground-floor windows, between the entrances or on the balcony side
    for _ in range(wrng.choice((1, 1, 2))):
        side = wrng.choice((1, -1))
        s = wrng.randrange(sec)
        cx = -hx + sw * (s + 0.5)
        lx = cx + wrng.choice((-1, 1)) * 3.4 * k             # clear of the door, under the window sills
        writing(wrng.choice(SLOGANS), bid, lx, 0.22, side * (hz + OFF), 0 if side > 0 else 180, ground=True, scale=1.25)
    # gable ends: the windowless strip in the middle (windows sit 3 m off the centre line)
    for gx in (-1, 1):
        r = wrng.random()
        turn = 90 if gx > 0 else -90
        if r < 0.35:                   # the roof leaks down the gable
            writing('leak', bid, gx * (hx + OFF), floor_y(floors) - 2.2, wrng.uniform(-0.8, 0.8), turn,
                    scale=wrng.uniform(0.9, 1.2))
        elif r < 0.6:
            writing('damp', bid, gx * (hx + OFF), floor_y(0) + wrng.uniform(0.2, 0.6), wrng.uniform(-0.6, 0.6), turn,
                    scale=wrng.uniform(0.7, 0.9))
        elif r < 0.8:
            writing(wrng.choice(SLOGANS), bid, gx * (hx + OFF), 0.4, wrng.uniform(-0.6, 0.6), turn, ground=True,
                    scale=wrng.uniform(1.5, 1.9))
    # two or three flats burnt out: soot over the window, below the window of the flat above
    for _ in range(wrng.randint(2, 3)):
        f = wrng.randrange(floors - 1)
        s = wrng.randrange(sec)
        cx = -hx + sw * (s + 0.5)
        if wrng.random() < 0.5:
            lx = cx + wrng.choice((-4.6, -2.5, 2.5, 4.6)) * k
            writing('soot', bid, lx, floor_y(f) + 2.28, hz + OFF, 0, scale=0.55)
        else:
            lx = cx + wrng.choice((-1.6, 1.6)) * k
            writing('soot', bid, lx, floor_y(f) + 2.28, -(hz + OFF), 180, scale=0.55)

# the bandits mark their base, Lyokha marked his way to the hospital, a warning at the X-ray end
b = BUILD[VENTA]
OFFP = 0.1                             # public buildings: their models are rounded up and clad, stand further off
writing('keep_out', VENTA, -b['length'] / 2 - OFFP, 0.5, 0.0, -90, ground=True, scale=1.6)
writing('cross', VENTA, -b['length'] / 2 - OFFP, 2.0, -4.5, -90, ground=True, scale=1.4)
b = BUILD[HOSP]
writing('lyokha', HOSP, -b['length'] / 2 - OFF, 0.9, 1.3, -90, ground=True, scale=1.7)
writing('arrow_clear', HOSP, -b['length'] / 2 - OFF, 0.4, -1.6, -90, ground=True, scale=1.5)
writing('radiation', HOSP, b['length'] / 2 + OFF, 0.5, 0.0, 90, ground=True, scale=1.5)
b = BUILD[HQ]
writing('dmb', HQ, b['length'] / 2 + OFFP, 0.5, 0.0, 90, ground=True, scale=1.8)
writing('damp', HQ, -b['length'] / 2 - OFFP, 0.3, 0.0, -90, ground=True, scale=0.8)
b = BUILD[TOWER]
for ang in (40.0,):                    # on the brick shaft beside the door (round: 7 cm off so the edges stay out)
    a = math.radians(ang)
    writing('radiation', TOWER, math.sin(a) * (2.95 + 0.07), 0.9, math.cos(a) * (2.95 + 0.07), ang, ground=True)

# ---------------------------------------------------------------------------- radios and dust in the light
# Radios left on (assets/sounds/source/radio.py): the stalkers' set in the camp plays an old waltz, the bandits
# have one in the Venta, and in an empty flat of the sniper's block a set hisses on a dead band with a beacon.
RADIO = 'sounds/ambient/radio_waltz.sound'
b = BUILD[HQ]
x, y, z = to_world(HQ, -5.0, floor_y(0) + 0.85, -3.0)
entity('Camp radio', 'sound', x, y, z, 0, None, 'Radios', script='', props=(('sound', RADIO),), size=(0.4, 0.4, 0.4))
b = BUILD[VENTA]
x, y, z = to_world(VENTA, -b['length'] / 2 + 3.2, floor_y(0), -b['width'] / 2 + 1.0)
prop('Bandit radio', 'vintage_radio_transceiver', x, y, z, 'Radios', yaw=b['yaw'] + 160, collision=False)
entity('Bandit radio sound', 'sound', x, y + 0.3, z, 0, None, 'Radios', script='', props=(('sound', RADIO),),
       size=(0.4, 0.4, 0.4))
b = BUILD[SNIPER_BLOCK]
x, y, z = to_world(SNIPER_BLOCK, -b['length'] / 2 + 3.4, floor_y(1), -b['width'] / 2 + 1.6)
prop('Abandoned radio', 'vintage_radio_transceiver', x, y, z, 'Radios', yaw=b['yaw'] + 25, collision=False)
entity('Abandoned radio sound', 'sound', x, y + 0.3, z, 0, None, 'Radios', script='',
       props=(('sound', 'sounds/ambient/radio_beacon.sound'),), size=(0.4, 0.4, 0.4))

# dust floating in the rooms (lit particles: they show where the sun comes through the windows)
DUST = 'particles/dust_motes.particle'


def dust(bid, lx, ly, lz):
    x, y, z = to_world(bid, lx, ly, lz)
    entity('Dust', 'particle', x, y, z, 0, None, 'Dust', script='', props=(('effect', DUST),), size=(0.4, 0.4, 0.4))


for bid, b in BUILD.items():
    if not b['model'].startswith('block'):
        continue
    sec = int(b['model'].split('_')[-1][:-1])
    sw = b['length'] / sec
    for _ in range(2):                 # two rooms per block, behind the windows on either side
        cx = -b['length'] / 2 + sw * (wrng.randrange(sec) + 0.5)
        side = wrng.choice((1, -1))
        dust(bid, cx + wrng.choice((-1, 1)) * 2.5 * sw / 13.0, floor_y(wrng.randrange(5)) + 0.9, side * (b['width'] / 2 - 2.2))
for bid, n, floors in ((HOSP, 4, 3), (HQ, 3, 3), (CANTEEN, 2, 1), (VENTA, 1, 1)):
    b = BUILD[bid]
    for i in range(n):
        lx = -b['length'] / 2 + b['length'] * (i + 0.5) / n
        dust(bid, lx, floor_y(i % floors) + 0.9, wrng.choice((1, -1)) * (b['width'] / 2 - 2.5))

# interior volumes (dark rooms, sheltered rain, room reverb) were sized in the editor: carried over from the
# current level file, like other hand-made entities, so regenerating keeps them
if os.path.exists(OUT):
    keep, prev = False, open(OUT, encoding='utf-8').read().splitlines()
    kept = []
    for line in prev:
        if line.startswith('entity ') and ('" "interior" ' in line or '" "irradiance_volume" ' in line):
            keep = True
        elif not line.startswith('  '):
            keep = False
        if keep:
            kept.append(line)
    kept += [line for line in prev if line.startswith('streaming ')]      # Level > World streaming settings
    if kept:
        if 'group "Interiors" "Buildings"' not in L:
            L.insert(next(k for k, l in enumerate(L) if l.startswith('group ')), 'group "Interiors" "Buildings"')
        # light probe volumes per storey (skrunda_probes.py), made from the interiors when there are none yet
        if not any('" "irradiance_volume" ' in l for l in kept):
            kept += skrunda_probes.volumes(kept)
        L.insert(next(k for k, l in enumerate(L) if l.startswith('group ')), f'group "{skrunda_probes.GROUP}" "Buildings"')
        L += kept
i = L.index('@FOLIAGE_TYPES@')
L[i:i + 1] = foliage_lines()
with open(OUT, 'w', encoding='utf-8') as f:
    f.write('\n'.join(L) + '\n')
write_terrain(os.path.join(HERE, '..', 'skrunda1.terrain'))

hq = BUILD[HQ]
plan = {
    'routes': ROUTES,
    'labels': [(px + 4, pz + 6, 'START: tábor ve Štábu'), (ox - 70, oz - 10, 'CÍL: PDA, 3. patro'),
               (-250, -5, 'ТД «Вента» (bandité)'), (-300, -45, 'vodárna (skrýš)'), (sx + 6, sz, 'odstřelovač, 5. patro'),
               (-140, -130, 'boční cesta dvory'), (-300, -180, 'rybník a bažina')],
}
with open(PLAN, 'w') as f:
    json.dump(plan, f, indent=1)
print('wrote', os.path.normpath(OUT), sum(1 for l in L if l.startswith('model')), 'models',
      sum(1 for l in L if l.startswith('entity')), 'entities', len(FOLIAGE), 'foliage in', len(FOLIAGE_TYPES), 'types')
