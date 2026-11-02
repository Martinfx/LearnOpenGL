#!/usr/bin/env python3
"""Material audit of a level: does everything look like it comes from one world? (CC0 tool)

    python3 -I assets/levels/source/material_audit.py zone hospital_street skrunda1 [--md out.md]

For every model the level uses (and the terrain layers) it measures, per material:
  - texel density: texture pixels per metre of surface (from the UV and world area of the triangles);
  - albedo: mean colour of the base colour texture times the factor, as sRGB luminance and saturation.
Professional sets keep texel density within about 2x of a target (here: the level median) and albedo inside a
narrow, physically plausible band (sRGB 50-240, most surfaces 60-180) with a common saturation; anything far
outside is listed as an outlier, with the reason. See docs/LEVEL_ART_GUIDE.md, "Proč to působí amatérsky".
"""
import colorsys
import json
import os
import re
import struct
import sys

import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from add_terrain import LEVELS, _mat          # noqa: E402

_TEX = {}


def tex_stats(path):
    """(width, height, mean linear-ish sRGB colour 0..1) of a texture file."""
    if path in _TEX:
        return _TEX[path]
    try:
        im = Image.open(path).convert('RGB')
        w, h = im.size
        c = np.asarray(im.resize((64, 64)), np.float32).reshape(-1, 3).mean(0) / 255
        _TEX[path] = (w, h, c)
    except (OSError, ValueError):
        _TEX[path] = None
    return _TEX[path]


def glb_materials(path):
    """Per material: (name, texture path or None, factor rgb, world area m2, uv area) of a .glb / .gltf."""
    b = open(path, 'rb').read()
    if b[:4] == b'glTF':
        jl = struct.unpack('<I', b[12:16])[0]
        j = json.loads(b[20:20 + jl])
        binb = b[20 + jl + 8:]
        buffers = [binb]
    else:
        j = json.loads(b)
        buffers = [open(os.path.join(os.path.dirname(path), bf['uri']), 'rb').read() for bf in j.get('buffers', [])]
    ctype = {5120: 'i1', 5121: 'u1', 5122: 'i2', 5123: 'u2', 5125: 'u4', 5126: 'f4'}
    ncomp = {'SCALAR': 1, 'VEC2': 2, 'VEC3': 3, 'VEC4': 4}

    def acc(i):
        a = j['accessors'][i]
        bv = j['bufferViews'][a['bufferView']]
        buf = buffers[bv.get('buffer', 0)]
        dt = np.dtype('<' + ctype[a['componentType']])
        nc = ncomp[a['type']]
        stride = bv.get('byteStride', 0) or dt.itemsize * nc
        start = bv.get('byteOffset', 0) + a.get('byteOffset', 0)
        raw = np.frombuffer(buf, dtype=np.uint8, count=stride * (a['count'] - 1) + dt.itemsize * nc, offset=start)
        out = np.lib.stride_tricks.as_strided(raw, shape=(a['count'], stride), strides=(stride, 1))
        v = np.frombuffer(out[:, :dt.itemsize * nc].tobytes(), dtype=dt).reshape(a['count'], nc).astype(np.float64)
        if a.get('normalized') and dt.kind in 'iu':
            v /= float(np.iinfo(dt).max)
        return v

    def image_path(ti):
        img = j['images'][j['textures'][ti]['source']]
        if 'uri' in img:
            return os.path.normpath(os.path.join(os.path.dirname(path), img['uri']))
        return None                                      # embedded: not measured

    res = {}

    def walk(ni, parent):
        node = j['nodes'][ni]
        m = parent @ _mat(node)
        if 'mesh' in node:
            for p in j['meshes'][node['mesh']]['primitives']:
                if p.get('mode', 4) != 4 or 'TEXCOORD_0' not in p['attributes']:
                    continue
                mi = p.get('material')
                mat = j['materials'][mi] if mi is not None else {}
                pbr = mat.get('pbrMetallicRoughness', {})
                if mat.get('alphaMode') == 'BLEND':
                    continue                             # decals and glass: not a surface colour
                pos = acc(p['attributes']['POSITION']) @ m[:3, :3].T
                uv = acc(p['attributes']['TEXCOORD_0'])
                idx = acc(p['indices']).astype(np.int64).reshape(-1, 3) if 'indices' in p else np.arange(len(pos)).reshape(-1, 3)
                P, U = pos[idx], uv[idx]
                wa = 0.5 * np.linalg.norm(np.cross(P[:, 1] - P[:, 0], P[:, 2] - P[:, 0]), axis=1).sum()
                a_, b_ = U[:, 1] - U[:, 0], U[:, 2] - U[:, 0]
                ua = 0.5 * np.abs(a_[:, 0] * b_[:, 1] - a_[:, 1] * b_[:, 0]).sum()
                tex = image_path(pbr['baseColorTexture']['index']) if 'baseColorTexture' in pbr else None
                key = mat.get('name', f'm{mi}')
                f = pbr.get('baseColorFactor', [1, 1, 1, 1])[:3]
                r = res.setdefault(key, [tex, f, 0.0, 0.0])
                r[2] += wa
                r[3] += ua
        for ci in node.get('children', []):
            walk(ci, m)
    scene = j.get('scenes', [{'nodes': list(range(len(j.get('nodes', []))))}])[j.get('scene', 0)]
    for ni in scene['nodes']:
        walk(ni, np.eye(4))
    return res


def audit(levels):
    rows = []
    seen = set()
    for name in levels:
        text = open(os.path.join(LEVELS, name + '.level'), encoding='utf-8').read()
        paths = sorted(set(re.findall(r'^model "[^"]*" "([^"]+\.(?:glb|gltf))"', text, re.M)))
        for rel in paths:
            p = os.path.normpath(os.path.join(LEVELS, rel))
            if p in seen or not os.path.exists(p) or '/skrunda/joint_' in p or '/graffiti_' in p:
                continue
            seen.add(p)
            try:
                mats = glb_materials(p)
            except (OSError, KeyError, ValueError, IndexError):
                continue
            for mname, (tex, f, wa, ua) in mats.items():
                if wa < 0.5:
                    continue
                st = tex_stats(tex) if tex else None
                if st is None:
                    continue
                tw, th, c = st
                col = np.clip(c * np.array(f), 0, 1)
                dens = float(np.sqrt(ua * tw * th / wa)) if wa > 0 else 0.0
                h, l, s = colorsys.rgb_to_hls(*col)
                lum = float(0.2126 * col[0] + 0.7152 * col[1] + 0.0722 * col[2]) * 255
                rows.append(dict(model=os.path.relpath(p, os.path.join(LEVELS, '..')), mat=mname, dens=dens, lum=lum,
                                 sat=float(s), hue=float(h * 360), area=float(wa)))
    return rows


def report(rows, md=None):
    dens = np.array([r['dens'] for r in rows])
    area = np.array([r['area'] for r in rows])
    order = np.argsort(dens)
    cum = np.cumsum(area[order]) / area.sum()
    med = float(dens[order][np.searchsorted(cum, 0.5)])        # area-weighted median texel density
    sat_med = float(np.median([r['sat'] for r in rows]))
    out = [f'# Material audit\n', f'Area-weighted median texel density: **{med:.0f} px/m**; median saturation {sat_med:.2f}.\n',
           '| model | material | px/m | x median | albedo (sRGB lum) | saturation | why it is listed |', '|---|---|---|---|---|---|---|']
    flagged = []
    for r in rows:
        why = []
        k = r['dens'] / med
        if k < 0.35:
            why.append('blurry next to the rest (texel density < 0.35x)')
        elif k > 3.0:
            why.append('much sharper than the rest (> 3x)')
        if r['lum'] < 35:
            why.append('albedo too dark (< 35)')
        elif r['lum'] > 200:
            why.append('albedo too bright (> 200)')
        if r['sat'] > max(0.45, sat_med * 2.2) and r['lum'] > 40:
            why.append('far more saturated than the set')
        if why:
            flagged.append((r, k, why))
    flagged.sort(key=lambda t: -t[0]['area'])
    for r, k, why in flagged:
        out.append(f"| {r['model']} | {r['mat']} | {r['dens']:.0f} | {k:.2f} | {r['lum']:.0f} | {r['sat']:.2f} | {'; '.join(why)} |")
    text = '\n'.join(out) + '\n'
    if md:
        open(md, 'w', encoding='utf-8').write(text)
    return text, flagged, med


if __name__ == '__main__':
    args = sys.argv[1:]
    md = None
    if '--md' in args:
        i = args.index('--md')
        md = args[i + 1]
        del args[i:i + 2]
    rows = audit(args or ['zone', 'hospital_street', 'skrunda1'])
    text, flagged, med = report(rows, md)
    print(f'{len(rows)} materials, median {med:.0f} px/m, {len(flagged)} listed')
    print('\n'.join(text.split('\n')[:40]))
