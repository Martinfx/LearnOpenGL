#!/usr/bin/env python3
"""Joint decals on an existing, hand-edited level (zone.level, hospital_street.level), in place.

    python3 -I assets/levels/source/add_joints.py [zone hospital_street]

Ties the hard things into the ground (docs/LEVEL_ART_GUIDE.md, "Proč to působí amatérsky", point 1), with the
decal quads of assets/models/zone/source/zonegen/joints.py, in the group "Ground joints" (under "Debris"); every
run replaces that group's models:
  - soil, sand and leaves blown against every kerb, lying in a strip on the asphalt;
  - broken, mossy asphalt edges where a road meets the verge;
  - dark splash-back and damp at the foot of every building wall, only where the wall is flat (ray test against
    the model's triangles: no strip across a door or a gap);
  - ballast stones spilled from the track bed into the grass on both sides of every track.
Own random stream; CC0, part of the engine.
"""
import math
import os
import random
import struct
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from add_terrain import LEVELS, parse, footprint              # noqa: E402
from add_wall_writing import load_mesh, fit, TURN             # noqa: E402

DEC = '../models/zone/skrunda/joint_'
GROUP = 'Ground joints'
LIFT = 0.012                 # above the surface it lies on (no z-fighting, no visible gap)
NOT_BUILDINGS = ('Carriage', 'Track', 'River', 'Far', 'Skyline', 'Car', 'Bus', 'Van', 'Tree', 'birch', 'dead_tree', 'bush',
                 'grass', 'rubble', 'slab', 'Lamp', 'Puddle', 'puddle', 'Fence', 'fence', 'Guardrail', 'Pipe', 'Sign',
                 'Gantry', 'Cooling', 'Bridge', 'Conveyor', 'Locomotive', 'Tender', 'Hesco', 'Barrier', 'Sandbag')


class Ground:
    """Heights of the level's engine terrain (<level>.terrain, version 2)."""

    def __init__(self, path):
        b = open(path, 'rb').read()
        _m, _v, self.n, _s, self.x0, self.z0, self.size, _c = struct.unpack('<IIiifffI', b[:32])
        self.H = np.frombuffer(b[32:32 + self.n * self.n * 4], '<f4').reshape(self.n, self.n)
        self.step = self.size / (self.n - 1)

    def __call__(self, x, z):
        fx, fz = (x - self.x0) / self.step, (z - self.z0) / self.step
        i, j = int(min(max(fx, 0), self.n - 2)), int(min(max(fz, 0), self.n - 2))
        tx, tz = min(max(fx - i, 0), 1), min(max(fz - j, 0), 1)
        H = self.H
        return float((H[j, i] * (1 - tx) + H[j, i + 1] * tx) * (1 - tz) + (H[j + 1, i] * (1 - tx) + H[j + 1, i + 1] * tx) * tz)


def build(name):
    path = os.path.join(LEVELS, name + '.level')
    lines = open(path, encoding='utf-8').read().split('\n')
    objs = parse(lines)
    drop = set()
    for o in objs:
        if o['kind'] == 'model' and any(lines[k].strip() == f'parent "{GROUP}"' for k in range(o['i'], o['j'])):
            drop.update(range(o['i'], o['j']))
    lines = [ln for k, ln in enumerate(lines) if k not in drop]
    if not any(ln.startswith(f'group "{GROUP}"') for ln in lines):
        gi = next((k for k, ln in enumerate(lines) if ln.startswith('group "Debris"')), None)
        if gi is not None:
            lines.insert(gi + 1, f'group "{GROUP}" "Debris"')
        else:
            gi = max(k for k, ln in enumerate(lines) if ln.startswith('group '))
            lines.insert(gi + 1, f'group "{GROUP}" ""')
    objs = parse(lines)
    ground = Ground(os.path.join(LEVELS, name + '.terrain'))
    rng = random.Random(4242)
    out = []

    def yaw_of(nx, nz):                    # model yaw that turns local +Z onto the world direction (nx, nz)
        return math.degrees(math.atan2(nx, nz)) % 360.0

    # ------------------------------------------------------------------ kerbs and road edges
    boxes = [o for o in objs if o['kind'] == 'box' and min(o['sx'], o['sz']) < 50]

    def box_top_at(x, z, skip):
        for b in boxes:
            if b is not skip and abs(x - b['x']) <= b['sx'] / 2 and abs(z - b['z']) <= b['sz'] / 2:
                return b['y'] + b['sy'] / 2
        return None
    for b in boxes:
        top = b['y'] + b['sy'] / 2
        hx, hz = b['sx'] / 2, b['sz'] / 2
        road = min(b['sx'], b['sz']) >= 6.0
        for (ax, az, bx, bz, nx, nz) in ((b['x'] - hx, b['z'] - hz, b['x'] + hx, b['z'] - hz, 0, 1),
                                         (b['x'] - hx, b['z'] + hz, b['x'] + hx, b['z'] + hz, 0, -1),
                                         (b['x'] - hx, b['z'] - hz, b['x'] - hx, b['z'] + hz, 1, 0),
                                         (b['x'] + hx, b['z'] - hz, b['x'] + hx, b['z'] + hz, -1, 0)):
            length = math.hypot(bx - ax, bz - az)
            if length < 8.5:
                continue
            centres = list(np.arange(4.0, length - 4.0, 7.3)) + [length - 4.0]
            for tc in centres:
                tc += rng.uniform(-0.4, 0.4)
                tc = min(max(tc, 4.0), length - 4.0)
                cx, cz = ax + (bx - ax) * tc / length, az + (bz - az) * tc / length
                outside = box_top_at(cx - nx * 0.4, cz - nz * 0.4, b)
                if outside is not None and outside <= top + 0.05:
                    continue            # continues into a box as high or lower: the dirt collects down there
                kind = 'verge_soil' if road and outside is None and rng.random() < 0.55 else 'kerb_soil'
                if rng.random() < 0.9:
                    out.append((kind, cx, top + LIFT, cz, yaw_of(nx, nz), rng.uniform(0.85, 1.12)))

    # ------------------------------------------------------------------ track beds
    for o in objs:
        if o['kind'] != 'model' or not o['path'].endswith('rail_track_25m.glb'):
            continue
        a = math.radians(o['yaw'])
        c, s = math.cos(a), math.sin(a)
        for side in (-1, 1):
            for lz in (-6.25, 6.25):
                lx = side * 2.2
                x, z = o['x'] + lx * c + lz * s, o['z'] - lx * s + lz * c
                nx, nz = side * c, -side * s                    # local +-X outward, in world
                y = max(ground(x + nx * 0.6, z + nz * 0.6), ground(x, z)) + LIFT
                if rng.random() < 0.9:
                    out.append(('ballast_spill', x, y, z, yaw_of(nx, nz), rng.uniform(0.98, 1.05)))

    # ------------------------------------------------------------------ feet of the walls
    meshes = {}
    for o in objs:
        if o['kind'] != 'model' or o['tilt'] or o['name'].startswith(NOT_BUILDINGS) or not o['path'].endswith('.glb'):
            continue
        r0 = footprint(o, LEVELS)
        if (r0[2] - r0[0]) * (r0[3] - r0[1]) < 20.0:
            continue
        mp = os.path.normpath(os.path.join(LEVELS, o['path']))
        if mp not in meshes:
            try:
                T = load_mesh(mp)
            except (OSError, KeyError, ValueError):
                T = np.zeros((0, 3, 3))
            if len(T) == 0 or len(T) > 400000:
                meshes[mp] = None
            else:
                P = T.reshape(-1, 3)
                meshes[mp] = (T, P.min(0), P.max(0))
        if meshes[mp] is None:
            continue
        T, lo, hi = meshes[mp]
        if hi[1] - lo[1] < 2.5:
            continue
        sc = o['scale']
        L, Hh = 3.2 / sc, 0.9 / sc
        for side in ('+x', '-x', '+z', '-z'):
            ax = 2 if side[1] == 'x' else 0
            a0, a1 = lo[ax] + L / 2 + 0.05, hi[ax] - L / 2 - 0.05
            along = a0 + rng.uniform(0, 0.5)
            while along <= a1:
                p = fit(T, lo, hi, side, along, max(lo[1], 0.0) + 0.02 / sc, L, Hh)
                if p is not None:
                    ang = math.radians(o['yaw'])
                    c, s = math.cos(ang), math.sin(ang)
                    wx = o['x'] + (p[0] * c + p[2] * s) * sc
                    wz = o['z'] + (-p[0] * s + p[2] * c) * sc
                    wy = o['y'] + p[1] * sc - 0.06
                    out.append(('wall_grime', wx, wy, wz, (o['yaw'] + TURN[side]) % 360.0, rng.uniform(0.98, 1.06)))
                    along += L * 0.94
                else:
                    along += 0.8 / sc

    block = []
    for (kind, x, y, z, yaw, sc) in out:
        block += [f'model "{kind.replace("_", " ").capitalize()}" "{DEC}{kind}.glb" {x:.2f} {y:.3f} {z:.2f}',
                  f'  parent "{GROUP}"', f'  transform 0 {yaw:.1f} 0 {sc:.3f}',
                  '  cull_distance 60', '  collision 0', '  cast_shadows 0']
    gi = next(k for k, ln in enumerate(lines) if ln.startswith('entity '))
    lines = lines[:gi] + block + lines[gi:]
    open(path, 'w', encoding='utf-8').write('\n'.join(lines))
    counts = {}
    for d in out:
        counts[d[0]] = counts.get(d[0], 0) + 1
    print(f'{name}: {len(out)} joint decals', counts)


if __name__ == '__main__':
    for a in sys.argv[1:] or ['zone', 'hospital_street']:
        build(a)
