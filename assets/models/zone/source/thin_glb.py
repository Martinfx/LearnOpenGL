#!/usr/bin/env python3
"""Far-distance LOD for vegetation clumps (CC0): keeps a random fraction of the connected pieces
(blades, leaves) of every primitive and drops the rest (--keep-big: the largest pieces such as a trunk always stay). Materials, textures and vertices are kept,
only the index lists shrink, so the look stays the same, just sparser.

    python3 -I thin_glb.py in.glb out.glb keep_fraction [seed] [--keep-big] [--only MATERIAL_NAME_PART]
"""
import json
import struct
import sys

import numpy as np


def read_glb(path):
    b = open(path, 'rb').read()
    jl = struct.unpack('<I', b[12:16])[0]
    j = json.loads(b[20:20 + jl])
    off = 20 + jl
    bl = struct.unpack('<I', b[off:off + 4])[0]
    return j, bytearray(b[off + 8:off + 8 + bl])


def accessor(j, binb, i):
    a = j['accessors'][i]
    bv = j['bufferViews'][a['bufferView']]
    dt = {5121: np.uint8, 5123: np.uint16, 5125: np.uint32, 5126: np.float32}[a['componentType']]
    nc = {'SCALAR': 1, 'VEC2': 2, 'VEC3': 3, 'VEC4': 4}[a['type']]
    start = bv.get('byteOffset', 0) + a.get('byteOffset', 0)
    stride = bv.get('byteStride', 0)
    if stride and stride != nc * np.dtype(dt).itemsize:
        raw = np.frombuffer(bytes(binb), np.uint8)
        rows = [np.frombuffer(raw[start + k * stride:start + k * stride + nc * np.dtype(dt).itemsize].tobytes(), dt)
                for k in range(a['count'])]
        return np.array(rows).reshape(a['count'], nc)
    return np.frombuffer(bytes(binb[start:start + a['count'] * nc * np.dtype(dt).itemsize]), dt).reshape(a['count'], nc)


def components(tri, pos):
    """Connected pieces of a triangle list, welding vertices by position (split UV seams stay joined)."""
    key = np.round(pos / 1e-4).astype(np.int64)
    _, weld = np.unique(key, axis=0, return_inverse=True)
    weld = weld.reshape(-1)
    parent = np.arange(weld.max() + 1)

    def find(x):
        while parent[x] != x:
            parent[x] = parent[parent[x]]
            x = parent[x]
        return x
    for t in weld[tri]:
        ra, rb, rc = find(t[0]), find(t[1]), find(t[2])
        parent[rb] = ra
        parent[find(rc)] = ra
    return np.array([find(weld[t[0]]) for t in tri])


def thin(src, dst, keep, seed=1, keep_big=False, only=None):
    j, binb = read_glb(src)
    mats = j.get('materials', [])
    rng = np.random.default_rng(seed)
    before = after = 0
    for m in j['meshes']:
        for p in m['primitives']:
            if only and only not in mats[p.get('material', 0)].get('name', ''):
                continue
            idx = accessor(j, binb, p['indices']).reshape(-1, 3).astype(np.uint32)
            pos = accessor(j, binb, p['attributes']['POSITION'])
            comp = components(idx, pos)
            ids = np.unique(comp)
            kept = set(rng.choice(ids, max(1, int(round(len(ids) * keep))), replace=False).tolist())
            sizes = {c: n for c, n in zip(*np.unique(comp, return_counts=True))}
            big = max(sizes.values())
            if keep_big:
                kept |= {c for c, n in sizes.items() if n >= 0.25 * big and n > 8}     # trunk and big branches stay
            new = idx[np.array([c in kept for c in comp])].reshape(-1)
            before += len(idx)
            after += len(new) // 3
            while len(binb) % 4:
                binb.append(0)
            j['bufferViews'].append(dict(buffer=0, byteOffset=len(binb), byteLength=new.nbytes, target=34963))
            binb += new.astype('<u4').tobytes()
            j['accessors'].append(dict(bufferView=len(j['bufferViews']) - 1, componentType=5125, count=int(len(new)),
                                       type='SCALAR'))
            p['indices'] = len(j['accessors']) - 1
    while len(binb) % 4:
        binb.append(0)
    j['buffers'][0]['byteLength'] = len(binb)
    js = json.dumps(j, separators=(',', ':')).encode()
    js += b' ' * (-len(js) % 4)
    total = 12 + 8 + len(js) + 8 + len(binb)
    with open(dst, 'wb') as f:
        f.write(struct.pack('<III', 0x46546C67, 2, total))
        f.write(struct.pack('<I4s', len(js), b'JSON') + js)
        f.write(struct.pack('<I4s', len(binb), b'BIN\0') + bytes(binb))
    print(f'{dst}: {before} -> {after} tris')


if __name__ == '__main__':
    a = [x for x in sys.argv[1:] if x != '--keep-big']
    only = None
    if '--only' in a:
        i = a.index('--only')
        only = a[i + 1]
        del a[i:i + 2]
    thin(a[0], a[1], float(a[2]), int(a[3]) if len(a) > 3 else 1, '--keep-big' in sys.argv, only)
