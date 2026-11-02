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
  - ground cover: clumps of straw and fresh grass as terrain foliage (Foliage mode); nettles and weeds along
    walls, fences and wrecks, bushes by the buildings, brick/plaster debris under the walls, litter at the road
    edges, twigs under the trees and grass in the cracks along the kerbs;
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
        # a 6th value makes a region patchy: the share of it the layer covers, in noise-shaped patches
        regions=[(DRY, -280, -200, 280, 260), (DIRT, -280, -470, 280, -274), (GRAVEL, -107, -170, -61, 170, 0.45),
                 (MUD, -107, -170, -61, 170, 0.22),
                 (GRAVEL, 45, -170, 235, -20), (LEAVES, 10, 15, 230, 135), (GRAVEL, -14, -194, -11, 260),
                 (GRAVEL, 11, -194, 14, 260), (GRAVEL, -14, -470, -11, -280), (GRAVEL, 11, -470, 14, -280)],
        rim=('w', 'e', 's'),           # high ground at these edges (the north meets the far ground box)
        hill_std=1.3, rim_h=9.0,
        sink=[(-370, -276, 370, -198, -9.0)],   # under the river models (bed down to -8 m)
        extra_models=[('River 8', '../models/zone/gen/river_80m.glb', -320.0, 0.0, -237.0, 'River'),
                      ('River 9', '../models/zone/gen/river_80m.glb', 320.0, 0.0, -237.0, 'River')],
        sparse_z=-276.0,               # thinner grass in the dead city beyond the river
        weather_fog_max=0.007,         # the day cycle's fog keys (0.018 - 0.03 was a brown wall at 60 m)
        # the day cycle reddens the sun by the air mass itself, so the authored sun colour is near white (an orange
        # one doubled the evening glow into sepia); neutral tint, a little more saturation
        env={'fog_density': '0.0055', 'saturation': '0.93', 'contrast': '1.08', 'ambient_intensity': '0.9',
             'sun_color': '1 0.94 0.86', 'sky_zenith': '0.33 0.39 0.46', 'sky_horizon': '0.7 0.71 0.68',
             'ground_color': '0.3 0.29 0.23', 'fog_color': '0.64 0.65 0.63', 'tint': '1 1 0.98', 'vignette': '0.25'}),
    'hospital_street': dict(
        x0=-150.0, z0=-170.0, size=300.0, seed=57,
        remove=['Ground', 'Courtyard ground', 'Worn path', 'Worn path 2', 'Worn path 3', 'Embankment'],
        regions=[(DRY, -150, -170, 150, 120), (MUD, -110, 50, 110, 78, 0.35), (GRAVEL, -110, 56, 110, 72, 0.6), (DIRT, -27, -60, 16, 5), (PATH, -6, -30, -3, 5), (PATH, -6, -33, 14, -30),
                 (PATH, 11, -30, 14, -12)],
        rim=('w', 'e', 'n', 's'), hill_std=0.9, rim_h=7.0, sink=[], extra_models=[], sparse_z=None, weather_fog_max=0.007,
        env={'fog_density': '0.005', 'saturation': '0.92', 'contrast': '1.08', 'ambient_intensity': '0.85',
             'sun_color': '1 0.93 0.84', 'sky_zenith': '0.33 0.38 0.45', 'sky_horizon': '0.72 0.71 0.66',
             'ground_color': '0.29 0.28 0.22', 'fog_color': '0.66 0.65 0.62', 'tint': '1 1 0.97', 'vignette': '0.28'}),
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
            o['etype'] = v[0].strip('"')
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


NODE_KINDS = ('npc', 'objective', 'pickup', 'alife_point', 'map_marker', 'radio', 'trader')


def trails(nodes, X, Z, fixed, hard, slope, H, x0, z0, n, box=None):
    """Distance (m) from every grid point to the trodden paths between `nodes` and to the roads."""
    import heapq
    st = 2                                               # 2 m cells
    m = (n + st - 1) // st
    fx, hd, sl, hh = fixed[::st, ::st], hard[::st, ::st], slope[::st, ::st], H[::st, ::st]
    cost = 1.0 + 6.0 * np.clip(sl - 0.15, 0, None) + 3.0 * (fx < 2.5)
    cost = np.where(hd, 0.7, cost)
    cost = np.where((fx < 0.6) & ~hd, 25.0, cost)        # through objects only to get out of a building
    blocked = hh < -1.0
    if box:                                              # only inside the playable area
        bx0, bz0, bx1, bz1 = box
        xs_, zs_ = X[::st, ::st], Z[::st, ::st]
        blocked |= (xs_ < bx0) | (xs_ > bx1) | (zs_ < bz0) | (zs_ > bz1)
    merged = []                                          # a camp is one place, not five people
    for (x, z) in nodes:
        for k, (mx, mz, c) in enumerate(merged):
            if math.hypot(x - mx / c, z - mz / c) < 14.0:
                merged[k] = (mx + x, mz + z, c + 1)
                break
        else:
            merged.append((x, z, 1))
    nodes = [(mx / c, mz / c) for mx, mz, c in merged]
    cell = []
    for (x, z) in nodes:
        i, j = int(round((x - x0) / st)), int(round((z - z0) / st))
        if 0 <= i < m and 0 <= j < m:
            cell.append((j, i))
    nbr = [(-1, 0, 1.0), (1, 0, 1.0), (0, -1, 1.0), (0, 1, 1.0), (-1, -1, 1.414), (-1, 1, 1.414), (1, -1, 1.414), (1, 1, 1.414)]

    def dijkstra(src):
        dist = np.full((m, m), np.inf)
        prev = np.full((m, m), -1, dtype=np.int64)
        dist[src] = 0.0
        pq = [(0.0, src[0], src[1])]
        while pq:
            d, j, i = heapq.heappop(pq)
            if d > dist[j, i] or d > 900:
                continue
            for dj, di, w in nbr:
                jj, ii = j + dj, i + di
                if 0 <= jj < m and 0 <= ii < m and not blocked[jj, ii]:
                    nd = d + w * st * 0.5 * (cost[j, i] + cost[jj, ii])
                    if nd < dist[jj, ii]:
                        dist[jj, ii] = nd
                        prev[jj, ii] = j * m + i
                        heapq.heappush(pq, (nd, jj, ii))
        return dist, prev
    polys = []

    def walk(prev, j, i):
        pts = []
        while True:
            pts.append((j, i))
            k = prev[j, i]
            if k < 0:
                break
            j, i = divmod(int(k), m)
        # split at the roads: no trodden earth painted on them
        run = []
        for (pj, pi) in pts:
            if hd[pj, pi]:
                if len(run) > 1:
                    polys.append(run)
                run = []
            else:
                run.append((pj, pi))
        if len(run) > 1:
            polys.append(run)
    runs = [dijkstra(c) for c in cell]
    for k, (dist, prev) in enumerate(runs):                # every node to its nearest road
        rd = np.where(hd, dist, np.inf)
        if np.isfinite(rd).any():
            j, i = np.unravel_index(int(np.argmin(rd)), rd.shape)
            walk(prev, int(j), int(i))
    if len(cell) > 1:                                      # nodes to each other: minimum spanning tree
        inside, rest = {0}, set(range(1, len(cell)))
        while rest:
            d, a, b = min(((runs[a][0][cell[b]], a, b) for a in inside for b in rest), key=lambda t: t[0])
            if np.isfinite(d) and d <= 450:                # farther ones meet at the roads
                walk(runs[a][1], *cell[b])
            inside.add(b)
            rest.discard(b)

    def clear_line(a, b):                                  # straight walk between two cells stays cheap
        nseg = int(max(abs(b[0] - a[0]), abs(b[1] - a[1]))) + 1
        for t in np.linspace(0.0, 1.0, nseg + 1):
            jj, ii = int(round(a[0] + (b[0] - a[0]) * t)), int(round(a[1] + (b[1] - a[1]) * t))
            if cost[jj, ii] > 2.5 or blocked[jj, ii]:
                return False
        return True
    out = np.full(X.shape, 1e9)
    for run in polys:
        # string pulling: people cut corners in straight lines where nothing is in the way ...
        keep = [run[0]]
        k = 0
        while k < len(run) - 1:
            far = k + 1
            for t in range(len(run) - 1, k, -1):
                if t - k <= 40 and clear_line(run[k], run[t]):
                    far = t
                    break
            keep.append(run[far])
            k = far
        P = np.array([(x0 + i * st, z0 + j * st) for (j, i) in keep], dtype=np.float64)
        # ... and round them off (Chaikin), with a slight sideways wander
        for _ in range(3):
            if len(P) < 3:
                break
            Q = [P[0]]
            for a_, b_ in zip(P[:-1], P[1:]):
                Q += [0.75 * a_ + 0.25 * b_, 0.25 * a_ + 0.75 * b_]
            Q.append(P[-1])
            P = np.array(Q)
        dense = [P[0]]
        for a_, b_ in zip(P[:-1], P[1:]):
            L_ = float(np.hypot(*(b_ - a_)))
            for t in np.arange(0.5, L_, 0.5):
                dense.append(a_ + (b_ - a_) * (t / max(L_, 1e-6)))
            dense.append(b_)
        for (cx, cz) in dense:
            wob = 0.6 * math.sin(cx * 0.21 + cz * 0.17) + 0.4 * math.sin(cx * 0.07 - cz * 0.11)
            cx, cz = cx + 0.5 * wob, cz - 0.5 * wob
            i, j = int(cx - x0), int(cz - z0)
            sl_ = (slice(max(j - 4, 0), min(j + 5, n)), slice(max(i - 4, 0), min(i + 5, n)))
            out[sl_] = np.minimum(out[sl_], np.hypot(X[sl_] - cx, Z[sl_] - cz))
    return out


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
    kerbs = []                              # kept boxes (roads, sidewalks): grass in the cracks along their edges
    wall_d = np.full(X.shape, 1e9)          # distance to fixed models (walls, fences, wrecks): weeds grow there
    ent_d = np.full(X.shape, 1e9)           # distance to characters and spawners: camps are trodden bare
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
        if big and o['kind'] == 'model':
            build_d[sl] = np.minimum(build_d[sl], d)
        if o['kind'] == 'model' and 2.5 < max(r0[2] - r0[0], r0[3] - r0[1]) < 150:   # not crates, fire pits, signs
            wall_d[sl] = np.minimum(wall_d[sl], d)
        if o['kind'] == 'entity':
            ent_d[sl] = np.minimum(ent_d[sl], d)
        if o['kind'] == 'box':
            hard[sl] |= d <= 0.0
            road_d[sl] = np.minimum(road_d[sl], d)
            if min(o['sx'], o['sz']) < 50:
                kerbs.append((r0, o['y'] + o['sy'] / 2))

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
    for (layer, rx0, rz0, rx1, rz1, *amount) in cfg['regions'][1:]:
        d = np.hypot(np.maximum(np.maximum(rx0 - X, X - rx1), 0), np.maximum(np.maximum(rz0 - Z, Z - rz1), 0))
        inside = np.minimum(np.minimum(X - rx0, rx1 - X), np.minimum(Z - rz0, rz1 - Z))
        sd = np.where(d > 0, d, -np.maximum(inside, 0))
        # patchy inside (the old surface shows through in places), soft wandering edge outside
        patch = 0.75 + 0.5 * (nz.fbm(X * 1.0 + 11 * layer, Z, 7.0, 2) - 0.5)
        cover = smoothstep(2.5, -1.5, sd + warp) * np.clip(patch + 0.25, 0, 1)
        if amount:                     # patchy region: only the highest `amount` of a slow noise
            pn = nz.fbm(X - 57 * layer, Z + 91 * layer, 11.0, 3)
            t = float(np.quantile(pn, 1.0 - amount[0]))
            cover *= smoothstep(t - 0.04, t + 0.04, pn)
        put(layer, cover)
    put(LEAVES, smoothstep(0.62, 0.7, nz.fbm(X + 1300, Z - 700, 22.0, 2)) * 0.9)
    put(DIRT, smoothstep(0.3, 0.55, slope) * 0.85)
    put(GRAVEL, smoothstep(1.4, 0.2, road_d + 0.8 * (nz(X - 31, Z + 17, 3.0) - 0.5)) * (road_d > 0))
    put(DIRT, smoothstep(2.2, 0.3, build_d + 1.2 * (nz(X + 5, Z - 5, 2.5) - 0.5)) * 0.85)
    put(MUD, smoothstep(0.6, 0.85, nz.fbm(X - 77, Z + 41, 8.0, 2)) * smoothstep(0.0, -0.5, H - base))
    # ---------------------------------------------------------------- trodden paths (desire lines)
    # People walk the cheapest way between the places they go to: camps, characters, stashes, map markers and
    # the roads. Dijkstra on a 2 m grid (open ground cheap, roads a bit cheaper, steep banks dear, objects
    # impassable) joins every such node to its nearest road and the nodes to each other (minimum spanning
    # tree), and the routes are painted as bare trodden earth with no grass on them.
    nodes = [(o['x'], o['z']) for o in objs if o['kind'] == 'entity' and o.get('etype') in NODE_KINDS]
    ps = next((ln.split() for ln in lines if ln.startswith('player_start ')), None)
    if ps:
        nodes.append((float(ps[1]), float(ps[3])))
    trail = trails(nodes, X, Z, fixed, hard, slope, H, x0, z0, n, cfg.get('trail_box'))
    tw = 0.28 + 0.2 * nz(X + 19, Z - 47, 6.0)                      # 0.28 .. 0.48 m half width: a footpath, wandering
    # a worn track: flattened, dried-out grass with bare earth only along the middle (dark mud or leaf layers read
    # as holes from eye height)
    put(DRY, smoothstep(tw + 0.6, tw - 0.1, trail) * (H > -1.0) * 0.9)
    put(DIRT, smoothstep(tw * 0.6 + 0.1, tw * 0.6 - 0.15, trail) * (H > -1.0) * (0.45 + 0.25 * nz(X - 3, Z + 8, 2.0)))
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
            if sample(trail, x, z) < 0.55:
                continue
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

    # ---------------------------------------------------------------- details at walls, wrecks, trees and kerbs
    # (own random stream, so the grass above keeps its layout): nettles and weeds along every wall, fence and
    # wreck, a bush now and then by the buildings, brick and plaster fallen off them, litter at the road edges,
    # twigs under the trees and grass in the cracks along the kerbs.
    drng = random.Random(cfg['seed'] + 7)

    def pick(table):
        r, acc = drng.random(), 0.0
        for key, pr in table:
            acc += pr
            if r < acc:
                return key
        return table[-1][0]

    def add(key, x, z, scale, y=None):
        if key not in types:
            types[key] = len(types)
        yy = sample(H, x, z) - 0.03 if y is None else y
        inst.append((x, yy, z, drng.uniform(0, 360), scale, types[key]))

    WEEDS = [('nettle_tall_a', 0.2), ('nettle_tall_b', 0.12), ('nettle_medium_a', 0.18), ('weed_a', 0.1), ('weed_b', 0.1),
             ('weed_c', 0.1), ('weed', 0.1), ('grass_tall_a', 0.1)]
    WALL = [('d:silicate_bricks', 0.3), ('d:plaster_chunks', 0.35), ('d:glass_shards', 0.15), ('d:concrete_chunks', 0.2)]
    KERB = [('d:tin_cans', 0.35), ('d:bottles', 0.25), ('d:glass_shards', 0.2), ('d:concrete_chunks', 0.2)]
    CRACK = [('grass_clump_a', 0.3), ('grass_short_a', 0.25), ('weed_a', 0.15), ('weed_b', 0.15), ('grass_tall_b', 0.15)]
    ground = (H > -1.0) & ~hard & (ent_d > 3.5)
    if cfg['sparse_z'] is not None:
        town = Z >= cfg['sparse_z']
    else:
        town = np.ones(X.shape, dtype=bool)
    for j, i in zip(*np.nonzero(ground & (wall_d > 0.15) & (wall_d < 3.2))):
        d = wall_d[j, i]
        for _ in range(2):
            if drng.random() < 0.42 * (1.0 - d / 3.4):
                add(pick(WEEDS), X[j, i] + drng.uniform(-0.5, 0.5), Z[j, i] + drng.uniform(-0.5, 0.5), drng.uniform(0.75, 1.2))
    for j, i in zip(*np.nonzero(ground & town & (build_d > 1.5) & (build_d < 5.0))):
        if drng.random() < 0.022:
            add(drng.choice(['bush_a', 'bush_b', 'bush_c', 'bush_dense_a']), X[j, i] + drng.uniform(-0.5, 0.5),
                Z[j, i] + drng.uniform(-0.5, 0.5), drng.uniform(0.7, 1.1))
    for j, i in zip(*np.nonzero(ground & (build_d > 0.1) & (build_d < 2.6))):
        if drng.random() < 0.35 * (1.0 - build_d[j, i] / 3.0):
            add(pick(WALL), X[j, i] + drng.uniform(-0.5, 0.5), Z[j, i] + drng.uniform(-0.5, 0.5), drng.uniform(0.8, 1.2))
    for j, i in zip(*np.nonzero(ground & (road_d > 0.05) & (road_d < 1.5))):
        if drng.random() < 0.035:
            add(pick(KERB), X[j, i] + drng.uniform(-0.5, 0.5), Z[j, i] + drng.uniform(-0.5, 0.5), 1.0)
    for (bx0, bz0, bx1, bz1), top in kerbs:
        for (ax, az, bx, bz, nx, nz_) in ((bx0, bz0, bx1, bz0, 0, 1), (bx0, bz1, bx1, bz1, 0, -1),
                                         (bx0, bz0, bx0, bz1, 1, 0), (bx1, bz0, bx1, bz1, -1, 0)):
            length = math.hypot(bx - ax, bz - az)
            for k in range(int(length)):
                if drng.random() < 0.55:
                    t, inset = (k + drng.random()) / length, drng.uniform(0.08, 0.5)
                    x, z = ax + (bx - ax) * t + nx * inset, az + (bz - az) * t + nz_ * inset
                    if x0 < x < x0 + size and z0 < z < z0 + size:
                        add(pick(CRACK), x, z, drng.uniform(1.0, 1.6), y=top + 0.01)
    for o in loose:
        if o['name'].startswith(('birch', 'dead_tree')) and drng.random() < 0.5:
            for _ in range(drng.randint(1, 3)):
                a, r = drng.uniform(0, 2 * math.pi), drng.uniform(0.8, 3.0)
                x, z = o['x'] + r * math.cos(a), o['z'] + r * math.sin(a)
                if x0 < x < x0 + size and z0 < z < z0 + size:
                    add('d:twigs', x, z, drng.uniform(0.8, 1.3))

    # ---------------------------------------------------------------- nature taking the place back
    # As in Pripyat after 25 years (Lacan et al. 2015): most of the new wood is seedlings and saplings, densest
    # along the streets and in the groves, and young trees come up wherever mature ones stand. Trees grow in
    # groves with a shrub belt at their edge, not evenly sprinkled. Own random stream.
    rrng = random.Random(cfg['seed'] + 13)
    rim_w = 45.0

    def open_at(x, z, clear=1.5, path_clear=2.0):
        if not (x0 + 2 < x < x0 + size - 2 and z0 + 2 < z < z0 + size - 2):
            return False
        return (sample(H, x, z) > -1.0 and sample(fixed, x, z) > clear and sample(trail, x, z) > path_clear
                and sample(hard.astype(float), x, z) <= 0.0 and sample(ent_d, x, z) > 4.0)

    def put_tree(key, x, z, scale):
        if key not in types:
            types[key] = len(types)
        inst.append((x, sample(H, x, z) - 0.05, z, rrng.uniform(0, 360), scale, types[key]))
    YOUNG = [('birch_young', 0.7), ('pine_young', 0.1), ('spruce_young', 0.2)]

    def young():
        r, acc = rrng.random(), 0.0
        for key, pr in YOUNG:
            acc += pr
            if r < acc:
                return key
        return YOUNG[-1][0]
    # seedlings and saplings around the living mature trees
    for o in loose:
        if o['name'].startswith('birch') and 'dead' not in o['path']:
            for _ in range(rrng.randint(1, 4)):
                a, r = rrng.uniform(0, 2 * math.pi), 2.5 + abs(rrng.gauss(0.0, 4.0))
                x, z = o['x'] + r * math.cos(a), o['z'] + r * math.sin(a)
                if open_at(x, z):
                    put_tree(young(), x, z, rrng.uniform(0.35, 0.95))
    # street side: saplings in the verges along the roads and, now and then, one out of the pavement seam
    for (bx0, bz0, bx1, bz1), top in kerbs:
        for (ax, az, bx, bz, nx, nz_) in ((bx0, bz0, bx1, bz0, 0, -1), (bx0, bz1, bx1, bz1, 0, 1),
                                         (bx0, bz0, bx0, bz1, -1, 0), (bx1, bz0, bx1, bz1, 1, 0)):
            length = math.hypot(bx - ax, bz - az)
            for k in range(int(length)):
                t = (k + rrng.random()) / length
                ex, ez = ax + (bx - ax) * t, az + (bz - az) * t
                if rrng.random() < 0.045:
                    off = rrng.uniform(1.0, 3.5)
                    x, z = ex + nx * off, ez + nz_ * off
                    if open_at(x, z, 1.0, 1.5) and sample(road_d, x, z) > 0.8:
                        put_tree(young(), x, z, rrng.uniform(0.5, 1.05))
                elif rrng.random() < 0.004 and min(bx1 - bx0, bz1 - bz0) < 5.0:     # sidewalks: out of the seam
                    x, z = ex - nx * 0.25, ez - nz_ * 0.25
                    if x0 < x < x0 + size and z0 < z < z0 + size and sample(fixed, x, z) > 1.0:
                        if 'birch_young' not in types:
                            types['birch_young'] = len(types)
                        inst.append((x, top - 0.02, z, rrng.uniform(0, 360), rrng.uniform(0.4, 0.7), types['birch_young']))
    # groves: a slow noise picks the wooded patches; trees in the core, shrubs and saplings at the edge
    gn = nz.fbm(X + 2100, Z - 1300, 55.0, 3)
    t_core, t_edge = float(np.quantile(gn, 0.87)), float(np.quantile(gn, 0.81))
    GROVE = [('birch_a', 0.3), ('birch_b', 0.25), ('birch_young', 0.1), ('broadleaf_a', 0.15), ('broadleaf_b', 0.1),
             ('pine_a', 0.05), ('spruce_a', 0.05)]
    gx_ = x0 + rim_w
    while gx_ < x0 + size - rim_w:
        gz_ = z0 + rim_w
        while gz_ < z0 + size - rim_w:
            x, z = gx_ + rrng.uniform(-2.4, 2.4), gz_ + rrng.uniform(-2.4, 2.4)
            g = sample(gn, x, z)
            if g > t_edge and open_at(x, z, 4.0, 3.0) and min((math.hypot(x - nx_, z - nz2) for nx_, nz2 in nodes),
                                                               default=1e9) > 28.0:     # camps and goals stay open
                if g > t_core and rrng.random() < 0.55:
                    r, acc, key = rrng.random(), 0.0, GROVE[-1][0]
                    for kk, pr in GROVE:
                        acc += pr
                        if r < acc:
                            key = kk
                            break
                    put_tree(key, x, z, rrng.uniform(0.8, 1.2))
                elif rrng.random() < 0.45:
                    put_tree(rrng.choice(['bush_a', 'bush_b', 'bush_c', 'bush_dense_a', 'bush_wide_a']), x, z,
                             rrng.uniform(0.7, 1.15))
                elif rrng.random() < 0.4:
                    put_tree(young(), x, z, rrng.uniform(0.4, 0.9))
            gz_ += 6.0
        gx_ += 6.0

    # key -> (model without .glb, [(lod model, distance)], brush density, cull distance, shadows, far density)
    FILES = {'dry': (SK + 'veg/grass_dry', [(SK + 'veg/grass_dry_lod1.glb', 8), (SK + 'veg/grass_dry_far.glb', 18)], 0.25, 50, 0, 0.5),
             'fresh': (SK + 'veg/grass_fresh', [(SK + 'veg/grass_fresh_lod1.glb', 8), (SK + 'veg/grass_fresh_far.glb', 18)], 0.25, 50, 0, 0.5),
             'weed': (V + 'weed_patch', [(V + 'weed_patch_lod1.glb', 8), (SK + 'veg/weed_patch_far.glb', 18)], 0.1, 50, 0, 0.6)}
    for key in ('nettle_tall_a', 'nettle_tall_b', 'nettle_medium_a', 'weed_a', 'weed_b', 'weed_c', 'grass_tall_a', 'grass_tall_b',
                'grass_clump_a', 'grass_short_a'):
        FILES[key] = (V + key, [(V + key + '_lod1.glb', 8)], 0.1, 45, 0, 0.6)
    for key in ('bush_a', 'bush_b', 'bush_c', 'bush_dense_a', 'bush_wide_a'):
        FILES[key] = (V + key, [(V + key + '_lod1.glb', 25)], 0.02, 120, 1, 0.8)
    for key in ('birch_young', 'pine_young', 'spruce_young'):
        FILES[key] = (V + key, [(V + key + '_lod1.glb', 25)], 0.03, 150, 1, 0.8)
    TREES = ('birch_a', 'birch_b', 'broadleaf_a', 'broadleaf_b', 'pine_a', 'spruce_a')
    for key in TREES:                           # grove trees: collide, cast shadows, impostor cards far away
        FILES[key] = (V + key, [(V + key + '_lod1.glb', 30)], 0.03, 230, 1, 1.0)
    for key in ('silicate_bricks', 'plaster_chunks', 'glass_shards', 'concrete_chunks', 'tin_cans', 'bottles', 'twigs'):
        FILES['d:' + key] = ('../models/zone/skrunda/debris_' + key, [], 0.1, 40, 0, 0.5)
    header = [f'terrain "{name}.terrain"'] + [f'terrain_layer {i} "{SKM}{LAYERS[i]}.material" {TILING[i]:g}' for i in range(8)]
    for key, _ in sorted(types.items(), key=lambda kv: kv[1]):
        f, lods, dens, cull, shadows, far = FILES[key]
        lo, hi = (0.8, 1.2) if key.startswith('d:') else (0.85, 1.15)
        coll = 1 if key in TREES else 0
        header.append(f'foliage_type "{f}.glb" {dens:g} {lo:g} {hi:g} {cull:g} {coll} 0 {shadows} {far:g}')
        for lf, ld in lods:
            header.append(f'foliage_lod "{lf}" {ld:g}')
        if key in TREES:
            header.append('foliage_impostor 90')

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
