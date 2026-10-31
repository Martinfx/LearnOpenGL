#!/usr/bin/env python3
"""Builds the Skrunda-1 buildings (CC0) into assets/models/zone/skrunda/ from the real OSM footprints.

    python3 -I assets/models/zone/source/build_skrunda.py [name ...]

Sizes come from assets/levels/source/osm/skrunda1.json (oriented bounding rectangle of each footprint).
Textures are shared with the other zone models (assets/models/zone/textures/).
"""
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from zonegen import mats, glb, skrunda          # noqa: E402

OUT = os.path.join(mats.ZONE, 'skrunda')
OSM = os.path.join(mats.ROOT, 'levels', 'source', 'osm', 'skrunda1.json')


def footprint_models():
    """One model per distinct building size; returns name -> (fn, kwargs, has_lod) and osm id -> name."""
    d = json.load(open(OSM))
    models, by_id = {}, {}
    for b in d['buildings']:
        o, tg = b['obb'], b['tags']
        if abs(o['cx']) > 330 or abs(o['cz']) > 330:
            continue
        L, W = o['length'], o['width']
        kind = tg.get('building')
        if kind == 'apartments':
            sec = max(2, round(L / 12.6))
            name = f'block_{round(L)}m_{sec}s'
            models[name] = (skrunda.block, dict(length=round(L, 1), sections=sec, depth=round(W, 1), seed=int(b['id']) % 97), True)
        elif kind == 'hospital':
            name = 'hospital_56m'
            models[name] = (skrunda.corridor, dict(length=round(L, 1), depth=round(W, 1), floors=3, seed=5, sign_row=0, inn='stair_paint',
                                                   ext='silicate_dirty'), True)
        elif kind == 'retail':
            name = 'shop_venta'
            models[name] = (skrunda.shop, dict(length=round(L, 1), width=round(W, 1), seed=3, sign_row=1), True)
        elif b['id'] == '184415657':
            name = 'hq_3floors'
            models[name] = (skrunda.corridor, dict(length=round(L, 1), depth=round(W, 1), floors=3, seed=9, ext='plaster_yellow',
                                                   inn='plaster_peel', floor_mat='wood_floor', sign_row=4, beds=False), True)
        elif b['id'] == '184415665':
            name = 'canteen'
            models[name] = (skrunda.shop, dict(length=round(L, 1), width=round(W, 1), seed=4, ext='silicate', sign_row=2,
                                               kind='canteen'), True)
        elif tg.get('man_made') == 'water_tower':
            name = 'water_tower'
            models[name] = (skrunda.water_tower, dict(), True)
        elif L > 6:
            name = f'kiosk_{round(L)}m'
            models[name] = (skrunda.kiosk, dict(length=round(L, 1), width=round(W, 1), seed=1), False)
        else:
            name = 'shed'
            models[name] = (skrunda.shed, dict(length=max(3.5, round(L, 1)), width=max(3.0, round(W, 1))), False)
        by_id[b['id']] = name
    models['fence_po2_x5'] = (skrunda.fence_po2, dict(n=5), True)
    models['fence_po2_x5_broken'] = (skrunda.fence_po2, dict(n=5, broken=True, seed=2), True)
    models['fence_po2_x1'] = (skrunda.fence_po2, dict(n=1), True)
    models['sandbags_3m'] = (skrunda.sandbags, dict(length=3.0, rows=5, seed=1), False)
    models['sandbags_5m'] = (skrunda.sandbags, dict(length=5.0, rows=6, seed=2), False)
    return models, by_id


MODELS, BY_ID = footprint_models()


def build(name):
    fn, kw, has_lod = MODELS[name]
    os.makedirs(OUT, exist_ok=True)
    mesh = fn(**kw)
    glb.write_glb(os.path.join(OUT, f'{name}.glb'), mesh, tex_rel_dir='../textures', name=name)
    lo, hi = mesh.bounds()
    msg = f'{name}: {mesh.triangles()} tris, {hi[0]-lo[0]:.1f} x {hi[1]-lo[1]:.1f} x {hi[2]-lo[2]:.1f} m (min y {lo[1]:.2f})'
    if has_lod:
        lod = fn(**kw, lod=1)
        glb.write_glb(os.path.join(OUT, f'{name}_lod1.glb'), lod, tex_rel_dir='../textures', name=name + '_lod1')
        msg += f', lod1 {lod.triangles()} tris'
    print(msg, flush=True)




def build_terrain(only=None):
    """Terrain tiles (9 x 9 of 64 m) into assets/models/zone/skrunda/terrain/."""
    from zonegen import skrunda_site as site
    t = site.terrain(BY_ID)
    out = os.path.join(OUT, 'terrain')
    os.makedirs(out, exist_ok=True)
    for j in range(site.NT):
        for i in range(site.NT):
            if only and (i, j) not in only:
                continue
            x0, z0 = site.X0 + i * site.TILE, site.Z0 + j * site.TILE
            m = t.tile(x0, z0, site.TILE, 1.0, lod=0)
            glb.write_glb(os.path.join(out, f'tile_{i}_{j}.glb'), m, tex_rel_dir='../../textures', name=f'tile_{i}_{j}')
            m1 = t.tile(x0, z0, site.TILE, 4.0, lod=1)
            glb.write_glb(os.path.join(out, f'tile_{i}_{j}_lod1.glb'), m1, tex_rel_dir='../../textures', name=f'tile_{i}_{j}_lod1')
            print(f'tile_{i}_{j}: {m.triangles()} tris, lod1 {m1.triangles()}', flush=True)


if __name__ == '__main__':
    mats.ensure_textures()
    skrunda.make_signs()
    args = sys.argv[1:]
    if args[:1] == ['terrain']:
        build_terrain()
    else:
        for n in args or list(MODELS):
            build(n)
