#!/usr/bin/env python3
"""Ground-cover normals (CC0 tool): bends the vertex normals of grass blades towards straight up, in place.

Scanned grass clumps come with the normals of the thin blades themselves, pointing sideways or down, so in
sunlight every clump turns into a dark spot on the bright ground (a "polka dot" meadow). Real grass seen
from eye height is lit like the ground it covers, so the usual trick is to light the blades with (mostly)
the ground normal. pbr.frag keeps the up component on the back faces of alpha-masked foliage, so both sides
of a blade stay lit.

    python3 -I grass_normals.py file.glb [more.glb ...] [--up 0.8] [--only grass]

--up: how far towards +Y (0 = unchanged, 1 = straight up); --only: materials whose name contains this
(default "grass"). Writes the file back; running it twice bends further, so run it on the source models.
"""
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from thin_glb import read_glb        # noqa: E402
from recolor_glb import write_glb    # noqa: E402


def bend(path, up=0.8, only='grass'):
    j, binb = read_glb(path)
    done = set()
    count = 0
    for mesh in j.get('meshes', []):
        for p in mesh['primitives']:
            mat = j['materials'][p['material']].get('name', '') if 'material' in p else ''
            if only not in mat or 'NORMAL' not in p['attributes']:
                continue
            ai = p['attributes']['NORMAL']
            if ai in done:
                continue
            done.add(ai)
            a = j['accessors'][ai]
            bv = j['bufferViews'][a['bufferView']]
            stride = bv.get('byteStride', 12) or 12
            start = bv.get('byteOffset', 0) + a.get('byteOffset', 0)
            for k in range(a['count']):
                o = start + k * stride
                n = np.frombuffer(bytes(binb[o:o + 12]), np.float32).copy()
                n = n * (1.0 - up) + np.array([0.0, 1.0, 0.0], np.float32) * up
                n /= max(float(np.linalg.norm(n)), 1e-6)
                binb[o:o + 12] = n.astype(np.float32).tobytes()
            count += a['count']
    write_glb(path, j, binb)
    print(f'{path}: {count} normals bent up')


if __name__ == '__main__':
    args = sys.argv[1:]
    opt = {'up': 0.8, 'only': 'grass'}
    for key in ('--up', '--only'):
        if key in args:
            i = args.index(key)
            opt[key[2:]] = args[i + 1]
            del args[i:i + 2]
    for f in args:
        bend(f, float(opt['up']), opt['only'])
