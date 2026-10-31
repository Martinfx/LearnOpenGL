"""Skrunda-1 site: buildings from the OSM footprints placed on the terrain, shared by the model builder
(terrain tiles) and the level script (heights for buildings, vegetation, props). CC0.

Map data (c) OpenStreetMap contributors, ODbL 1.0 (assets/levels/source/osm/).
"""
import json
import math
import os
import numpy as np
from .mats import ROOT
from .terrain import Terrain, ROAD_HALF, PAVED

OSM = os.path.join(ROOT, 'levels', 'source', 'osm', 'skrunda1.json')
X0, Z0, SIZE, TILE = -330.0, -320.0, 576.0, 64.0
NT = int(SIZE / TILE)
PLAY = (-300.0, -290.0, 160.0, 210.0)       # x0, z0, x1, z1 of the playable town area


def load():
    return json.load(open(OSM))


def buildings(osm, by_id):
    """List of dicts: id, model, cx, cz, length, width, obb_yaw (long side angle in degrees, world)."""
    out = []
    for b in osm['buildings']:
        if b['id'] not in by_id:
            continue
        o = b['obb']
        out.append(dict(id=b['id'], model=by_id[b['id']], cx=o['cx'], cz=o['cz'], length=o['length'], width=o['width'],
                        obb_yaw=o['yaw'], tags=b['tags']))
    return out


def ponds(osm):
    """Wetland next to the hospital becomes a pond (the real one is a reed swamp)."""
    res = []
    for a in osm['areas']:
        if a['tags'].get('natural') in ('wetland', 'water'):
            xs = [p[0] for p in a['pts']]
            zs = [p[1] for p in a['pts']]
            if max(xs) > X0 and min(xs) < X0 + SIZE and max(zs) > Z0 and min(zs) < Z0 + SIZE:
                res.append(a['pts'])
    return res


_T = None


def terrain(by_id):
    global _T
    if _T is None:
        osm = load()
        bl = buildings(osm, by_id)
        rects = [(b['cx'], b['cz'], b['length'] + 0.2, b['width'] + 0.2, b['obb_yaw']) for b in bl]
        _T = Terrain(osm, X0, Z0, SIZE, SIZE, 1.0, seed=7, buildings=rects, pond_areas=ponds(osm))
        _T.blist = bl
        for b, hb in zip(bl, _T.platforms):
            b['y'] = hb
    return _T


def placement(t, b):
    """Engine yaw (model rot_y) so that the long side runs along the footprint and the entrance (+Z local)
    faces the nearer road. Returns (yaw_deg, entrance normal (nx, nz))."""
    th = math.radians(b['obb_yaw'])
    n = (-math.sin(th), math.cos(th))
    best = None
    for s in (1, -1):
        px = b['cx'] + s * n[0] * (b['width'] / 2 + 10)
        pz = b['cz'] + s * n[1] * (b['width'] / 2 + 10)
        d = min(float(t.field('road_' + k, px, pz)) - ROAD_HALF[k] for k in ('unclassified', 'service', 'footway'))
        if best is None or d < best[0]:
            best = (d, s)
    s = best[1]
    yaw = -b['obb_yaw'] + (0 if s > 0 else 180)
    return yaw, (s * n[0], s * n[1])
