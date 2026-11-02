#!/usr/bin/env python3
"""Wall writing and stains on the buildings of an existing, hand-edited level (zone.level), in place.

    python3 -I assets/levels/source/add_wall_writing.py [zone]

Decal quads from assets/models/zone/skrunda/graffiti_*.glb (zonegen/graffiti.py), 2 cm off the walls, no collision,
no shadows, in the group "Wall writing" (under "Debris"); every run replaces that group's models. Each kind has a
reason in the zone's story:
  - conscripts' and teenagers' slogans (Tsoi, DMB, no exit) low on the blocks, garages and goods wagons;
  - damp at the plinths and leaks down the gable ends, where the roofs have been open for years;
  - soot over the ruined garages that burnt;
  - stalker marks: arrows along the way from the rail-yard camp to the town, crosses on the wagons next to the
    anomaly field;
  - the bandits' "keep out" around the hospital they hold, radiation signs and warnings at the power plant.

Placement is checked against the real geometry, not the bounding box: rays from outside hit the model's triangles,
and a decal goes only where its corners and centre all land on one flat, outward-facing wall (no windows, doors,
balconies or pipes under it). Own random stream; CC0, part of the engine.
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
sys.path.insert(0, HERE)
from add_terrain import LEVELS, _mat, parse          # noqa: E402

DECALS = '../models/zone/skrunda/graffiti_'
GROUP = 'Wall writing'
OFF = 0.02
SLOGANS = ['tsoi', 'dmb', 'no_exit', 'tsoi']

# model file -> what its walls carry: list of (designs, count range, height range of the bottom edge, scale range)
RULES = {
    'apartment_khrushchevka': [(SLOGANS, (1, 3), (0.25, 0.7), (1.0, 1.3)), (['damp'], (1, 2), (0.15, 0.4), (0.7, 0.9)),
                               (['leak'], (0, 2), (5.0, 9.0), (0.9, 1.2))],
    'panel_block_16': [(SLOGANS, (1, 3), (0.25, 0.7), (1.0, 1.3)), (['damp'], (1, 2), (0.15, 0.4), (0.7, 0.9)),
                       (['leak'], (1, 2), (10.0, 30.0), (1.0, 1.3))],
    'school': [(SLOGANS, (1, 2), (0.3, 0.8), (1.0, 1.3)), (['damp'], (1, 2), (0.15, 0.4), (0.7, 1.0)),
               (['leak'], (0, 1), (3.0, 6.0), (0.9, 1.1))],
    'hospital': [(['keep_out'], (2, 3), (0.6, 1.2), (1.0, 1.2)), (['no_exit'], (0, 1), (0.4, 0.8), (1.0, 1.2)),
                 (['damp'], (1, 2), (0.15, 0.4), (0.8, 1.0)), (['leak'], (1, 2), (3.0, 8.0), (0.9, 1.2))],
    'garage_block': [(SLOGANS, (1, 2), (0.3, 0.8), (0.9, 1.1)), (['soot'], (1, 2), (0.0, 0.2), (0.8, 1.0))],
    'garages_pack': [(SLOGANS + ['tsoi'], (2, 3), (0.3, 0.8), (0.9, 1.1))],
    'gas_station': [(SLOGANS, (1, 2), (0.3, 0.8), (0.9, 1.1))],
    'industrial_building': [(['radiation'], (1, 2), (1.2, 1.8), (1.0, 1.2)), (['keep_out'], (0, 1), (0.8, 1.2), (1.0, 1.1)),
                            (['damp', 'leak'], (1, 2), (0.2, 3.0), (0.9, 1.2))],
    'factory_hall': [(['radiation'], (1, 2), (1.2, 1.8), (1.0, 1.2)), (['damp', 'leak'], (1, 3), (0.2, 4.0), (0.9, 1.3))],
    'machine_hall': [(['radiation'], (1, 1), (1.2, 1.8), (1.0, 1.2)), (['damp', 'leak'], (1, 2), (0.2, 4.0), (0.9, 1.3))],
    'shipping_container': [(['keep_out', 'dmb', 'tsoi'], (0, 1), (0.5, 1.0), (0.8, 1.0))],
    'rail_carriage_rust': [(['dmb', 'tsoi'], (0, 1), (0.9, 1.4), (0.8, 1.0))],     # red paint is lost on the rust
    'rail_carriage_green': [(['dmb', 'tsoi', 'no_exit'], (0, 1), (0.9, 1.4), (0.8, 1.0))],
}
CAMP = (-101.0, 33.0)
TOWN_EDGE_X = 75.0                 # stalker arrows on the west faces of the first town buildings


def load_mesh(path):
    """All triangles of a .glb in model space, (T, 3, 3) float64."""
    b = open(path, 'rb').read()
    jl = struct.unpack('<I', b[12:16])[0]
    j = json.loads(b[20:20 + jl])
    binb = b[20 + jl + 8:]
    ctype = {5120: 'i1', 5121: 'u1', 5122: 'i2', 5123: 'u2', 5125: 'u4', 5126: 'f4'}
    ncomp = {'SCALAR': 1, 'VEC2': 2, 'VEC3': 3, 'VEC4': 4}

    def acc(i):
        a = j['accessors'][i]
        bv = j['bufferViews'][a['bufferView']]
        dt = np.dtype('<' + ctype[a['componentType']])
        nc = ncomp[a['type']]
        stride = bv.get('byteStride', 0) or dt.itemsize * nc
        start = bv.get('byteOffset', 0) + a.get('byteOffset', 0)
        raw = np.frombuffer(binb, dtype=np.uint8, count=stride * (a['count'] - 1) + dt.itemsize * nc, offset=start)
        out = np.lib.stride_tricks.as_strided(raw, shape=(a['count'], stride), strides=(stride, 1))
        return np.frombuffer(out[:, :dt.itemsize * nc].tobytes(), dtype=dt).reshape(a['count'], nc)

    tris = []

    def walk(ni, parent):
        node = j['nodes'][ni]
        m = parent @ _mat(node)
        if 'mesh' in node:
            for p in j['meshes'][node['mesh']]['primitives']:
                if p.get('mode', 4) != 4:
                    continue
                pos = acc(p['attributes']['POSITION']).astype(np.float64)
                pos = pos @ m[:3, :3].T + m[:3, 3]
                idx = acc(p['indices']).reshape(-1) if 'indices' in p else np.arange(len(pos))
                tris.append(pos[idx.reshape(-1, 3)])
        for ci in node.get('children', []):
            walk(ci, m)
    scene = j.get('scenes', [{'nodes': list(range(len(j['nodes'])))}])[j.get('scene', 0)]
    for ni in scene['nodes']:
        walk(ni, np.eye(4))
    return np.concatenate(tris) if tris else np.zeros((0, 3, 3))


def raycast(T, o, d):
    """Nearest hit (t, unit normal) of a ray against triangles T, or None (Moller-Trumbore, both sides)."""
    v0, e1, e2 = T[:, 0], T[:, 1] - T[:, 0], T[:, 2] - T[:, 0]
    p = np.cross(d, e2)
    det = np.einsum('ij,ij->i', e1, p)
    ok = np.abs(det) > 1e-9
    inv = np.where(ok, 1.0 / np.where(ok, det, 1.0), 0.0)
    s = o - v0
    u = np.einsum('ij,ij->i', s, p) * inv
    q = np.cross(s, e1)
    v = (q @ d) * inv
    t = np.einsum('ij,ij->i', e2, q) * inv
    hit = ok & (u >= 0) & (v >= 0) & (u + v <= 1) & (t > 1e-4)
    if not hit.any():
        return None
    k = int(np.argmin(np.where(hit, t, np.inf)))
    n = np.cross(e1[k], e2[k])
    n /= max(np.linalg.norm(n), 1e-12)
    return float(t[k]), n


def design_size(design):
    from add_terrain import gltf_bounds
    lo, hi = gltf_bounds(os.path.join(LEVELS, DECALS + design + '.glb'))
    return float(hi[0] - lo[0]), float(hi[1] - lo[1])


def fit(T, lo, hi, side, along, y, w, h):
    """Model-space ray test of a w x h decal (bottom centre at `along`, `y`) on face `side` (+x, -x, +z, -z).
    Returns the bottom-centre point on the wall (model space) or None."""
    axis = 0 if side[1] == 'x' else 2
    sign = 1.0 if side[0] == '+' else -1.0
    d = np.zeros(3)
    d[axis] = -sign
    other = 2 - axis

    def ray(a, yy):
        o = np.zeros(3)
        o[axis] = (hi[axis] if sign > 0 else lo[axis]) + sign * 1.0
        o[other] = a
        o[1] = yy
        return raycast(T, o, d)
    centre = ray(along, y + 0.5 * h)
    if centre is None or abs(float(centre[1] @ d)) < 0.97:
        return None
    t0 = centre[0]
    for a, yy in ((along - 0.45 * w, y + 0.08), (along + 0.45 * w, y + 0.08), (along - 0.45 * w, y + 0.92 * h),
                  (along + 0.45 * w, y + 0.92 * h), (along, y + 0.08), (along, y + 0.92 * h)):
        r = ray(a, yy)
        if r is None or abs(r[0] - t0) > 0.03 or abs(float(r[1] @ d)) < 0.97:
            return None
    p = np.zeros(3)
    p[axis] = (hi[axis] if sign > 0 else lo[axis]) + sign * 1.0 - sign * t0 + sign * OFF
    p[other] = along
    p[1] = y
    return p


TURN = {'+z': 0.0, '-z': 180.0, '+x': 90.0, '-x': -90.0}


def build(name):
    path = os.path.join(LEVELS, name + '.level')
    lines = open(path, encoding='utf-8').read().split('\n')
    objs = parse(lines)
    # drop what an earlier run added (models in the group), keep the group line
    drop = set()
    for o in objs:
        if o['kind'] == 'model' and any(lines[k].strip() == f'parent "{GROUP}"' for k in range(o['i'], o['j'])):
            drop.update(range(o['i'], o['j']))
    lines = [ln for k, ln in enumerate(lines) if k not in drop]
    if not any(ln.startswith(f'group "{GROUP}"') for ln in lines):
        gi = next(k for k, ln in enumerate(lines) if ln.startswith('group "Debris"'))
        lines.insert(gi + 1, f'group "{GROUP}" "Debris"')
    objs = parse(lines)
    anomalies = [(o['x'], o['z']) for o in objs if o['kind'] == 'entity' and '"anomaly"' in lines[o['i']]]

    rng = random.Random(1986)
    meshes = {}
    out = []
    counts = {}

    def place(o, T, lo, hi, design, y_range, s_range, sides, tries=40, spacing=None):
        w0, h0 = design_size(design)
        for _ in range(tries):
            sc = rng.uniform(*s_range) * (1.0 / o['scale'])
            side = rng.choice(sides)
            axis_other = 2 if side[1] == 'x' else 0
            w, h = w0 * sc, h0 * sc
            a0, a1 = lo[axis_other] + w / 2 + 0.3, hi[axis_other] - w / 2 - 0.3
            if a1 <= a0:
                continue
            along = rng.uniform(a0, a1)
            y = rng.uniform(*y_range) / o['scale']
            p = fit(T, lo, hi, side, along, y, w, h)
            if p is None:
                continue
            ang = math.radians(o['yaw'])
            c, s = math.cos(ang), math.sin(ang)
            wx = o['x'] + (p[0] * c + p[2] * s) * o['scale']
            wz = o['z'] + (-p[0] * s + p[2] * c) * o['scale']
            wy = o['y'] + p[1] * o['scale']
            if any(math.hypot(wx - q[1], wz - q[3]) < (spacing or 2.5) and abs(wy - q[2]) < 2.0 for q in out):
                continue
            out.append((design, wx, wy, wz, (o['yaw'] + TURN[side]) % 360.0, sc * o['scale']))
            counts[design] = counts.get(design, 0) + 1
            return True
        return False

    for o in objs:
        if o['kind'] != 'model':
            continue
        stem = os.path.splitext(os.path.basename(o['path']))[0]
        if stem not in RULES or o['tilt']:
            continue
        mp = os.path.normpath(os.path.join(LEVELS, o['path']))
        if mp not in meshes:
            T = load_mesh(mp)
            meshes[mp] = (T, T.reshape(-1, 3).min(0), T.reshape(-1, 3).max(0))
        T, lo, hi = meshes[mp]
        # wagons: the long sides only (the ends are coupled to the next wagon)
        sides = ['+x', '-x'] if stem.startswith('rail_carriage') else ['+x', '-x', '+z', '-z']
        for designs, (n0, n1), y_range, s_range in RULES[stem]:
            for _ in range(rng.randint(n0, n1)):
                place(o, T, lo, hi, rng.choice(designs), y_range, s_range, sides)
        near_anomaly = min((math.hypot(o['x'] - ax, o['z'] - az) for ax, az in anomalies), default=1e9)
        if stem.startswith('rail_carriage') and near_anomaly < 30:      # stalkers mark the anomaly field
            place(o, T, lo, hi, 'cross', (1.0, 1.5), (0.9, 1.1), sides)
        elif stem.startswith('rail_carriage') and math.hypot(o['x'] - CAMP[0], o['z'] - CAMP[1]) < 70 and rng.random() < 0.6:
            place(o, T, lo, hi, 'arrow_clear', (1.0, 1.5), (0.9, 1.1), sides)
        elif o['x'] < TOWN_EDGE_X and stem in ('apartment_khrushchevka', 'gas_station', 'school', 'garage_block'):
            # the way into the town: arrows on the faces that look back west, towards the camp
            west = [sd for sd in sides if math.sin(math.radians(o['yaw'] + TURN[sd])) < -0.7]   # normal (sin, cos) of yaw
            if west:
                place(o, T, lo, hi, 'arrow_clear', (1.1, 1.5), (1.0, 1.1), west)

    block = []
    for k, (design, x, y, z, yaw, sc) in enumerate(out):
        block += [f'model "{design.replace("_", " ").capitalize()}" "{DECALS}{design}.glb" {x:.2f} {y:.2f} {z:.2f}',
                  f'  parent "{GROUP}"', f'  transform 0 {yaw:.1f} 0 {sc:.3g}', '  cull_distance 70', '  collision 0',
                  '  cast_shadows 0']
    gi = next(k for k, ln in enumerate(lines) if ln.startswith('entity '))
    lines = lines[:gi] + block + lines[gi:]
    open(path, 'w', encoding='utf-8').write('\n'.join(lines))
    print(f'{name}: {len(out)} decals on walls', dict(sorted(counts.items())))


if __name__ == '__main__':
    for a in sys.argv[1:] or ['zone']:
        build(a)
