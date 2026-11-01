#!/usr/bin/env python3
"""Seasonal colour variant of a vegetation glb (CC0 tool): every base-colour texture is recoloured towards
a target colour by its luminance (green grass -> last year's straw), alpha and normals untouched.

    python3 -I recolor_glb.py in.glb out.glb R G B amount [--only NAME] [--rough R]
    e.g. straw: 0.86 0.74 0.5 0.85; tree leaves only: --only leaves --rough 0.95
--only recolours just the materials whose name contains NAME; --rough sets their roughness (matte leaves
do not mirror the blue sky).
"""
import io
import json
import os
import struct
import sys

import numpy as np
from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from thin_glb import read_glb       # noqa: E402


def compact(j, binb):
    """Drop buffer views nothing references any more (replaced images, thinned index lists)."""
    used = sorted({a['bufferView'] for a in j.get('accessors', []) if 'bufferView' in a} |
                  {im['bufferView'] for im in j.get('images', []) if 'bufferView' in im})
    out, remap = bytearray(), {}
    for old in used:
        bv = j['bufferViews'][old]
        o = bv.get('byteOffset', 0)
        while len(out) % 4:
            out.append(0)
        nb = dict(bv, byteOffset=len(out))
        out += binb[o:o + bv['byteLength']]
        remap[old] = len(remap)
        j['bufferViews'][old] = nb
    j['bufferViews'] = [j['bufferViews'][i] for i in used]
    for a in j.get('accessors', []) + j.get('images', []):
        if 'bufferView' in a:
            a['bufferView'] = remap[a['bufferView']]
    return out


def write_glb(path, j, binb):
    binb = compact(j, binb)
    while len(binb) % 4:
        binb.append(0)
    j['buffers'][0]['byteLength'] = len(binb)
    js = json.dumps(j, separators=(',', ':')).encode()
    js += b' ' * (-len(js) % 4)
    with open(path, 'wb') as f:
        f.write(struct.pack('<III', 0x46546C67, 2, 12 + 8 + len(js) + 8 + len(binb)))
        f.write(struct.pack('<I4s', len(js), b'JSON') + js)
        f.write(struct.pack('<I4s', len(binb), b'BIN\0') + bytes(binb))


def recolor(src, dst, color, amount, only=None, rough=None):
    j, binb = read_glb(src)
    target = np.array(color, np.float32)
    images = set()
    for m in j.get('materials', []):
        if only and only not in m.get('name', ''):
            continue
        if rough is not None:
            m.setdefault('pbrMetallicRoughness', {})['roughnessFactor'] = rough
        t = m.get('pbrMetallicRoughness', {}).get('baseColorTexture')
        if t is not None:
            images.add(j['textures'][t['index']]['source'])
    for i in sorted(images):
        img = j['images'][i]
        bv = j['bufferViews'][img['bufferView']]
        o = bv.get('byteOffset', 0)
        im = Image.open(io.BytesIO(bytes(binb[o:o + bv['byteLength']]))).convert('RGBA')
        a = np.asarray(im, np.float32) / 255.0
        rgb = a[..., :3]
        lum = rgb @ np.array([0.299, 0.587, 0.114], np.float32)
        # keep the blade-to-blade variation: brightness from the original, hue from the target
        dry = np.clip(lum[..., None] / max(float(lum[a[..., 3] > 0.5].mean()) if (a[..., 3] > 0.5).any() else 0.4, 1e-3)
                      * target, 0, 1)
        a[..., :3] = rgb * (1 - amount) + dry * amount
        buf = io.BytesIO()
        Image.fromarray(np.clip(a * 255 + 0.5, 0, 255).astype(np.uint8), 'RGBA').save(buf, 'PNG', optimize=True)
        data = buf.getvalue()
        while len(binb) % 4:
            binb.append(0)
        j['bufferViews'].append(dict(buffer=0, byteOffset=len(binb), byteLength=len(data)))
        binb += data
        img['bufferView'] = len(j['bufferViews']) - 1
        img['mimeType'] = 'image/png'
    write_glb(dst, j, binb)
    print(f'{dst}: {len(images)} textures recoloured')


if __name__ == '__main__':
    a = sys.argv[1:]
    opt = {}
    for k in ('--only', '--rough'):
        if k in a:
            i = a.index(k)
            opt[k[2:]] = a[i + 1]
            del a[i:i + 2]
    recolor(a[0], a[1], [float(x) for x in a[2:5]], float(a[5]), opt.get('only'),
            float(opt['rough']) if 'rough' in opt else None)
