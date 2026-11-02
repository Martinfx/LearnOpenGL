#!/usr/bin/env python3
"""Puts an engine terrain under an existing, hand-edited level that was built on flat ground boxes
(zone.level, hospital_street.level), without regenerating the level, so every editor change stays.

    python3 -I assets/levels/source/add_terrain.py zone|hospital_street [...]

What it does to <level>.level (in place) and writes next to it (<level>.terrain, version 2):
  - the flat ground boxes listed in CONFIG are removed; roads, sidewalks and other hard surfaces stay boxes;
  - heights: everything already in the level stays where it is. The ground is level (at the old box top,
    -0.02 m) within a few metres of every building, wreck, track, fence and character, and rises into rolling
    hills, hollows and banks further away, with higher ground at the map edges that closes the view;
  - 8 ground layers (Skrunda-1 materials): the old box regions become soft-edged layer areas (grass, mud,
    gravel, leaf litter ...), plus gravel along roads, trodden dirt at buildings, bare soil on steep banks;
  - trees, bushes, grass models and loose rubble are dropped onto the new ground (y only);
  - ground cover: clumps of straw and fresh grass and weeds by walls as terrain foliage (Foliage mode);
  - environment: thinner haze and more natural colour (CONFIG 'env').

Footprints come from the models' glTF bounds (accessor min/max through the node tree), rotated by the yaw.
Run it again after big layout changes; it replaces its own terrain lines and foliage. CC0, part of the engine.
"""
import json
import math
import os
import random
import re
import struct
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, '..', '..', '..'))
LEVELS = os.path.join(ROOT, 'assets', 'levels')
sys.path.insert(0, os.path.join(ROOT, 'assets', 'models', 'zone', 'source'))
from zonegen.terrain import Noise, smoothstep          # noqa: E402

SKM = '../materials/skrunda/'
LAYERS = ['terrain_meadow', 'terrain_dry', 'terrain_lush', 'terrain_dirt', 'terrain_leaves', 'terrain_gravel',
          'terrain_mud', 'terrain_path']
TILING = [0.4, 0.5, 0.5, 0.77, 0.33, 0.5, 0.77, 0.32]
MEADOW, DRY, LUSH, DIRT, LEAVES, GRAVEL, MUD, PATH = range(8)
SK = '../models/zone/skrunda/'
V = '../models/zone/vegetation/'
# loose things that follow the ground (name prefixes); everything else is fixed and the ground levels under it
LOOSE = ('birch', 'dead_tree', 'bush', 'grass_', 'rubble', 'slab_fallen', 'Tyre', 'Barrel')

CONFIG = {
    'zone': dict(
        x0=-365.0, z0=-470.0, size=730.0, seed=31,
        remove=['Ground south', 'Ground north', 'Rail yard mud', 'Plant yard', 'Town yard', 'Shoulder', 'Shoulder 2',
                'Median', 'Median north'],
        # former box regions -> (layer, x0, z0, x1, z1), later ones on top
        regions=[(DRY, -280, -200, 280, 260), (DIRT, -280, -470, 280, -274), (MUD, -107, -170, -61, 170),
                 (GRAVEL, 45, -170, 235, -20), (LEAVES, 10, 15, 230, 135), (GRAVEL, -14, -194, -11, 260),
                 (GRAVEL, 11, -194, 14, 260), (GRAVEL, -14, -470, -11, -280), (GRAVEL, 11, -470, 14, -280)],
        rim=('w', 'e', 's'),           # high ground at these edges (the north meets the far ground box)
        hill_std=1.3, rim_h=9.0,
        sink=[(-370, -276, 370, -198, -9.0)],   # under the river models (bed down to -8 m)
        extra_models=[('River 8', '../models/zone/gen/river_80m.glb', -320.0, 0.0, -237.0, 'River'),
                      ('River 9', '../models/zone/gen/river_80m.glb', 320.0, 0.0, -237.0, 'River')],
        sparse_z=-276.0,               # thinner grass in the dead city beyond the river
        weather_fog_max=0.007,         # the day cycle's fog keys (0.018 - 0.03 was a brown wall at 60 m)
        env={'fog_density': '0.0055', 'saturation': '0.84', 'contrast': '1.08', 'ambient_intensity': '0.85',
             'sky_horizon': '0.72 0.69 0.62', 'fog_color': '0.66 0.65 0.6', 'tint': '1.02 1 0.94', 'vignette': '0.28'}),
    'hospital_street': dict(
        x0=-150.0, z0=-170.0, size=300.0, seed=57,
        remove=['Ground', 'Courtyard ground', 'Worn path', 'Worn path 2', 'Worn path 3', 'Embankment'],
        regions=[(DRY, -150, -170, 150, 120), (MUD, -110, 50, 110, 78), (GRAVEL, -110, 56, 110, 72), (DIRT, -27, -60, 16, 5), (PATH, -6, -30, -3, 5), (PATH, -6, -33, 14, -30),
                 (PATH, 11, -30, 14, -12)],
        rim=('w', 'e', 'n', 's'), hill_std=0.9, rim_h=7.0, sink=[], extra_models=[], sparse_z=None, weather_fog_max=0.007,
        env={'fog_density': '0.005', 'saturation': '0.84', 'contrast': '1.08', 'ambient_intensity': '0.8',
             'sky_horizon': '0.76 0.7 0.6', 'fog_color': '0.68 0.64 0.58', 'tint': '1.02 1 0.94', 'vignette': '0.3'}),
}

_GLTF = {}


def _mat(node):
    if 'matrix' in node:
        return np.array(node['matrix'], dtype=np.float64).reshape(4, 4).T
    m = np.eye(4)
    t, r, s = node.get('translation', [0, 0, 0]), node.get('rotation', [0, 0, 0, 1]), node.get('scale', [1, 1, 1])
    x, y, z, w = r
    R = np.array([[1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)],
                  [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)],
                  [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)]])
    m[:3, :3] = R * np.array(s)[None, :]
    m[:3, 3] = t
    return m


def gltf_bounds(path):
    """Model-space AABB (min, max) of a .glb / .gltf from the POSITION accessor bounds."""
    if path in _GLTF:
        return _GLTF[path]
    b = open(path, 'rb').read()
    j = json.loads(b[20:20 + struct.unpack('<I', b[12:16])[0]]) if b[:4] == b'glTF' else json.loads(b)
    lo, hi = np.full(3, 1e9), np.full(3, -1e9)

    def walk(ni, parent):
        nonlocal lo, hi
        node = j['nodes'][ni]
        m = parent @ _mat(node)
        if 'mesh' in node:
            for p in j['meshes'][node['mesh']]['primitives']:
                a = j['accessors'][p['attributes']['POSITION']]
                if 'min' not in a:
                    continue
                c = np.array([[x, y, z, 1.0] for x in (a['min'][0], a['max'][0]) for y in (a['min'][1], a['max'][1])
                              for z in (a['min'][2], a['max'][2])]) @ m.T
                lo, hi = np.minimum(lo, c[:, :3].min(0)), np.maximum(hi, c[:, :3].max(0))
        for ci in node.get('children', []):
            walk(ci, m)
    scene = j.get('scenes', [{'nodes': list(range(len(j.get('nodes', []))))}])[j.get('scene', 0)]
    for ni in scene['nodes']:
        walk(ni, np.eye(4))
    if lo[0] > hi[0]:
        lo, hi = np.full(3, -0.5), np.full(3, 0.5)
    _GLTF[path] = (lo, hi)
    return lo, hi


def parse(lines):
    """Objects of the level: dicts with kind, name, line index, end index (exclusive, sub-lines), x y z, extra."""
    objs = []
    i = 0
    while i < len(lines):
        ln = lines[i]
        m = re.match(r'(model|box|entity) "([^"]*)"(.*)$', ln)
        if not m:
            i += 1
            continue
        kind, name, rest = m.groups()
        j = i + 1
        while j < len(lines) and lines[j].startswith('  '):
            j += 1
        o = dict(kind=kind, name=name, i=i, j=j)
        if kind == 'model':
            path, nums = re.match(r'\s*"([^"]*)"\s+(.*)$', rest).groups()
            o['path'] = path
            o['x'], o['y'], o['z'] = (float(v) for v in nums.split()[:3])
            o['yaw'], o['scale'], o['tilt'] = 0.0, 1.0, False
            for k in range(i + 1, j):
                t = lines[k].split()
                if t[0] == 'transform':
                    o['yaw'], o['scale'] = float(t[2]), float(t[4])
                    o['tilt'] = abs(float(t[1])) > 1 or abs(float(t[3])) > 1
        elif kind == 'box':
            v = [float(s) for s in rest.split()[:6]]
            o['x'], o['y'], o['z'], o['sx'], o['sy'], o['sz'] = v
        else:
            v = rest.split()
            o['x'], o['y'], o['z'] = float(v[1]), float(v[2]), float(v[3])
        objs.append(o)
        i = j
    return objs


def footprint(o, level_dir):
    """World xz rectangle (x0, z0, x1, z1) of an object."""
    if o['kind'] == 'box':
        return o['x'] - o['sx'] / 2, o['z'] - o['sz'] / 2, o['x'] + o['sx'] / 2, o['z'] + o['sz'] / 2
    if o['kind'] == 'entity':
        return o['x'] - 0.6, o['z'] - 0.6, o['x'] + 0.6, o['z'] + 0.6
    p = os.path.normpath(os.path.join(level_dir, o['path']))
    try:
        lo, hi = gltf_bounds(p)
    except (OSError, ValueError, KeyError):
        lo, hi = np.full(3, -1.0), np.full(3, 1.0)
    lo, hi = lo * o['scale'], hi * o['scale']
    if o['tilt']:
        r = float(np.max(np.abs(np.concatenate([lo, hi]))))
        return o['x'] - r, o['z'] - r, o['x'] + r, o['z'] + r
    a = math.radians(o['yaw'])
    c, s = math.cos(a), math.sin(a)
    xs, zs = [], []
    for x in (lo[0], hi[0]):
        for z in (lo[2], hi[2]):
            xs.append(o['x'] + x * c + z * s)
            zs.append(o['z'] - x * s + z * c)
    return min(xs), min(zs), max(xs), max(zs)


def build(name):
    cfg = CONFIG[name]
    path = os.path.join(LEVELS, name + '.level')
    lines = open(path, encoding='utf-8').read().split('\n')
    rng = random.Random(cfg['seed'])
    nz = Noise(cfg['seed'])

    # ---------------------------------------------------------------- strip what an earlier run added
    lines = [ln for ln in lines if not re.match(r'(terrain |terrain_layer |foliage_type |foliage_lod |foliage_impostor )', ln)]
    objs = parse(lines)
    drop = set()
    for o in objs:
        if o['kind'] == 'box' and o['name'] in cfg['remove']:
            drop.update(range(o['i'], o['j']))
    have = {o['name'] for o in objs}
    extra = []
    for (mn, mp, x, y, z, parent) in cfg['extra_models']:
        if mn not in have:
            extra += [f'model "{mn}" "{mp}" {x:.2f} {y:.2f} {z:.2f}', f'  parent "{parent}"']
    lines = [ln for k, ln in enumerate(lines) if k not in drop]
    gi = next(k for k, ln in enumerate(lines) if ln.startswith('group '))
    lines = lines[:gi] + extra + lines[gi:] if extra else lines
    objs = parse(lines)

    # ---------------------------------------------------------------- grid
    x0, z0, size = cfg['x0'], cfg['z0'], cfg['size']
    n = int(size) + 1
    xs = x0 + np.arange(n, dtype=np.float64)
    zs = z0 + np.arange(n, dtype=np.float64)
    X, Z = np.meshgrid(xs, zs)

    def window(r0, margin):
        i0 = max(int(r0[0] - margin - x0), 0)
        i1 = min(int(r0[2] + margin - x0) + 2, n)
        j0 = max(int(r0[1] - margin - z0), 0)
        j1 = min(int(r0[3] + margin - z0) + 2, n)
        return slice(j0, j1), slice(i0, i1)

    def rect_dist(r0, sl):
        dx = np.maximum(np.maximum(r0[0] - X[sl], X[sl] - r0[2]), 0)
        dz = np.maximum(np.maximum(r0[1] - Z[sl], Z[sl] - r0[3]), 0)
        return np.hypot(dx, dz)

    fixed = np.full(X.shape, 1e9)           # distance to anything that must keep its ground
    build_d = np.full(X.shape, 1e9)         # distance to buildings and big objects (trodden aprons)
    hard = np.zeros(X.shape, dtype=bool)    # under kept boxes (roads, sidewalks): no grass
    road_d = np.full(X.shape, 1e9)
    loose = []
    for o in objs:
        if o['kind'] == 'model' and o['name'].startswith(LOOSE):
            loose.append(o)
            continue
        if o['kind'] == 'model' and o['name'].startswith(('River', 'Far tower')):
            continue
        r0 = footprint(o, LEVELS)
        if r0[2] < x0 - 40 or r0[0] > x0 + size + 40 or r0[3] < z0 - 40 or r0[1] > z0 + size + 40:
            continue
        sl = window(r0, 40)
        d = rect_dist(r0, sl)
        fixed[sl] = np.minimum(fixed[sl], d)
        big = (r0[2] - r0[0]) * (r0[3] - r0[1]) > 30
        if big:
            build_d[sl] = np.minimum(build_d[sl], d)
        if o['kind'] == 'box':
            hard[sl] |= d <= 0.0
            road_d[sl] = np.minimum(road_d[sl], d)

    # ---------------------------------------------------------------- heights
    base = -0.02                            # top of the old ground boxes: everything stands on it
    shape = 0.6 * (1.0 - np.abs(2.0 * nz.fbm(X - 900, Z + 300, 90.0, 3) - 1.0)) + 0.4 * nz.fbm(X + 40, Z - 70, 38.0, 2)
    shape = (shape - shape.mean()) / (shape.std() + 1e-9)
    micro = 0.9 * (nz.fbm(X + 710, Z - 260, 13.0, 3) - 0.5) + 0.2 * (nz(X - 40, Z + 620, 4.0) - 0.5)
    edge = np.full(X.shape, 1e9)
    for side in cfg['rim']:
        e = {'w': X - x0, 'e': x0 + size - X, 'n': Z - z0, 's': z0 + size - Z}[side]
        edge = np.minimum(edge, e)
    rim = cfg['rim_h'] * smoothstep(90.0, 5.0, edge) * (0.55 + 0.9 * nz(X + 77, Z - 31, 45.0))
    free_w = smoothstep(4.0, 30.0, fixed)               # 0 next to objects .. 1 in the open
    H = base + (cfg['hill_std'] * shape + micro) * free_w + rim * smoothstep(2.0, 20.0, fixed)
    H = np.where(fixed <= 0.0, base, H)
    for (sx0, sz0, sx1, sz1, depth) in cfg['sink']:      # under rivers (and their bridges): out of the way
        d = np.hypot(np.maximum(np.maximum(sx0 - X, X - sx1), 0), np.maximum(np.maximum(sz0 - Z, Z - sz1), 0))
        k = smoothstep(0.0, 3.0, d)
        H = np.where(d <= 0, depth, H * k + base * (1 - k))
    gz, gx = np.gradient(H)
    slope = np.hypot(gx, gz)

    # ---------------------------------------------------------------- layers
    W = np.zeros(X.shape + (8,))
    W[..., MEADOW] = 1.0
    warp = 4.0 * (nz.fbm(X + 3, Z - 9, 9.0, 2) - 0.5)

    def put(layer, cover):
        cover = np.clip(cover, 0.0, 1.0)
        W[...] *= (1.0 - cover)[..., None]
        W[..., layer] += cover

    put(DRY, smoothstep(0.42, 0.6, nz.fbm(X - 400, Z + 250, 26.0, 2)))
    put(LUSH, smoothstep(0.5, 0.64, nz.fbm(X + 60, Z - 900, 20.0, 2)) * (0.5 + 0.5 * smoothstep(0.3, -0.6, H - base)))
    for (layer, rx0, rz0, rx1, rz1) in cfg['regions'][1:]:
        d = np.hypot(np.maximum(np.maximum(rx0 - X, X - rx1), 0), np.maximum(np.maximum(rz0 - Z, Z - rz1), 0))
        inside = np.minimum(np.minimum(X - rx0, rx1 - X), np.minimum(Z - rz0, rz1 - Z))
        sd = np.where(d > 0, d, -np.maximum(inside, 0))
        # patchy inside (the old surface shows through in places), soft wandering edge outside
        patch = 0.75 + 0.5 * (nz.fbm(X * 1.0 + 11 * layer, Z, 7.0, 2) - 0.5)
        put(layer, smoothstep(2.5, -1.5, sd + warp) * np.clip(patch + 0.25, 0, 1))
    put(LEAVES, smoothstep(0.62, 0.7, nz.fbm(X + 1300, Z - 700, 22.0, 2)) * 0.9)
    put(DIRT, smoothstep(0.3, 0.55, slope) * 0.85)
    put(GRAVEL, smoothstep(1.4, 0.2, road_d + 0.8 * (nz(X - 31, Z + 17, 3.0) - 0.5)) * (road_d > 0))
    put(DIRT, smoothstep(2.2, 0.3, build_d + 1.2 * (nz(X + 5, Z - 5, 2.5) - 0.5)) * 0.85)
    put(MUD, smoothstep(0.6, 0.85, nz.fbm(X - 77, Z + 41, 8.0, 2)) * smoothstep(0.0, -0.5, H - base))
    splat_n = 1024
    u = (np.arange(splat_n) + 0.5) / splat_n * (n - 1)
    iu = np.clip(u.astype(int), 0, n - 2)
    fu = u - iu
    Wx = W[:, iu] * (1 - fu)[None, :, None] + W[:, iu + 1] * fu[None, :, None]
    Ws = Wx[iu] * (1 - fu)[:, None, None] + Wx[iu + 1] * fu[:, None, None]
    S = np.clip(np.round(Ws / np.maximum(Ws.sum(-1, keepdims=True), 1e-6) * 255), 0, 255).astype(np.uint8)

    def sample(A, x, z):
        fx, fz = x - x0, z - z0
        i, j = min(max(int(fx), 0), n - 2), min(max(int(fz), 0), n - 2)
        tx, tz = min(max(fx - i, 0), 1), min(max(fz - j, 0), 1)
        return float((A[j, i] * (1 - tx) + A[j, i + 1] * tx) * (1 - tz) + (A[j + 1, i] * (1 - tx) + A[j + 1, i + 1] * tx) * tz)

    # ---------------------------------------------------------------- loose things onto the ground
    for o in loose:
        if not (x0 <= o['x'] <= x0 + size and z0 <= o['z'] <= z0 + size):
            continue
        r0 = footprint(o, LEVELS)
        sl = window(r0, 0)
        flat = o['name'].startswith(('rubble', 'slab_fallen'))     # heaps sit on their lowest corner
        y = float(H[sl].min()) if flat and H[sl].size else sample(H, o['x'], o['z'])
        y += -0.05 if o['name'].startswith(('birch', 'dead_tree', 'bush')) else 0.0
        if o['name'].startswith(('Tyre', )):
            y += 0.08
        head = re.match(r'(model "[^"]*" "[^"]*" )', lines[o['i']]).group(1)
        lines[o['i']] = f"{head}{o['x']:.2f} {y:.2f} {o['z']:.2f}"

    # ---------------------------------------------------------------- ground cover (terrain foliage)
    types, inst = {}, []

    def veg(key, x, z, scale, yaw=None):
        if key not in types:
            types[key] = len(types)
        inst.append((x, sample(H, x, z) - 0.03, z, rng.uniform(0, 360) if yaw is None else yaw, scale, types[key]))

    for gz_ in np.arange(z0 + 1.0, z0 + size - 1.0, 2.5):
        for gx_ in np.arange(x0 + 1.0, x0 + size - 1.0, 2.5):
            far = cfg['sparse_z'] is not None and gz_ < cfg['sparse_z']
            if far and rng.random() < 0.55:
                continue
            x, z = gx_ + rng.uniform(-1.1, 1.1), gz_ + rng.uniform(-1.1, 1.1)
            fd = sample(fixed, x, z)
            if fd < 0.7 or sample(H, x, z) < -1.0 or sample(hard.astype(float), x, z) > 0.0:
                continue
            fi, fj = int(x - x0), int(z - z0)
            w = W[min(max(fj, 0), n - 1), min(max(fi, 0), n - 1)]
            clump = float(nz.fbm(x + 333.0, z - 777.0, 11.0, 2))
            dense = min(max((clump - 0.38) / 0.24, 0.0), 1.0)
            bare = w[GRAVEL] + w[PATH] + w[MUD] + 0.7 * w[DIRT]
            bd = sample(build_d, x, z)
            if bd < 4.0 and rng.random() < 0.35:
                veg('weed', x, z, rng.uniform(0.8, 1.15))
                continue
            if rng.random() < (0.5 + 0.45 * dense) * (1.0 - 0.75 * min(bare, 1.0)):
                fresh = w[LUSH] + 0.3 * w[MEADOW] + 0.4 * w[LEAVES]
                veg('fresh' if rng.random() < fresh else 'dry', x, z, rng.uniform(0.7, 0.95) + 0.3 * dense)
                if dense > 0.4 and not far:
                    veg('dry' if rng.random() < 0.7 else 'fresh', x + 1.25, z + 1.25, rng.uniform(0.55, 0.8))
    FILES = {'dry': (SK + 'veg/grass_dry', SK + 'veg/grass_dry_far.glb', 'Tall grass', 0.25),
             'fresh': (SK + 'veg/grass_fresh', SK + 'veg/grass_fresh_far.glb', 'Tall grass', 0.25),
             'weed': (V + 'weed_patch', SK + 'veg/weed_patch_far.glb', 'Weeds', 0.1)}
    header = [f'terrain "{name}.terrain"'] + [f'terrain_layer {i} "{SKM}{LAYERS[i]}.material" {TILING[i]:g}' for i in range(8)]
    for key, _ in sorted(types.items(), key=lambda kv: kv[1]):
        f, farf, _kind, dens = FILES[key]
        header.append(f'foliage_type "{f}.glb" {dens:g} 0.85 1.15 50 0 0 0 0.5')
        header.append(f'foliage_lod "{f}_lod1.glb" 8')
        header.append(f'foliage_lod "{farf}" 18')

    # ---------------------------------------------------------------- environment
    for k, v in cfg['env'].items():
        for idx, ln in enumerate(lines):
            if ln.startswith(f'env {k} '):
                lines[idx] = f'env {k} {v}'
    for idx, ln in enumerate(lines):
        if ln.startswith('weather_key '):
            t = ln.split()
            t[3] = f"{min(float(t[3]), cfg['weather_fog_max']):g}"
            lines[idx] = ' '.join(t)
    gi = next(k for k, ln in enumerate(lines) if ln.startswith(('weapon ', 'group ')))
    lines = lines[:gi] + header + lines[gi:]
    open(path, 'w', encoding='utf-8').write('\n'.join(lines))

    with open(os.path.join(LEVELS, name + '.terrain'), 'wb') as f:
        f.write(struct.pack('<IIiifffI', 0x52524554, 2, n, splat_n, x0, z0, size, len(inst)))
        f.write(np.asarray(H, dtype='<f4').tobytes())
        f.write(S.tobytes())
        for x, y, z, yaw, sc, t in inst:
            f.write(struct.pack('<7fI', x, y, z, yaw, sc, 0.0, 0.0, t))
    print(f'{name}: terrain {n} x {n} ({H.min():.1f} .. {H.max():.1f} m), {len(drop)} box lines removed, '
          f'{len(loose)} loose objects on the ground, {len(inst)} ground cover')


if __name__ == '__main__':
    for a in sys.argv[1:] or ['zone', 'hospital_street']:
        build(a)
