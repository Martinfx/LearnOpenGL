#!/usr/bin/env python3
"""Mulberry Street (assets/levels/overgrown_street.level + .terrain), original, CC0.

    python3 -I assets/levels/source/make_overgrown_street.py [--models]

--models first (re)builds the houses and street fixtures into assets/models/zone/street/.

martin 2026-11-03: "put everything into Mulberry Street, make it look realistic, after real architecture, follow the
rules of game design". docs/LEVEL_ART_GUIDE.md sections 10 to 12 hold the research and the plan; the plan itself is
mulberry_layout.py (docs/images/mulberry_plan.png):
  - A Lower Manhattan grid in small: Baxter, Mulberry and Mott Street north-south, Canal, Hester and Grand Street
    across. Every block face is a row of narrow walk-up tenements, one per 25-foot lot (zonegen/walkup.py), each
    with its own height, brick, cornice, shop front and fire escape; back yards meet inside the blocks.
  - Critical path: the start on wide Canal Street looks north up Mulberry to the church at its end (the weenie);
    Hester Street is the node (the camp, the quest); a collapsed house closes Mulberry between Hester and Grand, the
    way on leads through a house and the yards or round by Mott Street; Grand Street, then north to the church.
  - The edge of the map is what really closes a street: the closed fronts across Baxter, Mott and Canal Street,
    wrecks and barricades at the ends, the church. Invisible walls stand only behind them.
  - Nature has had twenty years: weeds and trees of heaven in every crack, along the kerbs and walls, the yards a
    young wood; less on wide Canal Street, where the wind keeps the asphalt bare.
"""
import json
import math
import os
import random
import struct
import sys
import zlib

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, '..', '..'))                 # assets/
LEVELS = os.path.join(ROOT, 'levels')
sys.path.insert(0, os.path.join(ROOT, 'models', 'zone', 'source'))
sys.path.insert(0, HERE)
from zonegen.terrain import Noise, smoothstep                          # noqa: E402
import mulberry_layout as LY                                           # noqa: E402

NAME = 'overgrown_street'
ST = '../models/zone/street/'
V = '../models/zone/vegetation/'
G = '../models/zone/gen/'
SK = '../models/zone/skrunda/'
PB = '../materials/pbr/'
DC = '../models/props/deadcity/'
P = '../models/props/'

WALK = 0.15              # pavement above the road
SIDEWALK = 3.5           # pavement width (Canal Street 4.5)
X0, Z0, SIZE, RES = -128.0, -280.0, 390.0, 0.5     # engine terrain
SPLAT_N = 2048
LAYERS = [(PB + 'asphalt_01.material', 0.4808), (PB + 'asphalt_02.material', 0.3333),
          (PB + 'concrete_pavement.material', 0.5556), (PB + 'concrete_moss.material', 0.3333),
          ('../materials/skrunda/terrain_dirt.material', 0.5), ('../materials/skrunda/terrain_lush.material', 0.5),
          ('../materials/skrunda/terrain_crack.material', 0.5), (PB + 'rubble.material', 0.5)]
SLAB, ASPH, PAVE, MOSS, SOIL, GRASS, CRACK, RUBBLE = range(8)

# ----------------------------------------------------------------------------------------------------- the houses
# lot houses (zonegen/walkup.py), instanced along the block faces; a lot never gets the same house as its neighbour.
# name -> walkup() arguments. 'd' houses are 18 m deep (long faces), 's' houses 14 m (short faces, outer rows).
LOTS = {}


def _lot(name, w, d, floors, skin, cornice, lintel, seed, **kw):
    LOTS[name] = dict(width=w, depth=d, floors=floors, skin=skin, cornice=cornice, lintel=lintel, seed=seed, **kw)


D, S = LY.DEEP, LY.SHALLOW
_lot('lot_d76a', 7.6, D, 5, 'st_brick_red', 'metal', 'flat', 101, drop=True)
_lot('lot_d76b', 7.6, D, 6, 'st_brick_buff', 'metal', 'hood', 102, tank=True)
_lot('lot_d76c', 7.6, D, 4, 'st_brick_dark', 'corbel', 'arch', 103)
_lot('lot_d76d', 7.6, D, 5, 'st_brick_worn', 'metal', 'arch', 104)
_lot('lot_d76e', 7.6, D, 6, 'st_brick_patch', 'metal', 'flat', 105, drop=True)
_lot('lot_d61a', 6.1, D, 4, 'st_brick_slumped', 'corbel', 'flat', 106)
_lot('lot_d61b', 6.1, D, 5, 'st_brick_red', 'metal', 'hood', 107)
_lot('lot_d91a', 9.1, D, 6, 'st_brick_buff', 'metal', 'flat', 108, drop=True)
_lot('lot_d91b', 9.1, D, 5, 'st_brick_dark', 'metal', 'hood', 109)
_lot('lot_d152a', 15.2, D, 6, 'st_brick_worn', 'metal', 'hood', 110, tank=True)
_lot('lot_d152b', 15.2, D, 7, 'st_brick_red', 'metal', 'flat', 111, tank=True, drop=True)
_lot('lot_d76cl', 7.6, D, 6, 'st_brick_buff', 'metal', 'hood', 112, corner='-x')
_lot('lot_d76cr', 7.6, D, 6, 'st_brick_red', 'metal', 'flat', 113, corner='+x')
_lot('lot_d91cl', 9.1, D, 7, 'st_brick_dark', 'metal', 'flat', 114, corner='-x', tank=True)
_lot('lot_d91cr', 9.1, D, 5, 'st_brick_patch', 'metal', 'arch', 115, corner='+x')
_lot('lot_d76ea', 7.6, D, 5, 'st_brick_red', 'metal', 'hood', 116, enter=True, drop=True)
_lot('lot_d76eb', 7.6, D, 4, 'st_brick_buff', 'corbel', 'arch', 117, enter=True)
_lot('lot_d76ec', 7.6, D, 6, 'st_brick_worn', 'metal', 'flat', 118, enter=True, drop=True)
_lot('lot_d76x', 7.6, D, 5, 'st_brick_dark', 'metal', 'flat', 119, state='burnt')
_lot('lot_d91x', 9.1, D, 6, 'st_brick_red', 'corbel', 'hood', 120, state='burnt')
_lot('lot_s76a', 7.6, S, 5, 'st_brick_dark', 'metal', 'flat', 121)
_lot('lot_s76b', 7.6, S, 4, 'st_brick_red', 'corbel', 'arch', 122)
_lot('lot_s76c', 7.6, S, 6, 'st_brick_buff', 'metal', 'hood', 123, drop=True)
_lot('lot_s61a', 6.1, S, 5, 'st_brick_worn', 'metal', 'flat', 124)
_lot('lot_s91a', 9.1, S, 5, 'st_brick_patch', 'metal', 'hood', 125)
_lot('lot_s152a', 15.2, S, 6, 'st_brick_red', 'metal', 'flat', 126, tank=True)
_lot('lot_s76cl', 7.6, S, 5, 'st_brick_worn', 'metal', 'hood', 127, corner='-x')
_lot('lot_s76cr', 7.6, S, 6, 'st_brick_buff', 'metal', 'flat', 128, corner='+x')
_lot('lot_s76e', 7.6, S, 5, 'st_brick_red', 'metal', 'arch', 129, enter=True, drop=True)
# gutted loft buildings: concrete frames (zonegen/skeleton.py, martin's references), on two corners
SKELETONS = {'skeleton_loft_a': (15.2, D, 6, 31), 'skeleton_loft_b': (15.2, D, 7, 32)}
# gutted high-rises of the city beyond the old blocks, only seen from afar (the 17:14 reference)
TOWERS = {'skeleton_tower_a': (30.0, 24.0, 22, 41), 'skeleton_tower_b': (24.0, 20.0, 16, 42), 'skeleton_tower_c': (36.0, 22.0, 27, 43)}
SPOTS = os.path.join(HERE, 'overgrown_street_spots.json')               # seed spots and ladders, from --models


def build_models():
    from zonegen import mats, glb, walkup, fixtures, skeleton, church
    out = os.path.join(ROOT, 'models', 'zone', 'street')
    os.makedirs(out, exist_ok=True)
    mats.ensure_textures()
    fixtures.make_sign_atlas(force=True)
    spots = {}

    def pts(m):
        return {'spots': [[round(float(v), 2) for v in p[:3]] + [p[3]] for p in m.spots],
                'ladders': [[round(float(v), 3) for v in l] for l in getattr(m, 'ladders', [])]}
    for name, kw in LOTS.items():
        for lod in (0, 1):
            m = walkup.walkup(lod=lod, **kw)
            if lod == 0:
                spots[name] = pts(m)
            glb.write_glb(os.path.join(out, name + ('_lod1' if lod else '') + '.glb'), m, tex_rel_dir='../textures', name=name)
        print(name, m.triangles(), 'tris (lod1)')
    for name, (w, d, fl, seed) in SKELETONS.items():
        for lod in (0, 1):
            m = skeleton.skeleton(w, d, fl, seed=seed, lod=lod)
            if lod == 0:
                spots[name] = pts(m)
            glb.write_glb(os.path.join(out, name + ('_lod1' if lod else '') + '.glb'), m, tex_rel_dir='../textures', name=name)
    for name, (L, d, fl, seed) in TOWERS.items():
        for lod in (0, 1):
            m = skeleton.skeleton(L, d, fl, seed=seed, lod=lod, infill=0.45)
            glb.write_glb(os.path.join(out, name + ('_lod1' if lod else '') + '.glb'), m, tex_rel_dir='../textures', name=name)
    for lod in (0, 1):
        m = church.church(lod=lod)
        glb.write_glb(os.path.join(out, 'church' + ('_lod1' if lod else '') + '.glb'), m, tex_rel_dir='../textures', name='church')
    print('church', m.triangles(), 'tris (lod1)')
    for lod in (0, 1):
        m = walkup.collapse_heap(lod=lod)
        glb.write_glb(os.path.join(out, 'collapse_heap' + ('_lod1' if lod else '') + '.glb'), m, tex_rel_dir='../textures', name='collapse_heap')
    from zonegen import yard
    for name, fn in yard.MODELS.items():
        glb.write_glb(os.path.join(out, name + '.glb'), fn(), tex_rel_dir='../textures', name=name)
    from zonegen import works
    for name, fn in works.MODELS.items():
        for lod in ((0, 1) if name in works.LODS else (0,)):
            m = fn(lod)
            glb.write_glb(os.path.join(out, name + ('_lod1' if lod else '') + '.glb'), m, tex_rel_dir='../textures', name=name)
            if lod == 0 and (getattr(m, 'ladders', None) or getattr(m, 'stash', None)):
                spots[name] = {'spots': [], 'ladders': [list(l[:4]) + [list(l[4])] for l in m.ladders], 'stash': m.stash}
    from zonegen import streetkit
    for name, fn in streetkit.MODELS.items():
        for lod in ((0, 1) if name in streetkit.LODS else (0,)):
            glb.write_glb(os.path.join(out, name + ('_lod1' if lod else '') + '.glb'), fn(lod), tex_rel_dir='../textures', name=name)
    with open(SPOTS, 'w') as f:
        json.dump(spots, f, separators=(',', ':'))
    for s in fixtures.CABLE_SPANS:
        glb.write_glb(os.path.join(out, f'cable_{s}m.glb'), fixtures.cable(s), tex_rel_dir='../textures', name=f'cable_{s}m')
    glb.write_glb(os.path.join(out, 'kerb_6m.glb'), fixtures.kerb(6.0, seed=3), tex_rel_dir='../textures', name='kerb_6m')
    glb.write_glb(os.path.join(out, 'kerb_6m_b.glb'), fixtures.kerb(6.0, seed=8), tex_rel_dir='../textures', name='kerb_6m_b')


# ----------------------------------------------------------------------------------------------------------- level
class Level:
    def __init__(self):
        self.lines, self.groups = [], []
        self.n = {}
        self.ents = []

    def group(self, name, parent=''):
        self.groups.append(f'group "{name}" "{parent}"')

    def model(self, label, path, x, y, z, yaw=0.0, scale=1.0, group=None, lod=None, cull=None, coll=True, shadows=True,
              tilt=(0.0, 0.0), sway=0.0, impostor=0.0):
        k = self.n.get(label, 0) + 1
        self.n[label] = k
        name = label if k == 1 else f'{label} {k}'
        self.lines.append(f'model "{name}" "{path}" {x:.2f} {y:.3f} {z:.2f}')
        if group:
            self.lines.append(f'  parent "{group}"')
        if yaw or scale != 1.0 or tilt != (0.0, 0.0):
            self.lines.append(f'  transform {tilt[0]:g} {yaw:.1f} {tilt[1]:g} {scale:.3g}')
        for lp, ld in ([lod] if lod and isinstance(lod[0], str) else lod or []):   # one (file, m) or a list of them
            self.lines.append(f'  lod "{lp}" {ld:g}')
        if cull:
            self.lines.append(f'  cull_distance {cull:g}')
        if not coll:
            self.lines.append('  collision 0')
        if not shadows:
            self.lines.append('  cast_shadows 0')
        if sway:
            self.lines.append(f'  sway {sway:g}')
        if impostor:
            self.lines.append(f'  impostor {impostor:g}')

    def clip(self, name, x0, z0, x1, z1, y0=-2.0, y1=90.0):
        """An invisible wall (player clip) over the rectangle, from y0 to y1."""
        x0, x1 = sorted((x0, x1))
        z0, z1 = sorted((z0, z1))
        self.lines.append(f'box "{name}" {(x0 + x1) / 2:g} {(y0 + y1) / 2:g} {(z0 + z1) / 2:g} {max(x1 - x0, 0.5):g} '
                          f'{y1 - y0:g} {max(z1 - z0, 0.5):g} 0.7 0.7 0.7')
        self.lines.append('  parent "Map edge"')
        self.lines.append('  clip')


def rot(yaw, lx, lz):
    """Local (x, z) of a model turned by yaw (engine convention: yaw 90 turns local +z to +x) to world offsets."""
    a = math.radians(yaw)
    return (lx * math.cos(a) + lz * math.sin(a), -lx * math.sin(a) + lz * math.cos(a))


# --------------------------------------------------------------------------------------------- lots along the faces
# the plan's set pieces, by face and world position along it: what stands there
SPECIAL = [
    # (x, z on the house fronts, what stands there)
    # the way round the collapse: a house on Mulberry, the yard, a house out to Mott (Mulberry-Mott block, Hester-Grand)
    (8.0, -44.0, 'enter'), (66.0, -44.0, 'enter'),
    # the building site on Mulberry in sight of the start: its frame to climb, the crane over the roofs
    (8.0, 42.0, 'site'),
    # Baxter-Mulberry block: a vacant lot from Mulberry into the yard, a house out to Baxter
    (-8.0, -47.0, 'vacant'), (-66.0, -58.0, 'enter'),
    # the collapse that closes Mulberry between Hester and Grand, a burnt house beside it
    (-8.0, -62.0, 'collapse'), (-8.0, -74.0, 'burnt'),
    # south blocks: into the yards near the start and from Hester
    (8.0, 30.0, 'enter'), (-37.0, -10.0, 'enter'), (-8.0, 12.0, 'enter'), (37.0, 60.0, 'vacant'), (-66.0, 20.0, 'burnt'),
    (66.0, 8.0, 'enter'), (111.0, -10.0, 'enter'), (140.0, 30.0, 'vacant'), (82.0, -50.0, 'burnt'),
    # north blocks (Grand-Broome)
    (8.0, -140.0, 'enter'), (-8.0, -128.0, 'burnt'), (-8.0, -165.0, 'vacant'), (37.0, -110.0, 'enter'), (-66.0, -150.0, 'enter'),
    (82.0, -130.0, 'enter'), (140.0, -160.0, 'burnt'), (111.0, -96.0, 'vacant'),
]
# gutted loft buildings on corners: (face name contains, which end, model)
CORNER_LOFTS = [('Mulberry-Mott / Hester-Grand east', 'start', 'skeleton_loft_a'),
                ('Baxter-Mulberry / Hester-Canal west', 'end', 'skeleton_loft_b'),
                ('Mott-Elizabeth / Broome-Grand west', 'start', 'skeleton_loft_a')]


def face_s(f, x, z):
    """Distance along face f of the world point (x, z)."""
    return (x - f.x0) * f.u[0] + (z - f.z0) * f.u[1]


def fill_face(f, rng):
    """[(s, width, kind)] along the face: corner houses at street corners, the set pieces, ordinary lots between."""
    fixed = []                                        # (s0, s1, kind, width)
    if f.corner_start:
        w = rng.choice((7.6, 9.1))
        fixed.append((0.0, w, 'corner_l'))
    if f.corner_end:
        w = rng.choice((7.6, 9.1))
        fixed.append((f.L - w, f.L, 'corner_r'))
    for loft_face, end, name in CORNER_LOFTS:
        if loft_face == f.name and f.L > 40:
            fixed = [x for x in fixed if x[2] != ('corner_l' if end == 'start' else 'corner_r')]
            fixed.append((0.0, 15.2, 'loft:' + name) if end == 'start' else (f.L - 15.2, f.L, 'loft:' + name))
    for px, pz, kind in SPECIAL:
        if not f.contains(px, pz) or (kind == 'enter' and f.kind != 'inner'):
            continue
        s = face_s(f, px, pz)
        w = 15.2 if kind == 'site' else 9.1 if kind == 'burnt' and rng.random() < 0.5 else 7.6
        s0 = min(max(s - w / 2, 0.0), f.L - w)
        if any(a - 0.01 < s0 + w and s0 < b - 0.01 for a, b, _k in fixed):
            continue
        fixed.append((s0, s0 + w, kind))
    fixed.sort()
    out, s = [], 0.0
    for a, b, kind in fixed + [(f.L, f.L, None)]:
        out += fill_gap(s, a, rng, f.depth)
        if kind:
            out.append((a, b - a, kind))
        s = max(s, b)
    return out


def fill_gap(s0, s1, rng, depth):
    """Ordinary lots from s0 to s1: library widths, the last ones chosen so the row closes (overlap <= 0.5 m)."""
    G = s1 - s0
    if G < 0.5:
        return []
    widths = [6.1, 7.6, 9.1, 15.2]
    best = None
    for _ in range(400):
        seq, tot = [], 0.0
        while tot < G - 0.01:
            r = rng.random()
            w = 7.6 if r < 0.56 else 6.1 if r < 0.7 else 9.1 if r < 0.86 else 15.2
            seq.append(w)
            tot += w
        over = tot - G
        if over <= 0.5 * max(1, len(seq) - 0) and (best is None or over < best[1]):
            best = (seq, over)
            if over < 0.6:
                break
    if best is None:
        best = ([6.1] * max(1, int(math.ceil(G / 6.1))), 6.1 * math.ceil(G / 6.1) - G)
    seq, over = best
    rng.shuffle(seq)
    lap = over / len(seq)
    out, s = [], s0
    for w in seq:
        out.append((s, w, 'plain'))
        s += w - lap
    # (the last house ends at s1 exactly: the overlap is spread over the joints)
    return out


def pick_variant(rng, w, depth, kind, prev):
    pre = 'lot_d' if depth > 16 else 'lot_s'
    tag = {6.1: '61', 7.6: '76', 9.1: '91', 15.2: '152'}[w]
    if kind == 'corner_l':
        pool = [n for n in LOTS if n.startswith(pre + tag) and LOTS[n].get('corner') == '-x']
    elif kind == 'corner_r':
        pool = [n for n in LOTS if n.startswith(pre + tag) and LOTS[n].get('corner') == '+x']
    elif kind == 'enter':
        pool = [n for n in LOTS if n.startswith(pre + '76') and LOTS[n].get('enter')]
    elif kind == 'burnt':
        pool = [n for n in LOTS if n.startswith(pre + tag) and LOTS[n].get('state') == 'burnt']
    else:
        pool = [n for n in LOTS if n.startswith(pre + tag) and not LOTS[n].get('corner') and not LOTS[n].get('enter')
                and not LOTS[n].get('state')]
    if not pool:          # (no such house in the library: the nearest plain one of that depth)
        pool = [n for n in LOTS if n.startswith(pre) and not LOTS[n].get('corner') and not LOTS[n].get('enter')
                and not LOTS[n].get('state') and abs(LOTS[n]['width'] - w) < 1.6] or [pre + '76a']
    choice = [n for n in pool if n != prev] or pool
    return rng.choice(choice)


# ------------------------------------------------------------------------------------------------------- ground
def box_sdf(X, Z, r):
    x0, z0, x1, z1 = r
    cx, cz, hx, hz = (x0 + x1) / 2, (z0 + z1) / 2, abs(x1 - x0) / 2, abs(z1 - z0) / 2
    dx, dz = np.abs(X - cx) - hx, np.abs(Z - cz) - hz
    return np.maximum(dx, dz)


def carriageways():
    out = []
    for name, (x0, z0, x1, z1) in LY.STREETS.items():
        sw = 4.5 if name == 'Canal Street' else SIDEWALK
        if abs(x1 - x0) < abs(z1 - z0):           # north-south street
            out.append((x0 + sw, z0, x1 - sw, z1))
        else:
            out.append((x0, z0 + sw, x1, z1 - sw))
    return out


def road_dist(X, Z):
    """Signed distance to the kerb lines: < 0 on a carriageway."""
    return np.min([box_sdf(X, Z, r) for r in carriageways()], axis=0)


def street_dist(X, Z):
    """Distance from the house fronts into the streets: > 0 in a street."""
    return np.max([-box_sdf(X, Z, r) for r in LY.STREETS.values()], axis=0)


def cracks(X, Z, cell=2.3, seed=5):
    """Distance to the nearest crack (a jittered Voronoi network, slab size about `cell`) and a value per slab."""
    gx, gz = X / cell, Z / cell
    ix, iz = np.floor(gx).astype(np.int64), np.floor(gz).astype(np.int64)
    d1 = np.full(X.shape, 9.0)
    d2 = np.full(X.shape, 9.0)
    cid = np.zeros(X.shape)
    for ox in (-1, 0, 1):
        for oz in (-1, 0, 1):
            cx, cz = ix + ox, iz + oz
            h = (cx * 73856093) ^ (cz * 19349663) ^ seed
            jx = ((h * 2654435761) & 0xffff) / 65535.0
            jz = ((h * 40503) & 0xffff) / 65535.0
            px, pz = cx + 0.1 + 0.8 * jx, cz + 0.1 + 0.8 * jz
            d = np.hypot(gx - px, gz - pz)
            closer = d < d1
            d2 = np.where(closer, d1, np.minimum(d2, d))
            cid = np.where(closer, ((h * 9973) & 0xff) / 255.0, cid)
            d1 = np.where(closer, d, d1)
    return (d2 - d1) * 0.5 * cell, cid


# foliage types: key -> (model, brush density, min scale, max scale, cull m, collision, shadows, far density, lods, impostor m)
def _f(path, dens=0.1, lo=0.85, hi=1.15, cull=50, coll=0, shadows=0, far=0.6, lods=(), imp=0):
    return (path, dens, lo, hi, cull, coll, shadows, far, list(lods), imp)


FOLIAGE = {
    'grass_patch': _f(V + 'grass_fresh_street.glb', 0.25, cull=55, far=0.5, lods=[(V + 'grass_fresh_street_lod1.glb', 4), (V + 'grass_fresh_street_far.glb', 8), (V + 'grass_fresh_street_far2.glb', 18)]),
    'grass_tall_a': _f(V + 'grass_tall_a.glb', lods=[(V + 'grass_tall_a_lod1.glb', 8)], cull=48),
    'grass_tall_b': _f(V + 'grass_tall_b.glb', lods=[(V + 'grass_tall_b_lod1.glb', 8)], cull=48),
    'grass_clump': _f(V + 'grass_clump_a.glb', lods=[(V + 'grass_clump_a_lod1.glb', 3.5), (V + 'grass_clump_a_far.glb', 8), (V + 'grass_clump_a_far2.glb', 15)], cull=34),
    'grass_short': _f(V + 'grass_short_a.glb', lods=[(V + 'grass_short_a_lod1.glb', 4), (V + 'grass_short_a_far.glb', 9)], cull=28),
    'weed_a': _f(V + 'weed_a.glb', lods=[(V + 'weed_a_lod1.glb', 3.5), (V + 'weed_a_far.glb', 8)], cull=32),
    'weed_b': _f(V + 'weed_b.glb', lods=[(V + 'weed_b_lod1.glb', 3.5), (V + 'weed_b_far.glb', 8)], cull=32),
    'nettle': _f(V + 'nettle_tall_a.glb', lods=[(V + 'nettle_tall_a_lod1.glb', 4.5), (V + 'nettle_tall_a_far.glb', 10)], cull=32),
    'dandelion_a': _f(V + 'dandelion_a.glb', lods=[(V + 'dandelion_a_lod1.glb', 4), (V + 'dandelion_a_far2.glb', 9)], cull=20),
    'celandine_b': _f(V + 'celandine_b.glb', lods=[(V + 'celandine_b_lod1.glb', 4), (V + 'celandine_b_far.glb', 9), (V + 'celandine_b_far2.glb', 14)], cull=20),
    'shrub_a': _f(V + 'bush_street_b.glb', 0.03, cull=110, shadows=1, far=0.8, lods=[(V + 'bush_street_b_lod1.glb', 9), (V + 'bush_street_b_lod2.glb', 22)]),
    'shrub_b': _f(V + 'bush_street_a.glb', 0.03, cull=110, shadows=1, far=0.8, lods=[(V + 'bush_street_a_lod1.glb', 9), (V + 'bush_street_a_lod2.glb', 22)]),
    'bush_dense': _f(V + 'bush_street_c.glb', 0.02, cull=110, shadows=1, far=0.8, lods=[(V + 'bush_street_c_lod1.glb', 9), (V + 'bush_street_c_lod2.glb', 22)]),
    'sapling_a': _f(V + 'sapling_a.glb', 0.03, cull=150, shadows=1, far=1.0, lods=[(V + 'sapling_a_lod1.glb', 12), (V + 'sapling_a_lod2.glb', 30)]),
    'sapling_b': _f(V + 'sapling_b.glb', 0.03, cull=150, shadows=1, far=1.0, lods=[(V + 'sapling_b_lod1.glb', 12), (V + 'sapling_b_lod2.glb', 30)]),
    'sapling_c': _f(V + 'sapling_c.glb', 0.03, cull=150, shadows=1, far=1.0, lods=[(V + 'sapling_c_lod1.glb', 12), (V + 'sapling_c_lod2.glb', 30)]),
    'birch_young': _f(V + 'birch_young.glb', 0.03, cull=150, shadows=1, far=1.0, lods=[(V + 'birch_young_lod1.glb', 12), (V + 'birch_young_lod2.glb', 30)]),
    'broadleaf_young': _f(V + 'broadleaf_young.glb', 0.03, cull=300, coll=1, shadows=1, far=1.0, lods=[(V + 'broadleaf_young_lod1.glb', 12), (V + 'broadleaf_young_lod2.glb', 30)]),
    'broadleaf_a': _f(V + 'broadleaf_a.glb', 0.03, cull=400, coll=1, shadows=1, far=1.0, lods=[(V + 'broadleaf_a_lod1.glb', 18), (V + 'broadleaf_a_lod2.glb', 45)], imp=90),
    'broadleaf_b': _f(V + 'broadleaf_b.glb', 0.03, cull=400, coll=1, shadows=1, far=1.0, lods=[(V + 'broadleaf_b_lod1.glb', 18), (V + 'broadleaf_b_lod2.glb', 45)], imp=90),
    'maple_a': _f(V + 'maple_a.glb', 0.03, cull=400, coll=1, shadows=1, far=1.0, lods=[(V + 'maple_a_lod1.glb', 18), (V + 'maple_a_lod2.glb', 45)], imp=90),
    'maple_b': _f(V + 'maple_b.glb', 0.03, cull=400, coll=1, shadows=1, far=1.0, lods=[(V + 'maple_b_lod1.glb', 18), (V + 'maple_b_lod2.glb', 45)], imp=90),
    'ash_a': _f(V + 'ash_a.glb', 0.03, cull=400, coll=1, shadows=1, far=1.0, lods=[(V + 'ash_a_lod1.glb', 18), (V + 'ash_a_lod2.glb', 45)], imp=90),
    'ash_b': _f(V + 'ash_b.glb', 0.03, cull=400, coll=1, shadows=1, far=1.0, lods=[(V + 'ash_b_lod1.glb', 18), (V + 'ash_b_lod2.glb', 45)], imp=90),
    'birch_a': _f(V + 'birch_a.glb', 0.03, cull=400, coll=1, shadows=1, far=1.0, lods=[(V + 'birch_a_lod1.glb', 18), (V + 'birch_a_lod2.glb', 45)], imp=90),
}
DEBRIS = {'concrete_chunks': 60, 'plaster_chunks': 50, 'silicate_bricks': 50, 'glass_shards': 40, 'tin_cans': 40, 'bottles': 40,
          'twigs': 45, 'planks': 90, 'sheet': 90, 'brick_heap': 110}
for _k, _c in DEBRIS.items():
    FOLIAGE['deb_' + _k] = _f(SK + f'debris_{_k}.glb', 0.02, cull=_c, far=0.7, shadows=1 if _k in ('brick_heap', 'sheet', 'planks') else 0)
TREES_BIG = ('maple_a', 'maple_b', 'ash_a', 'ash_b', 'birch_a', 'broadleaf_a', 'broadleaf_b')
TREES_YOUNG = ('sapling_a', 'sapling_b', 'sapling_c', 'birch_young', 'broadleaf_young')


def build():
    rng = random.Random(1957)
    nz = Noise(77)
    lv = Level()
    for g, p in (('Houses', ''), ('Street', ''), ('Kerbs', 'Street'), ('Lamps and cables', 'Street'), ('Wrecks', 'Street'),
                 ('Street furniture', 'Street'), ('Barricades', 'Street'), ('Yards', ''), ('Ivy', 'Houses'),
                 ('Camp', 'Street'), ('Zone', 'Street'), ('Story', ''), ('Far', ''), ('Map edge', ''), ('Ladders', 'Houses'),
                 ('Road works', 'Street'), ('Stalkers', 'Story'), ('Bandits', 'Story'), ('A-life', 'Story')):
        lv.group(g, p)
    spots = json.load(open(SPOTS)) if os.path.exists(SPOTS) else {}

    # ------------------------------------------------------------------ houses on their lots
    lots = []                   # dict per lot
    for f in LY.faces():
        frng = random.Random(zlib.crc32(f.name.encode()) ^ 1957)
        prev = None
        for s, w, kind in fill_face(f, frng):
            cx, cz = f.point(s + w / 2, f.depth / 2)
            lot = dict(face=f, s=s, w=w, kind=kind, cx=cx, cz=cz, yaw=f.yaw, model=None)
            if kind in ('vacant', 'collapse', 'site'):
                pass
            elif kind.startswith('loft:'):
                lot['model'] = kind[5:]
            else:
                lot['model'] = pick_variant(frng, min((6.1, 7.6, 9.1, 15.2), key=lambda v: abs(v - w)), f.depth, kind, prev)
                if f.kind == 'outer' and lot['model'] in LOTS and LOTS[lot['model']].get('enter'):
                    lot['model'] = pick_variant(frng, 7.6, f.depth, 'plain', prev)
            prev = lot['model']
            lots.append(lot)
    for lot in lots:
        if not lot['model']:
            continue
        f = lot['face']
        name = lot['model']
        label = 'Loft' if name.startswith('skeleton') else ('House' if f.kind == 'inner' else 'Closed house')
        lv.model(label, ST + name + '.glb', lot['cx'], WALK, lot['cz'], lot['yaw'], group='Houses',
                 lod=(ST + name + '_lod1.glb', 55), cull=700)
        # ladders: let-down fire escape ladders and the goosenecks to the roofs (not on the closed rows: the edge)
        if f.kind == 'inner':
            for (lx, ly, lzz, h, out) in spots.get(name, {}).get('ladders', []):
                dx, dz = rot(lot['yaw'], lx, lzz)
                ix, iz = rot(lot['yaw'], 0.0, -out)            # into the wall
                lv.ents.append(f'entity "Fire escape ladder" "ladder" {lot["cx"] + dx:.3f} {WALK + ly:.3f} {lot["cz"] + dz:.3f} '
                               f'{math.degrees(math.atan2(iz, ix)):.1f} 0.5 {h:.2f} 0.06 0.4 0.9 0.5 ""\n  parent "Ladders"\n'
                               '  prop "style" "none"')
    footprints = []             # (x0, z0, x1, z1) of what stands on the lots
    for lot in lots:
        f = lot['face']
        a, b = f.point(lot['s']), f.point(lot['s'] + lot['w'], f.depth)
        footprints.append((min(a[0], b[0]), min(a[1], b[1]), max(a[0], b[0]), max(a[1], b[1]), lot))

    # vacant lots and the collapse: rubble, a torn fence, weeds; the collapse heap fills Mulberry across
    reclaim_pts = []
    for lot in lots:
        f = lot['face']
        if lot['kind'] == 'vacant':
            fx, fz = f.point(lot['s'] + lot['w'] / 2, 0.3)
            lv.model('Rubble', G + 'rubble_b.glb', *f.point(lot['s'] + lot['w'] / 2, 6.0)[:1], -0.3,
                     f.point(lot['s'] + lot['w'] / 2, 6.0)[1], rng.uniform(0, 360), 1.2, group='Yards', cull=200)
            lv.model('Fence', ST + 'fence_lot_hoarding.glb', fx, WALK, fz, f.yaw, group='Yards', cull=150)
            reclaim_pts.append((*f.point(lot['s'] + lot['w'] / 2, 8.0), 6.0))
        elif lot['kind'] == 'site':
            site_lot(lv, rng, lot, spots, reclaim_pts)
        elif lot['kind'] == 'collapse':
            hx_, hz_ = f.point(lot['s'] + lot['w'] / 2, f.depth / 2)
            lv.model('Collapsed house', ST + 'collapse_heap.glb', hx_, 0.0, hz_, f.yaw, group='Barricades',
                     lod=(ST + 'collapse_heap_lod1.glb', 80), cull=500)
            # it is not climbed: the heap is loose and the bus lies on top (a clip over it, behind what you see)
            a, b = f.point(lot['s'] - 2.0, -15.5), f.point(lot['s'] + lot['w'] + 2.0, 2.0)
            lv.clip('Collapse', a[0], a[1], b[0], b[1], 1.2, 40.0)
            mx, mz = f.point(lot['s'] + lot['w'] / 2, -9.0)
            lv.model('Bus', G + 'bus_long_rust.glb', mx, 1.3, mz, f.yaw + 80.0, group='Barricades', cull=400, tilt=(0.0, 14.0))
            reclaim_pts.append((mx, mz, 8.0))

    # ------------------------------------------------------------------ the map edge
    # behind the closed rows (their backs), at the ends of Canal Street (behind the wrecks), round the church
    E = LY
    xw, xe = E.WEST - E.SHALLOW - E.STUB, E.EAST + E.SHALLOW + E.STUB
    lv.clip('Map edge west', xw - 1.0, E.NORTH - 2.0, xw, E.CANAL[0])
    lv.clip('Map edge east', xe, E.NORTH - 2.0, xe + 1.0, E.CANAL[0])
    lv.clip('Map edge north', xw, E.NORTH - E.SHALLOW - 1.0, -E.MUL - E.DEEP, E.NORTH - E.SHALLOW)
    lv.clip('Map edge north', E.MUL + E.DEEP, E.NORTH - E.SHALLOW - 1.0, xe, E.NORTH - E.SHALLOW)
    for sg in (-1, 1):
        lv.clip('Map edge Mulberry north', sg * (E.MUL + E.DEEP), E.CHURCH_Z - 40.0, sg * (E.MUL + E.DEEP + 1.0), E.NORTH - E.SHALLOW)
    lv.clip('Map edge church', -E.MUL - E.DEEP, E.CHURCH_Z - 41.0, E.MUL + E.DEEP, E.CHURCH_Z - 40.0)
    lv.clip('Map edge south', E.CANAL_X[0], E.SOUTH + E.SHALLOW, E.CANAL_X[1], E.SOUTH + E.SHALLOW + 1.0)
    for x in E.CANAL_X:
        lv.clip('Map edge Canal end', x - 0.5, E.CANAL[0], x + 0.5, E.SOUTH)

    # ------------------------------------------------------------------ the church at the north end (the weenie)
    lv.model('Church', ST + 'church.glb', 0.0, WALK, E.CHURCH_Z - 17.5, 0.0, group='Houses', lod=(ST + 'church_lod1.glb', 120), cull=1500)

    # ------------------------------------------------------------------ the city beyond: gutted towers in the haze
    for k, (bx, bz, yaw) in enumerate(((-60, -330, 8), (70, -380, -6), (190, -250, 85), (-200, -300, 30), (15, -470, 2),
                                       (-210, -60, 90), (215, 10, 90), (-190, 170, 92), (200, 200, 88), (-20, 260, 180),
                                       (80, 300, 175), (-130, 280, 120))):
        name = ('skeleton_tower_a', 'skeleton_tower_b', 'skeleton_tower_c')[k % 3]
        lv.model('Gutted tower', ST + name + '.glb', bx, 0.0, bz, yaw, group='Far', lod=(ST + name + '_lod1.glb', 90), cull=1500, coll=False)
    for (tx, tz, t) in ((-40, -520, 'a'), (90, -560, 'c'), (260, -380, 'b'), (-280, -420, 'b'), (300, 120, 'c'), (-300, 60, 'a')):
        lv.model('Tower', G + f'skyline_tower_{t}.glb', tx, -0.2, tz, rng.uniform(0, 90), group='Far', cull=1500, coll=False)

    # ------------------------------------------------------------------ street furniture
    furn = street_furniture(lv, rng, lots, reclaim_pts)

    # ------------------------------------------------------------------ ground: heights and layers
    n = int(round(SIZE / RES)) + 1
    xs = X0 + np.arange(n) * RES
    zs = Z0 + np.arange(n) * RES
    X, Z = np.meshgrid(xs, zs)
    rd = road_dist(X, Z)
    H = WALK * smoothstep(-0.05, 0.25, rd)
    H = H + 0.05 * (nz.fbm(X + 3, Z - 5, 3.0, 2) - 0.5) * smoothstep(0.3, 1.5, rd)      # pavement settled unevenly
    for (x, z, r) in reclaim_pts:
        H = H + 0.12 * smoothstep(r, 0.0, np.hypot(X - x, Z - z))                     # earth blown against
    for (x, z, r, dp) in furn['potholes']:
        H = H - dp * smoothstep(r, r * 0.45, np.hypot(X - x, Z - z)) * (rd < 0)
    for (x, z, name, yaw, L, W, D) in TRENCHES:                                  # the utility cuts: the terrain drops
        a = math.radians(yaw)
        lx = (X - x) * math.cos(a) - (Z - z) * math.sin(a)
        lz = (X - x) * math.sin(a) + (Z - z) * math.cos(a)
        H = np.where((np.abs(lx) < L / 2 - 0.05) & (np.abs(lz) < W / 2 - 0.05), -D - 0.08, H)
    for (x, z, yaw, name) in SUBWAYS:                     # the subway stairs: the ground drops at the grid vertices
        a = math.radians(yaw)                             # inside the walls (zonegen/streetkit.py)
        lx = (X - x) * math.cos(a) - (Z - z) * math.sin(a)
        lz = (X - x) * math.sin(a) + (Z - z) * math.cos(a)
        H = np.where((np.abs(lx) < 1.01) & (lz > -SUBWAY_L - 0.01) & (lz < -0.49), WALK - 3.9, H)
    S = splat(nz, reclaim_pts, furn['potholes'], footprints)
    inst, types = vegetation(nz, rng, S, H, reclaim_pts, furn, footprints, lots)

    # ------------------------------------------------------------------ plants on the houses: roofs, cornices, fire escapes
    irng = random.Random(4242)
    for lot in lots:
        if not lot['model']:
            continue
        for lx, ly, lzz, kind in spots.get(lot['model'], {}).get('spots', []):
            p = {'balcony': 0.25, 'roof': 0.12, 'ledge': 0.7, 'cornice': 0.15}[kind]
            if irng.random() > p:
                continue
            dx, dz = rot(lot['yaw'], lx, lzz)
            r = irng.random()
            if kind == 'balcony':
                key, sc = (('sapling_c', irng.uniform(0.5, 0.8)) if r < 0.5 else ('birch_young', irng.uniform(0.3, 0.5)))
            elif kind == 'cornice':
                key, sc = ('birch_young', irng.uniform(0.25, 0.45)) if r < 0.7 else ('sapling_c', irng.uniform(0.4, 0.7))
            else:
                key, sc = (('birch_young', irng.uniform(0.5, 0.9)) if r < 0.45 else ('sapling_c', irng.uniform(0.7, 1.1))
                           if r < 0.75 else ('sapling_a', irng.uniform(0.5, 0.8)))
            lv.model('Roof plant', V + key + '.glb', lot['cx'] + dx, WALK + ly, lot['cz'] + dz, irng.uniform(0, 360), sc, group='Ivy',
                     lod=[(V + key + '_lod1.glb', 12), (V + key + '_lod2.glb', 30)], cull=120 if kind != 'balcony' else 70,
                     coll=False, shadows=kind != 'balcony', sway=0.7)
    # ivy and vine up the fronts from the pavement
    for lot in lots:
        if not lot['model'] or lot['model'].startswith('skeleton') or irng.random() < 0.45:
            continue
        f = lot['face']
        r = irng.random()
        key = 'ivy_low' if r < 0.5 else 'ivy_a' if r < 0.8 else 'ivy_b' if r < 0.95 else 'ivy_tall'
        x, z = f.point(lot['s'] + irng.uniform(0.2, 0.8) * lot['w'], -0.04)
        lv.model('Ivy', V + key + '.glb', x, WALK, z, f.yaw, irng.uniform(0.85, 1.15), group='Ivy',
                 lod=(V + key + '_lod1.glb', 30), cull=140 if key != 'ivy_low' else 70, coll=False, shadows=key != 'ivy_low', sway=0.03)

    yards(lv, rng, lots, furn)
    story(lv, furn)
    people(lv, lots)

    # ------------------------------------------------------------------ header and entities
    head = ['# FPS engine level', '# Mulberry Street: made by assets/levels/source/make_overgrown_street.py (plan: mulberry_layout.py);',
            '# edit in the editor afterwards (re-running the script rebuilds it).', '# Original CC0 content.',
            'version 5', f'player_start {START[0]:g} 0 {START[1]:g}', 'sun -0.5 -0.62 -0.6']
    env = {'sun_color': '0.82 0.86 0.92', 'sun_intensity': '1.5', 'sky_zenith': '0.085 0.095 0.11', 'sky_horizon': '0.46 0.48 0.48',
           'ground_color': '0.2 0.2 0.18', 'cloud_cover': '0.88', 'volumetric_clouds': '1', 'cloud_base': '600', 'cloud_thickness': '3000',
           'cloud_density': '3', 'atmosphere': '0.6', 'atmosphere_haze': '2', 'sky_occlusion': '0.8', 'ground_haze': '0.01',
           'ground_haze_height': '0.15', 'ground_haze_thickness': '1.6', 'ambient_intensity': '0.95', 'fog_color': '0.25 0.27 0.28',
           'fog_density': '0.007', 'fog_height': '6', 'fog_height_falloff': '0.012', 'fog_sun_scatter': '0.25',
           'exposure': '1.05', 'bloom_intensity': '0.05', 'bloom_threshold': '1.2', 'ssao_radius': '0.8', 'ssao_intensity': '1.4',
           'shadow_distance': '110', 'saturation': '0.8', 'contrast': '1.2', 'tint': '0.96 1 1.02', 'vignette': '0.32',
           'grain': '0.03', 'auto_exposure': '0.5', 'exposure_min_ev': '-0.5', 'exposure_max_ev': '2', 'rain': '0.45', 'storm': '0.25',
           'tonemap': '2', 'puddles': '0.6', 'wind': '0.55'}
    head += [f'env {k} {v}' for k, v in env.items()]
    head += ['loading_title "@level.overgrown_street.title"', 'loading_hint "@level.overgrown_street.hint"',
             'weapon "../weapons/ak74.weapon"', 'weapon "../weapons/service9.weapon"', 'weapon "../weapons/knife.weapon"']
    head.append('ambience "../sounds/ambience/street_storm.ambience"')
    head.append(f'terrain "{NAME}.terrain"')
    head += [f'terrain_layer {i} "{m}" {t:g}' for i, (m, t) in enumerate(LAYERS)]
    for key, _ in sorted(types.items(), key=lambda kv: kv[1]):
        path, dens, lo, hi, cull, coll, shadows, far, lods, imp = FOLIAGE[key]
        head.append(f'foliage_type "{path}" {dens:g} {lo:g} {hi:g} {cull:g} {coll} 0 {shadows} {far:g}')
        for lf, ld in lods:
            head.append(f'foliage_lod "{lf}" {ld:g}')
        if imp:
            head.append(f'foliage_impostor {imp:g}')
    markers = [f'entity "{n}" "map_marker" {(r[0] + r[2]) / 2:g} 0 {(r[1] + r[3]) / 2:g} 0 1 1 1 0.85 0.3 0.2 ""'
               for n, r in LY.STREETS.items()]
    text = '\n'.join(head + lv.groups + lv.lines + markers + lv.ents) + '\n'
    open(os.path.join(LEVELS, NAME + '.level'), 'w', encoding='utf-8').write(text)
    with open(os.path.join(LEVELS, NAME + '.terrain'), 'wb') as f:
        f.write(struct.pack('<IIiifffI', 0x52524554, 2, n, SPLAT_N, X0, Z0, SIZE, len(inst)))
        f.write(np.asarray(H, dtype='<f4').tobytes())
        f.write(S.tobytes())
        for x, y, z, yaw, sc, t in inst:
            f.write(struct.pack('<7fI', x, y, z, yaw, sc, 0.0, 0.0, t))
    plan = [[l['s'], l['w'], {'enter': 'enter', 'burnt': 'burnt', 'vacant': 'vacant', 'collapse': 'vacant'}.get(l['kind'],
            'skeleton' if (l['model'] or '').startswith('skeleton') else ('enter' if l['model'] in LOTS and LOTS[l['model']].get('enter') else None))]
            for l in lots]
    by_face = {}
    for l, p in zip(lots, plan):
        by_face.setdefault(l['face'].name, []).append(p)
    LY.draw_plan(os.path.join(ROOT, '..', 'docs', 'images', 'mulberry_plan.png'), by_face, PLAN_NOTES)
    print(f'{NAME}: {len(lots)} lots, {len(lv.lines)} model lines, terrain {n} x {n}, splat {SPLAT_N}, {len(inst)} plants')


START = (1.5, 78.0)          # on Canal Street, at the mouth of Mulberry, looking north
CAMP = (-20.0, -17.0)        # the stalkers' fire on Hester Street, just off the Mulberry crossing
EDDIE = (2.5, -234.0)        # he lies on the steps of the church
DOGS = (-37.0, -60.0)        # the dog pack's lair: the yard of the west middle block
PLAN_NOTES = [(START[0], START[1], 'start', (60, 170, 60)), (CAMP[0], CAMP[1], 'camp (Eddie)', (230, 150, 40)),
              (0.0, -62.0, 'collapse closes Mulberry', (200, 60, 60)), (EDDIE[0], EDDIE[1] - 20, 'church = goal', (200, 60, 60)),
              (DOGS[0], DOGS[1], 'dogs', (120, 80, 60))]


# utility cuts: (x, z, model, yaw, length, width, depth); yaw 0 runs along X, 90 along -Z (the spoil slope to -Z)
TRENCHES = [
    (1.8, 36.0, 'works_trench_12m', 90.0, 12.0, 1.8, 1.2),       # Mulberry by the start: the street narrowed to one lane
    (55.0, -17.0, 'works_trench_8m', 90.0, 8.0, 1.6, 1.1),       # across Hester by Mott: planks and a plate to cross on
    (30.0, -101.5, 'works_trench_12m', 0.0, 12.0, 1.8, 1.2),     # along Grand
    (146.5, -58.0, 'works_trench_8m', 90.0, 8.0, 1.6, 1.1),      # along Elizabeth
    (-72.5, -140.0, 'works_trench_8m', 90.0, 8.0, 1.6, 1.1),     # along Baxter, north block
]
LOOSE_PLATES = [(-1.5, -30.0, 5.0), (75.5, -140.0, 92.0), (-74.0, 40.0, 0.0), (110.0, -15.5, 88.0), (2.0, -150.0, 3.0),
                (148.0, 20.0, 178.0)]


def road_works(lv, rng, out, occupied):
    """The utility cuts with their plates, planks, spoil, drums, cones and signs; loose plates over holes elsewhere.
    Each cut narrows its street (cover beside it, a lane left free), the Hester one has to be crossed on the planks."""
    out['trenches'] = TRENCHES
    for (x, z, name, yaw, L, W, D) in TRENCHES:
        lv.model('Utility cut', ST + name + '.glb', x, 0.0, z, yaw, group='Road works', lod=(ST + name + '_lod1.glb', 60), cull=220)
        out['clear'].append((x, z, L / 2 + 1.5))
        occupied.append((x, z, L / 2 + 1.0))
        ax, az = rot(yaw, 1.0, 0.0)                    # along the trench (toward the spoil slope)
        cx_, cz_ = rot(yaw, 0.0, 1.0)                  # across it
        # steel plates over the first stretch, planks across the middle, the spoil heap beside it
        for k, t in enumerate((-L / 2 + 0.9, -L / 2 + 2.7)):
            if k == 1 and L < 10:
                continue
            lv.model('Road plate', ST + ('works_plate_b.glb' if W > 1.7 else 'works_plate.glb'), x + ax * t, 0.04, z + az * t,
                     yaw + rng.uniform(-3, 3), group='Road works', cull=120, shadows=False)
        t = rng.uniform(-0.5, 1.0)
        lv.model('Planks', ST + 'works_planks.glb', x + ax * t, 0.04, z + az * t, yaw + rng.uniform(-6, 6),
                 group='Road works', cull=90)
        side = 1 if rng.random() < 0.5 else -1
        sx, sz = x + cx_ * side * (W / 2 + 1.6) + ax * 1.5, z + cz_ * side * (W / 2 + 1.6) + az * 1.5
        lv.model('Spoil', ST + 'works_spoil.glb', sx, 0.0, sz, yaw + rng.uniform(-10, 10), group='Road works',
                 lod=(ST + 'works_spoil_lod1.glb', 50), cull=150)
        # drums at the corners, cones along the open side, the sign at the end you come from
        for (u, v) in ((-L / 2 - 0.8, -W / 2 - 0.7), (-L / 2 - 0.8, W / 2 + 0.7), (L / 2 + 0.8, -W / 2 - 0.7), (L / 2 + 0.8, W / 2 + 0.7)):
            px, pz = rot(yaw, u, v)
            if rng.random() < 0.75:
                dc(lv, 'traffic_drum' if rng.random() < 0.7 else 'traffic_drum_fallen', x + px, z + pz, rng.uniform(0, 360),
                   'Road works', y=0.0, cull=150)
        for u in np.arange(-L / 2 + 1.5, L / 2, 2.5):
            px, pz = rot(yaw, u, -side * (W / 2 + 0.9))
            if rng.random() < 0.6:
                dc(lv, 'traffic_cone' if rng.random() < 0.6 else rng.choice(('traffic_cone_fallen', 'traffic_cone_crushed')),
                   x + px, z + pz, rng.uniform(0, 360), 'Road works', y=0.0, cull=80)
        px, pz = rot(yaw, -L / 2 - 2.2, 0.0)
        dc(lv, 'sign_roadwork' if rng.random() < 0.6 else 'sign_roadwork_fallen', x + px, z + pz, yaw - 90.0 + rng.uniform(-10, 10),
           'Road works', y=0.0, cull=150)
    for (x, z, yaw) in LOOSE_PLATES:
        lv.model('Road plate', ST + 'works_plate_b.glb', x, 0.0, z, yaw, group='Road works', cull=120, shadows=False)
        occupied.append((x, z, 1.8))


def site_lot(lv, rng, lot, spots, reclaim_pts):
    """The building site: the frame and the sheds, the site office on the sidewalk shed, the crane in the yard behind
    with its ladder and the stash in the cab."""
    f = lot['face']
    cx, cz, yaw = lot['cx'], lot['cz'], lot['yaw']
    lv.model('Building site', ST + 'works_site.glb', cx, WALK, cz, yaw, group='Houses', lod=(ST + 'works_site_lod1.glb', 80), cull=700)
    dx, dz = rot(yaw, -3.0, 10.65)
    dc(lv, 'container_blue', cx + dx, cz + dz, yaw + 180.0, 'Houses', y=WALK + 3.25, cull=400)
    dx, dz = rot(yaw, -4.0, -12.0)
    kx, kz = cx + dx, cz + dz
    kyaw = yaw + 65.0
    lv.model('Tower crane', ST + 'works_crane.glb', kx, WALK, kz, kyaw, group='Houses', lod=(ST + 'works_crane_lod1.glb', 120),
             cull=1500)
    info = spots.get('works_crane', {})
    for (lx, ly, lzz, h, out) in info.get('ladders', []):
        ox, oz = rot(kyaw, lx, lzz)
        ix, iz = rot(kyaw, out[0], out[1])
        lv.ents.append(f'entity "Crane ladder" "ladder" {kx + ox:.3f} {WALK + ly:.3f} {kz + oz:.3f} '
                       f'{math.degrees(math.atan2(iz, ix)):.1f} 0.5 {h:.2f} 0.06 0.4 0.9 0.5 ""\n  parent "Ladders"\n'
                       '  prop "style" "none"')
    if info.get('stash'):
        sx_, sy_, sz_ = info['stash']
        ox, oz = rot(kyaw, sx_, sz_)
        lv.ents.append(f'entity "Crane cab stash" "pickup" {kx + ox:.2f} {WALK + sy_ + 0.05:.2f} {kz + oz:.2f} 75 0.285 0.105 0.197 '
                       '0.9 0.8 0.3 ""\n  parent "Story"\n  character "../models/props/pickups/ammo_can_rifle.glb" 1 0 ""\n'
                       '  prop "ammo_type" "5.45x39"\n  prop "amount" "90"\n  prop "item" "ammo"')
        ox, oz = rot(kyaw, sx_ - 0.6, sz_ + 0.3)
        lv.ents.append(f'entity "Crane cab medkit" "pickup" {kx + ox:.2f} {WALK + sy_ + 0.05:.2f} {kz + oz:.2f} 30 0.245 0.1 0.165 '
                       '0.9 0.8 0.3 ""\n  parent "Story"\n  character "../models/props/pickups/medkit_pouch.glb" 1 0 ""\n'
                       '  prop "amount" "1"\n  prop "item" "medkit"')
    reclaim_pts.append((*f.point(lot['s'] + lot['w'] / 2, 6.0), 4.0))


# the subway under Canal Street: two entrances on its pavements, on the 0.5 m terrain grid (x, z of the mouth, yaw
# turning the stairs' local +z, the way up, toward the corner they face)
SUBWAYS = [(-29.0, 62.5, 90.0, 'subway_entrance'), (32.0, 87.5, -90.0, 'subway_entrance_b')]
SUBWAY_L = 7.0
# bus shelters: x, z, yaw (open side toward the kerb)
BUS_SHELTERS = [(-46.0, 86.6, 180.0), (52.0, 63.4, 0.0), (-40.0, -107.6, 0.0)]


def subway_entrances(lv, rng, out, occupied):
    """Stairs down to the station, shut since the city emptied: something left on the landing for whoever goes down."""
    for k, (x, z, yaw, name) in enumerate(SUBWAYS):
        lv.model('Subway entrance', ST + name + '.glb', x, WALK, z, yaw, group='Street furniture',
                 lod=(ST + name + '_lod1.glb', 45), cull=260)
        for t in np.arange(-0.5, -SUBWAY_L - 0.6, -1.0):
            ox, oz = rot(yaw, 0.0, t)
            occupied.append((x + ox, z + oz, 1.7))
            out['clear'].append((x + ox, z + oz, 1.8))
        y = WALK - 3.6
        for j in range(rng.randint(2, 4)):                                  # rubbish bags thrown down the stairs
            ox, oz = rot(yaw, rng.uniform(-0.45, 0.45), rng.uniform(-6.5, -5.6))
            lv.model('Rubbish bag', P + 'trashbag/trashbag.gltf', x + ox, y - 0.03, z + oz, rng.uniform(0, 360),
                     rng.uniform(0.85, 1.2), group='Street furniture', cull=50, coll=False)
        ox, oz = rot(yaw, 0.35, -SUBWAY_L + 0.45)
        if k == 0:
            lv.ents.append(f'entity "Medkit in the subway" "pickup" {x + ox:.2f} {y + 0.05:.2f} {z + oz:.2f} 30 0.245 0.1 0.165 '
                           '0.9 0.8 0.3 ""\n  parent "Story"\n  character "../models/props/pickups/medkit_pouch.glb" 1 0 ""\n'
                           '  prop "amount" "1"\n  prop "item" "medkit"')
        else:
            lv.ents.append(f'entity "Rounds in the subway" "pickup" {x + ox:.2f} {y + 0.05:.2f} {z + oz:.2f} 75 0.285 0.105 0.197 '
                           '0.9 0.8 0.3 ""\n  parent "Story"\n  character "../models/props/pickups/ammo_can_rifle.glb" 1 0 ""\n'
                           '  prop "ammo_type" "5.45x39"\n  prop "amount" "45"\n  prop "item" "ammo"')
    for (x, z, yaw) in BUS_SHELTERS:
        lv.model('Bus shelter', ST + 'bus_shelter.glb', x, WALK, z, yaw, group='Street furniture',
                 lod=(ST + 'bus_shelter_lod1.glb', 40), cull=200)
        occupied.append((x, z, 2.4))


def sidewalk_kit(lv, rng, out, occupied, free, on_road):
    """The small things of a New York pavement: at the corners the blue mailbox, coin newspaper boxes, the red fire
    alarm post, a payphone; along the kerbs parking meters and street trees in iron-guarded pits."""
    def sidewalk(x, z):
        return (not on_road(x, z, 0.3) and LY.inside_map(x, z)
                and street_dist(np.array([x]), np.array([z]))[0] > 0.8)
    corner_kit = (('mailbox', 0.16), ('mailbox_tipped', 0.05), ('news_boxes_3', 0.1), ('news_boxes_4', 0.06),
                  ('fire_alarm_box', 0.13), ('phone_pedestal', 0.08))
    names = {'mailbox': 'Mailbox', 'mailbox_tipped': 'Mailbox', 'news_boxes_3': 'Newspaper boxes',
             'news_boxes_4': 'Newspaper boxes', 'fire_alarm_box': 'Fire alarm box', 'phone_pedestal': 'Payphone'}
    for (cx, cz), (hx, hz) in crossings():
        for sx in (-1, 1):
            for sz in (-1, 1):
                for along_ns in (True, False):
                    r = rng.random()
                    pick = None
                    for name, p in corner_kit:
                        if r < p:
                            pick = name
                            break
                        r -= p
                    if pick is None:
                        continue
                    d, o = rng.uniform(2.6, 6.0), rng.uniform(0.6, 0.8)
                    if along_ns:
                        x, z, yaw = cx + sx * (hx + o), cz + sz * (hz + d), 90.0 * sx
                    else:
                        x, z, yaw = cx + sx * (hx + d), cz + sz * (hz + o), 0.0 if sz > 0 else 180.0
                    rad = 1.1 if pick.startswith('news') else 0.6
                    if not sidewalk(x, z) or not free(x, z, rad):
                        continue
                    lv.model(names[pick], ST + pick + '.glb', x, WALK - 0.01, z, yaw + rng.uniform(-6, 6),
                             group='Street furniture', cull=90, shadows=pick == 'phone_pedestal')
                    occupied.append((x, z, rad))
    # along the kerbs: meters in front of the shops, trees in pits
    meters = (('parking_meter', 0.55), ('parking_meter_twin', 0.2), ('parking_meter_bent', 0.12), ('parking_meter_headless', 0.13))
    pits = []
    for (x0, z0, x1, z1) in carriageways():
        ns = x1 - x0 < z1 - z0
        a, b = (z0, z1) if ns else (x0, x1)
        for side in (0, 1):
            def kerb_pt(t, o):
                if ns:
                    return ((x0 - o, t, -90.0) if side == 0 else (x1 + o, t, 90.0))
                return ((t, z0 - o, 180.0) if side == 0 else (t, z1 + o, 0.0))
            if rng.random() < 0.35:                                          # a run of parking meters
                t = rng.uniform(a + 10, max(a + 11, b - 50))
                for _ in range(rng.randint(4, 8)):
                    x, z, yaw = kerb_pt(t, 0.45)
                    t += 6.4
                    if near_crossing(x, z, ns) or not sidewalk(x, z) or not free(x, z, 0.35):
                        continue
                    r = rng.random()
                    name = next((n for n, p in meters if (r := r - p) < 0), meters[0][0])
                    lv.model('Parking meter', ST + name + '.glb', x, WALK - 0.01, z, yaw + rng.uniform(-8, 8),
                             group='Street furniture', cull=70, shadows=False)
                    occupied.append((x, z, 0.3))
            t = a + rng.uniform(6, 14)
            while t < b - 6:
                x, z, yaw = kerb_pt(t, 1.05)
                t += rng.uniform(9.0, 13.0)
                if rng.random() > 0.32 or near_crossing(x, z, ns) or not sidewalk(x, z) or not free(x, z, 1.2):
                    continue
                r = rng.random()
                kind = 'big' if r < 0.5 else 'young' if r < 0.78 else 'none'
                model = 'tree_pit_bare' if (kind == 'none' and rng.random() < 0.5) else \
                    ('tree_pit_broken' if rng.random() < 0.2 else 'tree_pit')
                lv.model('Tree pit', ST + model + '.glb', x, WALK - 0.02, z, yaw + 90.0, group='Street furniture',
                         cull=80, coll=False, shadows=False)
                occupied.append((x, z, 1.0))
                pits.append((x, z, kind))
    out['tree_pits'] = pits


def street_furniture(lv, rng, lots, reclaim_pts):
    """Kerbs, lamps, cables, parked wrecks, hydrants, rubbish, signals and signs: what a Lower Manhattan street has."""
    out = {'lamps': [], 'wrecks': [], 'potholes': [], 'clear': [(CAMP[0], CAMP[1], 4.0), (0.0, LY.CHURCH_Z + 6.0, 6.5)]}
    cw = carriageways()
    occupied = [(CAMP[0], CAMP[1], 4.0), (START[0], START[1], 4.0)]
    road_works(lv, rng, out, occupied)
    subway_entrances(lv, rng, out, occupied)

    def free(x, z, r):
        return all(math.hypot(x - a, z - b) > r + c for a, b, c in occupied)
    # kerbs along every carriageway edge, cut at the crossings
    def on_road(x, z, m=0.0):
        return any(r[0] - m <= x <= r[2] + m and r[1] - m <= z <= r[3] + m for r in cw)
    for (x0, z0, x1, z1) in cw:
        for side in (0, 1):
            if x1 - x0 < z1 - z0:          # north-south: kerbs along x = x0 and x = x1
                x = x0 if side == 0 else x1
                z = z0
                while z + 6.0 <= z1 + 0.01:
                    cx, cz = x, z + 3.0
                    if not on_road(x + (-0.6 if side == 0 else 0.6), cz):
                        lv.model('Kerb', ST + ('kerb_6m.glb' if rng.random() < 0.5 else 'kerb_6m_b.glb'), cx, 0.0, cz,
                                 -90.0 if side == 1 else 90.0, group='Kerbs', cull=90, coll=False, shadows=False)
                    z += 6.0
            else:
                z = z0 if side == 0 else z1
                x = x0
                while x + 6.0 <= x1 + 0.01:
                    cx, cz = x + 3.0, z
                    if not on_road(cx, z + (-0.6 if side == 0 else 0.6)):
                        lv.model('Kerb', ST + 'kerb_6m.glb', cx, 0.0, cz, 180.0 if side == 0 else 0.0, group='Kerbs', cull=90,
                                 coll=False, shadows=False)
                    x += 6.0
    # lamps: cobra heads on the kerb every ~30 m, alternating sides; cables lamp to lamp and across the streets
    for name, (x0, z0, x1, z1) in LY.STREETS.items():
        ns = abs(x1 - x0) < abs(z1 - z0)
        sw = 4.5 if name == 'Canal Street' else SIDEWALK
        a, b = (z0, z1) if ns else (x0, x1)
        t = a + rng.uniform(6, 14)
        side = 1
        prev = None
        while t < b - 6:
            if ns:
                x, z = ((x1 - sw + 0.6) if side > 0 else (x0 + sw - 0.6)), t
                yaw = 180.0 if side > 0 else 0.0
            else:
                x, z = t, ((z1 - sw + 0.6) if side > 0 else (z0 + sw - 0.6))
                yaw = -90.0 if side > 0 else 90.0
            if on_road(x, z, 0.0) or any(abs(z - c) < 9 and abs(x) < 120 for c in ()) or not free(x, z, 6.0):
                t += 7
                continue
            # not in a crossing: the pavement must have house fronts on both sides of the lamp's position along it
            lean = rng.random() < 0.22
            lv.model('Lamp post', ST + ('lamp_cobra_leaning.glb' if lean else 'lamp_cobra.glb'), x, WALK - 0.02, z,
                     yaw + rng.uniform(-5, 5), group='Lamps and cables', cull=320)
            occupied.append((x, z, 1.0))
            out['lamps'].append((x, z))
            if prev and rng.random() < 0.6:
                cable(lv, prev[0], prev[1], 8.1, x, z, 8.1)
            prev = (x, z)
            t += rng.uniform(26, 34)
            side = -side
        # cables across the street from house to house
        t = a + rng.uniform(5, 15)
        while t < b - 5:
            if rng.random() < 0.55:
                ya, yb = rng.uniform(6.5, 11.0), rng.uniform(6.5, 11.0)
                if ns:
                    cable(lv, x0 + 0.05, t, ya, x1 - 0.05, t + rng.uniform(-4, 4), yb)
                else:
                    cable(lv, t, z0 + 0.05, ya, t + rng.uniform(-4, 4), z1 - 0.05, yb)
            t += rng.uniform(12, 22)
    # parked cars along both kerbs, decades rusted, flat tyres; taxis among them; a few pushed into the road
    cars = ['car_estate_rust', 'car_burnt', 'car_sedan_green', 'car_taxi_rust', 'car_sedan_cream', 'car_estate_red',
            'car_van_green', 'car_taxi', 'car_sedan_blue', 'car_estate_rust', 'car_taxi_rust']
    for (x0, z0, x1, z1) in cw:
        ns = x1 - x0 < z1 - z0
        for side in (0, 1):
            a, b = (z0, z1) if ns else (x0, x1)
            t = a + rng.uniform(3, 10)
            while t < b - 3:
                if rng.random() < 0.45:
                    off = 1.15 + rng.uniform(-0.15, 0.25)
                    if ns:
                        x, z = (x0 + off) if side == 0 else (x1 - off), t
                        yaw = (0.0 if rng.random() < 0.85 else 180.0) + rng.uniform(-7, 7)
                    else:
                        x, z = t, (z0 + off) if side == 0 else (z1 - off)
                        yaw = 90.0 + (0.0 if rng.random() < 0.85 else 180.0) + rng.uniform(-7, 7)
                    if sum(on_road(x + dx, z + dz) for dx, dz in ((3, 0), (-3, 0), (0, 3), (0, -3))) == 4 and False:
                        pass
                    if free(x, z, 2.6) and not near_crossing(x, z, ns):
                        c = rng.choice(cars)
                        lv.model('Parked wreck', G + c + '.glb', x, -0.05, z, yaw, group='Wrecks', cull=260,
                                 tilt=(rng.uniform(-2, 2), rng.uniform(-3, 3)))
                        occupied.append((x, z, 2.4))
                        out['wrecks'].append((x, z, yaw))
                        reclaim_pts.append((x, z, 3.0))
                    t += rng.uniform(5.2, 6.5)
                else:
                    t += rng.uniform(4, 12)
    # hydrants near the corners, rubbish bags heaped at the kerb, wire baskets at the corners
    for (x0, z0, x1, z1) in cw:
        ns = x1 - x0 < z1 - z0
        a, b = (z0, z1) if ns else (x0, x1)
        for t in np.arange(a + 9, b - 9, 55.0):
            side = rng.choice((0, 1))
            x, z = ((x0 - 0.5) if side == 0 else (x1 + 0.5), t) if ns else (t, (z0 - 0.5) if side == 0 else (z1 + 0.5))
            if free(x, z, 0.8) and not on_road(x, z):
                lv.model('Hydrant', G + rng.choice(('hydrant_a.glb', 'hydrant_b.glb')), x, WALK, z, rng.uniform(0, 360),
                         group='Street furniture', cull=120)
                occupied.append((x, z, 0.6))
        for _ in range(int((b - a) / 14)):
            t = rng.uniform(a + 4, b - 4)
            side = rng.choice((0, 1))
            x, z = ((x0 - 0.7) if side == 0 else (x1 + 0.7), t) if ns else (t, (z0 - 0.7) if side == 0 else (z1 + 0.7))
            if on_road(x, z) or not free(x, z, 1.0):
                continue
            for k in range(rng.randint(3, 9)):                       # a heap of black bags, split and faded
                lv.model('Rubbish bag', P + 'trashbag/trashbag.gltf', x + rng.uniform(-0.8, 0.8), WALK - 0.03,
                         z + rng.uniform(-0.8, 0.8), rng.uniform(0, 360), rng.uniform(0.85, 1.25), group='Street furniture',
                         cull=70, coll=False, tilt=(rng.uniform(-15, 15), rng.uniform(-15, 15)))
            occupied.append((x, z, 1.2))
            reclaim_pts.append((x, z, 1.5))
    # dumpsters and bins in front of some houses
    junk = ('dumpster_green', 'dumpster_blue_open', 'dumpster_green_tipped', 'wheelie_bin_green', 'wheelie_bin_tipped',
            'drum_rust', 'tyre_pile')
    for lot in lots:
        f = lot['face']
        if f.kind != 'inner' or rng.random() > 0.08 or not lot['model']:
            continue
        x, z = f.point(lot['s'] + lot['w'] / 2, -1.4)
        if free(x, z, 1.5):
            dc(lv, rng.choice(junk), x, z, f.yaw + 90 + rng.uniform(-10, 10), 'Street furniture')
            occupied.append((x, z, 1.3))
    # the crossings: traffic signals on mast arms (one fallen), stop signs, street name signs, cones
    for (cx_, cz_), (hx_, hz_) in crossings():
        k = rng.random()
        dc(lv, 'traffic_signal_mast' if k < 0.5 else 'traffic_signal_mast_sagging', cx_ - hx_ - 0.6, cz_ + hz_ + 0.6, 90.0,
           'Street furniture', cull=300)
        if k < 0.35:
            dc(lv, 'traffic_signal_fallen', cx_ + rng.uniform(-2, 2), cz_ + rng.uniform(-2, 2), rng.uniform(0, 360),
               'Street furniture', y=0.0, cull=200)
        else:
            dc(lv, 'traffic_signal_post_leaning' if rng.random() < 0.4 else 'traffic_signal_mast', cx_ + hx_ + 0.6,
               cz_ - hz_ - 0.6, -90.0, 'Street furniture', cull=300)
        dc(lv, 'sign_street_name', cx_ + hx_ + 0.7, cz_ + hz_ + 0.7, 180.0, 'Street furniture')
        dc(lv, rng.choice(('sign_stop', 'sign_stop_bent', 'sign_oneway', 'sign_oneway_leaning', 'sign_noparking')),
           cx_ - hx_ - 0.7, cz_ - hz_ - 0.7, 0.0, 'Street furniture')
        for _ in range(rng.randint(0, 3)):
            dc(lv, rng.choice(('traffic_cone', 'traffic_cone_fallen', 'traffic_drum', 'traffic_drum_fallen')),
               cx_ + rng.uniform(-hx_, hx_), cz_ + rng.uniform(-hz_, hz_), rng.uniform(0, 360), 'Street furniture', y=0.0)
        occupied.append((cx_, cz_, 3.0))
    sidewalk_kit(lv, rng, out, occupied, free, on_road)
    # potholes, more where the street is narrow and old
    prng = np.random.default_rng(77)
    for (x0, z0, x1, z1) in cw:
        for _ in range(int((x1 - x0) * (z1 - z0) / 70)):
            x, z = prng.uniform(x0 + 0.6, x1 - 0.6), prng.uniform(z0 + 0.6, z1 - 0.6)
            out['potholes'].append((x, z, prng.uniform(0.4, 1.3), prng.uniform(0.06, 0.16)))
    out['occupied'] = occupied
    barricades(lv, rng, reclaim_pts, out)
    return out


def near_crossing(x, z, ns):
    for (cx, cz), (hx, hz) in crossings():
        if abs(x - cx) < hx + 6.0 and abs(z - cz) < hz + 6.0:
            return True
    return False


def crossings():
    """Centres and half sizes of the carriageway crossings."""
    cw = carriageways()
    out = []
    for i, a in enumerate(cw):
        for b in cw[i + 1:]:
            x0, z0, x1, z1 = max(a[0], b[0]), max(a[1], b[1]), min(a[2], b[2]), min(a[3], b[3])
            if x1 > x0 and z1 > z0 and (x1 - x0) < 40 and (z1 - z0) < 40:
                out.append((((x0 + x1) / 2, (z0 + z1) / 2), ((x1 - x0) / 2, (z1 - z0) / 2)))
    return out


def dc(lv, name, x, z, yaw, group, y=None, cull=120, scale=1.0, tilt=(0.0, 0.0)):
    lod = (DC + name + '_lod1.glb', 40) if os.path.exists(os.path.join(ROOT, 'models', 'props', 'deadcity', name + '_lod1.glb')) else None
    lv.model(name.replace('_', ' ').capitalize(), DC + name + '.glb', x, WALK if y is None else y, z, yaw, scale,
             group=group, lod=lod, cull=cull, tilt=tilt)


def cable(lv, xa, za, ya, xb, zb, yb):
    spans = (6, 10, 14, 18, 24, 32)
    L = math.hypot(xb - xa, zb - za)
    s = min(spans, key=lambda t: abs(t - L))
    yaw = math.degrees(math.atan2(-(zb - za), xb - xa))       # local +X onto the span
    pitch = math.degrees(math.atan2(yb - ya, L))
    lv.model('Cable', ST + f'cable_{s}m.glb', (xa + xb) / 2, (ya + yb) / 2, (za + zb) / 2, yaw, L / s,
             group='Lamps and cables', cull=160, coll=False, tilt=(0.0, pitch))


def barricades(lv, rng, reclaim_pts, out):
    """The ends of the streets: wrecks pushed together, concrete barriers, rubble. Canal Street at both ends, Hester and
    Grand past Baxter / Mott, Baxter and Mott north of Grand (closed by the houses there)."""
    E = LY
    cz = (E.CANAL[0] + E.CANAL[1]) / 2
    for x_end, sg in ((E.CANAL_X[0], -1), (E.CANAL_X[1], 1)):
        x = x_end - sg * 3.0
        lv.model('Barricade bus', G + rng.choice(('bus_long_red.glb', 'bus_long_rust.glb')), x, -0.05, cz,
                 rng.uniform(-8, 8), group='Barricades', cull=400)
        for dz in (-11.0, 11.0):
            dc(lv, 'jersey_barrier_row', x - sg * 1.0, cz + dz, 90.0 + rng.uniform(-6, 6), 'Barricades', y=0.0)
        for dz in (-7.0, 7.0):
            lv.model('Barricade wreck', G + rng.choice(('car_burnt.glb', 'car_estate_rust.glb', 'car_taxi_rust.glb')),
                     x - sg * 2.5, -0.05, cz + dz, rng.uniform(60, 120), group='Barricades', cull=260)
        reclaim_pts.append((x, cz, 9.0))
    # the cross streets end at a house across them, behind the outer rows: a barrier, a container or a wreck in front
    for nm, c, w in E.EW[:-1]:
        for sg, xe in ((-1, E.WEST - E.STUB), (1, E.EAST + E.STUB)):
            x = xe - sg * 4.0
            k = rng.random()
            if k < 0.4:
                dc(lv, 'jersey_barrier_row', x, c, 90.0 + rng.uniform(-8, 8), 'Barricades', y=0.0)
            elif k < 0.7:
                dc(lv, 'container_rust_open', x, c + rng.uniform(-2, 2), 90.0 + rng.uniform(-10, 10), 'Barricades', y=0.0, cull=300)
            else:
                dc(lv, 'rubble_mound_large', x, c, rng.uniform(0, 360), 'Barricades', y=0.0, scale=1.4, cull=300)
            reclaim_pts.append((x, c, 5.0))


def splat(nz, reclaim_pts, potholes, footprints):
    s = (np.arange(SPLAT_N) + 0.5) / SPLAT_N * SIZE
    X, Z = np.meshgrid(X0 + s, Z0 + s)
    rd = road_dist(X, Z)
    sd = street_dist(X, Z)
    R = reclaim(nz, X, Z, rd, sd, reclaim_pts)
    W = np.zeros(X.shape + (8,), np.float32)
    road = rd < 0
    instreet = sd > 0
    prng = np.random.default_rng(31)
    patch = np.zeros(X.shape, bool)
    for (x0, z0, x1, z1) in carriageways():                 # utility-cut patches, darker asphalt
        for _ in range(int((x1 - x0) * (z1 - z0) / 120)):
            cx, cz = prng.uniform(x0, x1), prng.uniform(z0, z1)
            patch |= (np.abs(X - cx) < prng.uniform(0.5, 1.4)) & (np.abs(Z - cz) < prng.uniform(0.8, 2.6))
    W[..., SLAB] = np.where(road, patch, 0.0)
    W[..., ASPH] = np.where(road, ~patch, 0.0)
    W[..., PAVE] = np.where(road, 0.0, 1.0)
    ce, cid = cracks(X, Z, cell=3.4, seed=9)

    def put(layer, cover):
        cover = np.clip(cover, 0.0, 1.0).astype(np.float32)
        W[...] *= (1.0 - cover)[..., None]
        W[..., layer] += cover
    cw_ = 0.03 + 0.25 * R ** 1.4
    joint = road * 1.0
    put(MOSS, smoothstep(cw_ + 0.3, cw_, ce) * 0.25 * smoothstep(0.35, 0.8, R) * joint)
    put(CRACK, smoothstep(cw_ * 0.5 + 0.07, cw_ * 0.5 - 0.02, ce) * joint)
    heaved = (R + 0.3 * cid > 0.85) & road
    put(SOIL, heaved * 0.35)
    put(GRASS, (smoothstep(cw_ * 0.7 + 0.04, cw_ * 0.7 - 0.04, ce) * smoothstep(0.55, 0.85, R) * joint + heaved * 0.85 * road))
    for (x, z, r, dp) in potholes:
        d = np.hypot(X - x, Z - z)
        put(RUBBLE, smoothstep(r * 1.15, r * 0.8, d) * road * 0.7)
        put(CRACK, smoothstep(r * 0.85, r * 0.4, d) * road)
        put(GRASS, smoothstep(r * 0.7, r * 0.3, d) * road * smoothstep(0.3, 0.7, R) * 0.8)
    walk = ~road & instreet
    put(MOSS, smoothstep(0.35, 0.8, R + 0.3 * (nz(X + 7, Z, 1.3) - 0.5)) * 0.7 * walk)
    foot = smoothstep(1.4, 0.3, sd + 0.6 * (nz(X, Z + 3, 2.0) - 0.5))
    put(SOIL, foot * walk)
    put(RUBBLE, foot * smoothstep(0.55, 0.7, nz(X - 9, Z + 1, 4.0)) * walk * 0.8)
    put(GRASS, smoothstep(0.45, 0.75, R) * walk * 0.9)
    put(SOIL, smoothstep(-0.6, -0.05, rd) * road * (0.3 + 0.3 * R))
    put(GRASS, smoothstep(-0.7, -0.1, rd) * road * smoothstep(0.3, 0.6, R) * 0.9)
    # the yards: earth, grass gone wild, rubble and bricks where sheds fell, here and there old concrete
    yard = ~instreet
    put(SOIL, yard * 1.0)
    put(GRASS, yard * smoothstep(0.3, 0.6, nz.fbm(X + 70, Z - 30, 5.0, 2)) * 0.95)
    put(RUBBLE, yard * smoothstep(0.62, 0.75, nz(X - 31, Z + 17, 3.5)) * 0.8)
    put(PAVE, yard * smoothstep(0.7, 0.8, nz(X + 11, Z - 47, 6.0)) * 0.9)
    tot = np.maximum(W.sum(-1, keepdims=True), 1e-6)
    return np.clip(np.round(W / tot * 255), 0, 255).astype(np.uint8)


def reclaim(nz, X, Z, rd, sd, reclaim_pts):
    """0 .. 1: how far nature has taken the ground back: along the kerbs and walls, in slow blobs, round the wrecks
    and heaps, less on wide Canal Street (wind and sun keep the asphalt bare), all of it in the yards."""
    kerb = np.exp(-np.abs(rd) / 1.4)
    wall = np.exp(-np.maximum(sd, 0) / 1.0) * (sd > -0.5)
    blob = smoothstep(0.42, 0.68, nz.fbm(X * 1.0 + 300, Z, 16.0, 3))
    fine = nz.fbm(X - 40, Z + 70, 3.0, 2) - 0.5
    near = np.zeros_like(X)
    for (x, z, r) in reclaim_pts:
        near = np.maximum(near, smoothstep(r + 2.0, r * 0.4, np.hypot(X - x, Z - z)))
    canal = (Z > LY.CANAL[0]) & (Z < LY.CANAL[1])
    north = smoothstep(LY.NORTH, LY.CHURCH_Z, Z) * (np.abs(X) < LY.MUL)   # toward the church, wilder
    R = 0.7 * kerb + 0.55 * wall + 0.7 * blob + 0.3 * fine + 0.6 * near + 0.1 - 0.3 * canal + 0.35 * north
    R = np.where(sd > 0, R, 1.0)
    return np.clip(R, 0.0, 1.0)


def vegetation(nz, rng, S, H, reclaim_pts, furn, footprints, lots):
    """Plants from the splat: crack grass and weeds in the joints, tall grass where the asphalt is gone, shrubs and
    young trees (ailanthus, maple, birch) where nature has had longest; the yards a young wood."""
    types, inst = {}, []
    W = S.astype(np.float32) / 255.0
    n = H.shape[0]
    # inside a house: no plants (a raster of the lots, at the splat resolution)
    built = np.zeros((SPLAT_N, SPLAT_N), bool)
    for (x0, z0, x1, z1, lot) in footprints:
        if lot['kind'] in ('vacant',):
            continue
        u0, u1 = int((x0 - X0) / SIZE * SPLAT_N), int(math.ceil((x1 - X0) / SIZE * SPLAT_N))
        v0, v1 = int((z0 - Z0) / SIZE * SPLAT_N), int(math.ceil((z1 - Z0) / SIZE * SPLAT_N))
        built[max(v0, 0):max(v1, 0), max(u0, 0):max(u1, 0)] = True
    # where the player can be: the streets and the yards (a raster, like `built`)
    reach = np.zeros((SPLAT_N, SPLAT_N), bool)
    for (x0, z0, x1, z1) in list(LY.STREETS.values()) + [b[1:] for b in LY.blocks()]:
        u0, u1 = int((x0 - X0) / SIZE * SPLAT_N), int(math.ceil((x1 - X0) / SIZE * SPLAT_N))
        v0, v1 = int((z0 - Z0) / SIZE * SPLAT_N), int(math.ceil((z1 - Z0) / SIZE * SPLAT_N))
        reach[max(v0, 0):max(v1, 0), max(u0, 0):max(u1, 0)] = True

    def cell(x, z):
        return (int(min(max((z - Z0) / SIZE * SPLAT_N, 0), SPLAT_N - 1)), int(min(max((x - X0) / SIZE * SPLAT_N, 0), SPLAT_N - 1)))

    def inside_map(x, z):
        v, u = cell(x, z)
        return reach[v, u]

    def height(x, z):
        fx, fz = (x - X0) / RES, (z - Z0) / RES
        i, j = int(min(max(fx, 0), n - 2)), int(min(max(fz, 0), n - 2))
        tx, tz = min(max(fx - i, 0), 1), min(max(fz - j, 0), 1)
        return float((H[j, i] * (1 - tx) + H[j, i + 1] * tx) * (1 - tz) + (H[j + 1, i] * (1 - tx) + H[j + 1, i + 1] * tx) * tz)

    clearings = furn['clear']

    def put(key, x, z, scale=1.0, sink=0.03):
        if any((x - cx) ** 2 + (z - cz) ** 2 < cr * cr for cx, cz, cr in clearings):
            return
        if key not in types:
            types[key] = len(types)
        inst.append((x, height(x, z) - sink, z, rng.uniform(0, 360), scale, types[key]))
    occupied = list(furn['occupied'])

    def free(x, z, r):
        return all(math.hypot(x - a, z - b) > r + c for a, b, c in occupied)

    def ok(x, z):
        v, u = cell(x, z)
        return inside_map(x, z) and not built[v, u]
    # ground plants on a jittered grid
    step = 0.4
    for gz in np.arange(LY.CHURCH_Z, LY.SOUTH, step):
        for gx in np.arange(LY.CANAL_X[0], LY.CANAL_X[1], step):
            x, z = gx + rng.uniform(-0.2, 0.2), gz + rng.uniform(-0.2, 0.2)
            v, u = cell(x, z)
            w = W[v, u]
            g, s = w[GRASS], w[SOIL]
            p = 1.2 * g + 0.5 * s
            if p < 0.05 or rng.random() > p or not ok(x, z):
                continue
            r = rng.random()
            if g > 0.5 and r < 0.16:
                put('grass_patch', x, z, rng.uniform(0.7, 1.05))
            elif r < 0.38:
                put(rng.choice(('grass_tall_a', 'grass_tall_b')), x, z, rng.uniform(0.8, 1.35))
            elif r < 0.58:
                put('grass_clump', x, z, rng.uniform(0.8, 1.3))
            elif r < 0.7:
                put('grass_short', x, z, rng.uniform(0.9, 1.4))
            elif r < 0.78:
                put(rng.choice(('dandelion_a', 'celandine_b')), x, z, rng.uniform(0.9, 1.3))
            elif r < 0.9:
                put(rng.choice(('weed_a', 'weed_b')), x, z, rng.uniform(0.8, 1.2))
            else:
                put('nettle', x, z, rng.uniform(0.8, 1.2))
    # the street trees in their pits along the kerbs (London planes in the city's lists; here what came up)
    for (x, z, kind) in furn.get('tree_pits', []):
        if kind == 'big':
            put(rng.choice(('broadleaf_a', 'broadleaf_b', 'maple_a', 'ash_a')), x, z, rng.uniform(0.7, 0.9), sink=0.05)
        elif kind == 'young':
            put(rng.choice(TREES_YOUNG), x, z, rng.uniform(0.9, 1.2), sink=0.05)
    # shrubs and young trees where the ground is green; the yards get most of them
    tries = 0
    planted = 0
    while tries < 120000 and planted < 2000:
        tries += 1
        x, z = rng.uniform(*LY.CANAL_X), rng.uniform(LY.CHURCH_Z, LY.SOUTH)
        if not ok(x, z):
            continue
        v, u = cell(x, z)
        w = W[v, u]
        if w[GRASS] + 0.5 * w[SOIL] < 0.55:
            continue
        yard = street_dist(np.array([x]), np.array([z]))[0] <= 0
        r = rng.random()
        if r < (0.55 if yard else 0.2):
            if not free(x, z, 0.8):
                continue
            put(rng.choice(('shrub_a', 'shrub_b', 'bush_dense')), x, z, rng.uniform(0.55, 0.95))
            occupied.append((x, z, 0.6))
        elif r < (0.85 if yard else 0.3):
            if not free(x, z, 2.0):
                continue
            put(rng.choice(TREES_YOUNG), x, z, rng.uniform(0.8, 1.2), sink=0.05)
            occupied.append((x, z, 1.6))
        elif r < (0.95 if yard else 0.34):
            if not free(x, z, 3.0) or (not yard and abs(road_dist(np.array([x]), np.array([z]))[0]) > 1.2 and rng.random() < 0.7):
                continue
            put(rng.choice(TREES_BIG), x, z, rng.uniform(0.75, 1.0), sink=0.05)
            occupied.append((x, z, 2.8))
        else:
            continue
        planted += 1
    # wrecks sunk in the green
    for (wx, wz, yaw) in furn['wrecks']:
        a = math.radians(yaw)
        ax, az = math.sin(a), math.cos(a)
        for _ in range(90):
            uu, vv = rng.uniform(-3.6, 3.6), rng.uniform(-2.4, 2.4)
            o = max(abs(uu) - 2.1, abs(vv) - 0.85, 0.0)
            if abs(uu) < 1.9 and abs(vv) < 0.7 or rng.random() > math.exp(-o / 0.9):
                continue
            x, z = wx + uu * ax + vv * az, wz + uu * az - vv * ax
            if ok(x, z):
                put(rng.choice(('grass_tall_a', 'grass_tall_b', 'nettle', 'grass_clump', 'weed_a')), x, z, rng.uniform(1.0, 1.4))
    # debris under the facades and on the road
    for _ in range(20000):
        x, z = rng.uniform(*LY.CANAL_X), rng.uniform(LY.CHURCH_Z, LY.SOUTH)
        if not ok(x, z) or rng.random() > 0.5:
            continue
        sd = street_dist(np.array([x]), np.array([z]))[0]
        if sd <= 0:
            key = rng.choice(('planks', 'sheet', 'brick_heap', 'tin_cans', 'bottles', 'twigs', 'concrete_chunks'))
        elif sd < 1.6:
            key = rng.choice(('plaster_chunks', 'silicate_bricks', 'brick_heap', 'glass_shards', 'concrete_chunks'))
        else:
            key = rng.choice(('concrete_chunks', 'planks', 'glass_shards', 'tin_cans', 'bottles', 'twigs', 'plaster_chunks'))
        if key == 'brick_heap' and not free(x, z, 0.8):
            continue
        put('deb_' + key, x, z, rng.uniform(0.8, 1.3), sink=0.0)
    return inst, types


def yards(lv, rng, lots, furn):
    """The back yards inside the blocks: the fence along the middle (the rear lot line) and between the lots, most of
    it fallen or gone, clotheslines from the back windows, junk; the way through stays open somewhere."""
    for name, x0, z0, x1, z1 in LY.blocks():
        xm = (x0 + x1) / 2
        za, zb = z0 + LY.SHALLOW, z1 - LY.SHALLOW
        # the way through: behind every house one can walk through (and every burnt or empty lot) the middle fence is down
        ways = [lot['cz'] for lot in lots if lot['kind'] in ('enter', 'burnt', 'vacant', 'site') and lot['face'].kind == 'inner'
                and x0 - 0.5 <= lot['cx'] <= x1 + 0.5 and z0 - 0.5 <= lot['cz'] <= z1 + 0.5]
        z = za
        while z < zb - 1.0:
            r = rng.random()
            L = min(6.0, zb - z)
            if any(z - 4.0 < c < z + L + 4.0 for c in ways):
                z += 6.0
                continue
            if r < 0.45:
                lv.model('Yard fence', ST + 'fence_board_6m.glb', xm, WALK, z + L / 2, 90.0, L / 6.0, group='Yards', cull=120)
            elif r < 0.7:
                lv.model('Yard fence', ST + 'fence_board_6m_broken.glb', xm, WALK, z + L / 2, 90.0 + rng.choice((0, 180)), L / 6.0,
                         group='Yards', cull=120)
            z += 6.0
        # lot lines from the back walls of the deep rows to the middle, and clotheslines over the yard
        for lot in lots:
            f = lot['face']
            if f.kind != 'inner' or f.depth < 17 or not (x0 - 0.5 <= lot['cx'] <= x1 + 0.5 and z0 - 0.5 <= lot['cz'] <= z1 + 0.5):
                continue
            bx, bz = f.point(lot['s'], f.depth)                         # the back corner of the lot
            if za + 0.5 < bz < zb - 0.5 and rng.random() < 0.55:
                span = abs(xm - bx)
                lv.model('Yard fence', ST + ('fence_board_6m.glb' if rng.random() < 0.6 else 'fence_board_6m_broken.glb'),
                         (bx + xm) / 2, WALK, bz, 0.0, span / 6.0, group='Yards', cull=120)
            cx_, cz_ = f.point(lot['s'] + lot['w'] / 2, f.depth)
            if za + 1 < cz_ < zb - 1 and rng.random() < 0.3:
                ex = xm + (xm - cx_) * rng.uniform(-0.2, 0.6)
                span = abs(ex - cx_)
                cable_y = WALK + rng.uniform(5.5, 9.5)
                lv.model('Clothesline', ST + 'clothesline_9m.glb', (cx_ + ex) / 2, cable_y, cz_ + rng.uniform(-1, 1), 0.0,
                         span / 9.0, group='Yards', cull=90, coll=False, shadows=False)
        for _ in range(rng.randint(3, 7)):                             # junk in the yard
            x, z = rng.uniform(x0 + LY.DEEP + 1, x1 - LY.DEEP - 1), rng.uniform(za + 1, zb - 1)
            if any(abs(z - c) < 4.5 for c in ways):
                continue
            dc(lv, rng.choice(('drum_rust', 'drum_cluster', 'tyre_pile', 'tyre_stack', 'wheelie_bin_tipped', 'brick_pile_large',
                               'drum_rust_open', 'concrete_rubble_pile')), x, z, rng.uniform(0, 360), 'Yards', cull=90)
            furn['occupied'].append((x, z, 1.2))


def _human(name, kind, x, y, z, yaw, model, anim, props):
    lines = [f'entity "{name}" "{kind}" {x:.2f} {y:.2f} {z:.2f} {yaw:.0f} 0.6 1.8 0.6 0.55 0.5 0.5 "../scripts/enemy.lua"',
             '  parent "' + ('Stalkers' if kind == 'npc' else 'Bandits') + '"',
             f'  character "../models/characters/humans/{model}.glb" 1 0 "{anim}"', '  animgraph "../animgraphs/human.animgraph"']
    lines += [f'  prop "{k}" "{v}"' for k, v in props.items()]
    return '\n'.join(lines)


def people(lv, lots):
    """Who is still on the street. Sal Moretti trades at the fire on Hester (with Mike on watch); bandits hold the
    building site by the start (one at the gate, one on the sidewalk shed, a rifle on the top of the frame, one inside:
    the trench, the plates and the wrecks are the player's cover on the way up Mulberry) and the corner of Grand and Mott
    (the parcel); a pair roams between the A-life points while the player is elsewhere."""
    E = lv.ents
    cx, cz = CAMP
    E.append(_human('Sal Moretti', 'npc', cx - 3.1, 0.0, cz - 1.6, 27, 'worker', 'Idle_Talking_Loop', {
        'anim_idle': 'Idle_Talking_Loop', 'dialog': 'dialogs/sal.dialog', 'trade': '1', 'markup': '1.3', 'buy_rate': '0.45',
        'repair': '1', 'faction': 'stalker', 'group': 'camp', 'health': '150', 'patrol_radius': '0', 'rest_chance': '0',
        'stock': 'medkit 8; ammo 5.45x39 360; ammo 9x18 200; ammo 9x19 120; weapon weapons/molotov.weapon 2; ammo molotov 6; '
                 'weapon weapons/smg.weapon 1; weapon weapons/ak74_old.weapon 1; armor jacket 1'}))
    E.append(_human('Mike Doyle', 'npc', cx + 3.5, 0.0, cz - 2.5, 270, 'stalker', 'Idle_Loop', {
        'anim_idle': 'Idle_Loop', 'faction': 'stalker', 'group': 'camp', 'health': '140', 'patrol_radius': '1.5', 'rest_chance': '0.3',
        'view_range': '40', 'shoot_range': '28'}))
    # the building site (lot frame -> world)
    site = next((l for l in lots if l['kind'] == 'site'), None)
    if site:
        def w(lx, lz):
            dx, dz = rot(site['yaw'], lx, lz)
            return site['cx'] + dx, site['cz'] + dz
        face = (site['yaw'] - 90.0) % 360            # entity yaw (0 = +X, 90 = +Z) of the lot's street direction
        a = math.radians(site['yaw'])
        face = math.degrees(math.atan2(math.cos(a), math.sin(a)))
        for name, (lx, ly, lz), extra in (
                ('Bandit at the site gate', (2.9, 0.0, 7.4), {'patrol_radius': '2'}),
                ('Bandit on the shed', (3.5, 3.25, 11.0), {'patrol_radius': '0.5', 'view_range': '45', 'shoot_range': '30'}),
                ('Bandit rifle on the frame', (-2.0, 10.32, 5.0), {'patrol_radius': '0.8', 'view_range': '60', 'shoot_range': '50',
                                                                  'accuracy': '0.4', 'shot_damage': '14', 'shot_interval': '2.2',
                                                                  'sound_shot': 'sounds/weapons/rifle_shot.sound'}),
                ('Bandit in the site', (-4.0, 0.12, -2.0), {'patrol_radius': '4'})):
            x, z = w(lx, lz)
            props = {'faction': 'bandit', 'group': 'site', 'health': '100'}
            props.update(extra)
            E.append(_human(name, 'enemy', x, WALK + ly, z, face, 'bandit', 'Idle_Loop', props))
        x, z = w(0.0, 4.0)
        E.append(f'entity "A-life: Building site" "alife_point" {x:.2f} {WALK:g} {z:.2f} 0 1 2.5 1 0.3 0.85 0.9 ""\n'
                 '  parent "A-life"\n  prop "capacity" "2"\n  prop "factions" "bandit"\n  prop "roles" "camp"')
    # Grand and Mott: three round the newsstand, behind the barriers they dragged there
    for name, (x, z, yaw) in (('Bandit by the newsstand', (64.5, -95.0, 200)), ('Bandit on Grand', (80.5, -108.0, 160)),
                              ('Bandit on Mott', (71.0, -114.0, 260))):
        E.append(_human(name, 'enemy', x, WALK if abs(x - 74) > 4 and abs(z + 103) > 3.5 else 0.0, z, yaw, 'bandit', 'Idle_Loop',
                        {'faction': 'bandit', 'group': 'newsstand', 'patrol_radius': '3', 'health': '100'}))
    for (x, z, yaw) in ((66.5, -93.5, 0.0), (79.5, -110.5, 90.0), (69.0, -112.5, 15.0)):
        lv.model('Barrier', P + 'concrete_road_barrier/concrete_road_barrier.gltf', x, 0.0, z, yaw, group='Bandits', cull=150)
    E.append('entity "A-life: Grand and Mott" "alife_point" 74 0.15 -103 0 1 2.5 1 0.3 0.85 0.9 ""\n  parent "A-life"\n'
             '  prop "capacity" "2"\n  prop "factions" "bandit"\n  prop "roles" "camp"')
    # the roaming pair and the points they walk between
    for name, (x, z) in (('A-life: Elizabeth and Hester', (148.0, -17.0)), ('A-life: Broome and Mott', (74.0, -189.0)),
                         ('A-life: Baxter and Grand', (-74.0, -103.0)), ('A-life: Canal and Mott', (74.0, 75.0))):
        E.append(f'entity "{name}" "alife_point" {x:g} 0.15 {z:g} 0 1 2.5 1 0.3 0.85 0.9 ""\n  parent "A-life"\n'
                 '  prop "capacity" "1"\n  prop "roles" "patrol"')
    for k, (dx, dz) in enumerate(((0.0, 0.0), (1.6, 1.2))):
        E.append(_human(f'Bandit roamer {k + 1}', 'enemy', 148.0 + dx, 0.0, -17.0 + dz, 180, 'bandit', 'Idle_Loop',
                        {'faction': 'bandit', 'group': 'roamers', 'alife': 'roam', 'patrol_radius': '2', 'health': '100'}))
    E.append(f'entity "A-life: Stalker camp" "alife_point" {cx:g} 0.15 {cz:g} 0 1 2.5 1 0.3 0.85 0.9 ""\n  parent "A-life"\n'
             '  prop "factions" "stalker"\n  prop "roles" "camp"')


def story(lv, furn):
    """The stalkers' camp on Hester Street, the quest (Eddie), the dog pack in a yard, the Zone's anomalies, crows."""
    cx, cz = CAMP
    furn['clear'].append((cx, cz, 3.6))
    crng = random.Random(1986)

    def prop(label, name, dx, dy, dz, yaw=0.0, scale=1.0, tilt=(0.0, 0.0), coll=True, shadows=True):
        lv.model(label, P + f'{name}/{name}.gltf', cx + dx, dy, cz + dz, yaw, scale, group='Camp', cull=60,
                 coll=coll, shadows=shadows, tilt=tilt)
    prop('Stove', 'barrel_stove', 0.0, 0.0, 0.0, 20)
    prop('Table', 'WoodenTable_01', -1.7, 0.0, 0.9, 75)
    prop('Radio', 'vintage_radio_transceiver', -1.75, 0.66, 0.6, 110, coll=False)
    prop('Lantern', 'Lantern_01', -1.6, 0.55, 1.4, 0, coll=False)
    prop('Pot', 'pot_enamel_01', 0.05, 0.86, 0.0, 40, coll=False)
    for i, (dx, dz, yaw) in enumerate(((-0.9, -1.0, 30), (1.1, -0.6, -40), (0.9, 1.2, 200))):
        prop('Stool', 'folding_wooden_stool' if i != 1 else 'plastic_crate_01', dx, 0.0, dz, yaw + crng.uniform(-15, 15))
    prop('Crate', 'wooden_military_crate', -2.4, 0.0, -0.9, 95)
    prop('Ammo', 'ammo_box', -2.3, 0.4, -1.1, 70, coll=False)
    for k in range(5):
        prop('Food', 'long_life_food' if k % 2 else 'can_rusted', -1.9 + crng.uniform(-0.3, 0.3), 0.55,
             1.05 + crng.uniform(-0.2, 0.2), crng.uniform(0, 360), coll=False, shadows=False)
    prop('Jerrycan', 'metal_jerrycan', 1.6, 0.0, 0.6, 15)
    lv.model('Sandbags', SK + 'sandbags_3m.glb', cx + 0.4, 0.0, cz - 2.6, 0.0, group='Camp', cull=120)
    lv.model('Sandbags', SK + 'sandbags_3m.glb', cx - 2.6, 0.0, cz - 1.2, 70.0, group='Camp', cull=120)
    E = lv.ents
    y = 0.0
    E.append(f'entity "Camp fire" "particle" {cx:.2f} {y + 0.86:.2f} {cz:.2f} 0 0.4 0.4 0.4 1 0.6 0.3 ""\n  parent "Camp"\n'
             '  prop "effect" "particles/fire.particle"')
    E.append(f'entity "Camp fire light" "light" {cx:.2f} {y + 1.5:.2f} {cz:.2f} 0 0.3 0.3 0.3 1 0.55 0.25 ""\n  parent "Camp"\n'
             '  prop "intensity" "16"\n  prop "range" "9"\n  prop "flicker" "fire"\n  prop "shadows" "1"')
    E.append(f'entity "Camp fire sound" "sound" {cx:.2f} {y + 0.8:.2f} {cz:.2f} 0 0.5 0.5 0.5 0.25 0.55 0.95 ""\n  parent "Camp"\n'
             '  prop "sound" "sounds/ambient/campfire.sound"')
    E.append(f'entity "Camp radio" "sound" {cx - 1.75:.2f} {y + 0.8:.2f} {cz + 0.6:.2f} 0 0.3 0.3 0.3 0.25 0.55 0.95 ""\n'
             '  parent "Camp"\n  prop "sound" "sounds/ambient/radio_waltz.sound"\n  prop "volume" "0.6"')
    E.append(f'entity "Camp medkit" "pickup" {cx - 2.6:.2f} {y + 0.4:.2f} {cz - 0.7:.2f} 30 0.245 0.1 0.165 0.9 0.8 0.3 ""\n'
             '  parent "Camp"\n  character "../models/props/pickups/medkit_pouch.glb" 1 0 ""\n  prop "amount" "1"\n  prop "item" "medkit"')
    E.append(f'entity "Camp ammo" "pickup" {cx - 2.1:.2f} {y + 0.4:.2f} {cz - 1.2:.2f} 75 0.285 0.105 0.197 0.9 0.8 0.3 ""\n'
             '  parent "Camp"\n  character "../models/props/pickups/ammo_can_rifle.glb" 1 0 ""\n'
             '  prop "ammo_type" "5.45x39"\n  prop "amount" "60"\n  prop "item" "ammo"')
    # the Zone: an electric field on Canal Street, acid on Hester, radiation in the collapse, a fire vent on Grand
    for name, kind, (ax, ay, az), size, col, extra in (
            ('Discharge on Canal Street', 'discharge', (-30.0, 0.1, 74.0), (3.5, 3.0, 3.5), (0.35, 0.6, 1.0), '\n  prop "artifacts" "1"'),
            ('Acid on Hester Street', 'acid', (40.0, 0.02, -17.0), (6.0, 1.0, 6.0), (0.45, 0.85, 0.2), '\n  prop "artifacts" "1"'),
            ('Radiation in the collapse', 'radiation', (-4.0, 0.0, -62.0), (12.0, 5.0, 10.0), (0.9, 0.85, 0.2), ''),
            ('Vent on Grand Street', 'vent', (36.0, 0.05, -103.0), (2.0, 5.0, 2.0), (1.0, 0.45, 0.15), '')):
        E.append(f'entity "{name}" "anomaly" {ax:g} {ay:g} {az:g} 0 {size[0]:g} {size[1]:g} {size[2]:g} '
                 f'{col[0]:g} {col[1]:g} {col[2]:g} ""\n  parent "Zone"\n  prop "kind" "{kind}"{extra}')
    for name, (fx_, fz_), k in (('Crows on the collapse', (2.0, -58.0), 7), ('Crows by the church', (-3.0, -180.0), 6)):
        E.append(f'entity "{name}" "crow_flock" {fx_:g} 0.2 {fz_:g} 0 0.35 0.3 0.35 0.12 0.12 0.14 "../scripts/crow.lua"\n'
                 f'  parent "Zone"\n  character "../models/characters/animals/crow.glb" 1 0 "Idle_Loop"\n  prop "count" "{k}"')
    # the quest: the note by the fire sends the player north; Eddie lies on the church steps
    E.append(f'entity "Quest start" "empty" {START[0]:g} 0.2 {START[1]:g} 0 0.01 0.01 0.01 0.3 0.85 0.9 "../scripts/quest_start.lua"\n'
             '  parent "Story"\n  prop "message" "@quest.eddie.start"\n  prop "quest" "eddie"')
    E.append(f'entity "Camp" "objective" {cx:.2f} 0.2 {cz:.2f} 0 0.01 0.01 0.01 0.95 0.75 0.2 "../scripts/objective.lua"\n'
             '  parent "Story"\n  prop "message" "@quest.eddie.camp_done"\n  prop "quest" "eddie"\n  prop "radius" "3"\n  prop "stage" "camp"')
    ex, ez = EDDIE
    E.append(f'entity "Eddie" "objective" {ex:g} 0.3 {ez:g} 200 0.01 0.01 0.01 0.95 0.75 0.2 "../scripts/objective.lua"\n'
             '  parent "Story"\n  prop "message" "@quest.eddie.found"\n  prop "quest" "eddie"\n  prop "radius" "2.5"\n  prop "stage" "north"')
    E.append(f'entity "Eddie body" "empty" {ex:g} {WALK + 0.05:g} {ez:g} 200 0.5 1.8 0.5 0.4 0.4 0.4 "../scripts/corpse.lua"\n'
             '  parent "Story"\n  character "../models/characters/humans/stalker.glb" 1 0 "Death01"')
    E.append(f'entity "Eddie rounds" "pickup" {ex + 0.7:g} {WALK + 0.05:g} {ez + 0.8:g} 40 0.285 0.105 0.197 0.9 0.8 0.3 ""\n'
             '  parent "Story"\n  character "../models/props/pickups/ammo_can_rifle.glb" 1 0 ""\n'
             '  prop "ammo_type" "5.45x39"\n  prop "amount" "30"\n  prop "item" "ammo"')
    dx_, dz_ = DOGS
    E.append(f'entity "Dog lair" "alife_point" {dx_:g} 0.15 {dz_:g} 0 1 2.5 1 0.3 0.85 0.9 ""\n  parent "Story"\n'
             '  prop "factions" "mutant"\n  prop "roles" "lair"')
    for k, (ddx, ddz, yaw) in enumerate(((-2.5, 0.0, 0), (0.0, 2.5, 90), (2.5, 0.0, 180))):
        E.append(f'entity "Feral dog {k + 1}" "mutant" {dx_ + ddx:g} 0.15 {dz_ + ddz:g} {yaw} 0.5 1.04 0.5 0.35 0.28 0.22 '
                 '"../scripts/dog.lua"\n  parent "Story"\n  character "../models/characters/animals/wolf.glb" 1 0 "Idle_Loop"\n'
                 '  prop "group" "dog pack"')
    # the second pack in the Mott-Elizabeth yard between Hester and Grand
    for k, (ddx, ddz, yaw) in enumerate(((-2.0, 0.0, 30), (1.5, 2.0, 120), (2.0, -1.5, 250), (0.0, -2.5, 300))):
        E.append(f'entity "Feral dog {k + 4}" "mutant" {111 + ddx:g} 0.15 {-60 + ddz:g} {yaw} 0.5 1.04 0.5 0.35 0.28 0.22 '
                 '"../scripts/dog.lua"\n  parent "Story"\n  character "../models/characters/animals/wolf.glb" 1 0 "Idle_Loop"\n'
                 '  prop "group" "yard pack"')
    # the parcel (from Dead City): a voice on the radio of a wreck by the start sends the player to a newsstand on the
    # corner of Grand and Mott, off the critical path (the loop round the collapse by Mott Street passes it)
    E.append(f'entity "Radio start" "empty" {START[0] + 3.0:g} 0.2 {START[1] - 2.0:g} 0 0.01 0.01 0.01 0.3 0.85 0.9 "../scripts/quest_start.lua"\n'
             '  parent "Story"\n  prop "message" "@quest.parcel.start"\n  prop "quest" "parcel"')
    E.append('entity "Grand and Mott" "objective" 74 0.15 -103 0 0.6 0.6 0.6 0.95 0.75 0.2 "../scripts/objective.lua"\n'
             '  parent "Story"\n  prop "message" "@quest.parcel.corner_done"\n  prop "quest" "parcel"\n  prop "radius" "6"\n  prop "stage" "corner"')
    lv.model('Newsstand', G + 'square_kiosk.glb', 60.8, WALK, -97.4, 180.0, group='Story', cull=200)
    E.append(f'entity "Newsstand stash" "objective" 61.6 {WALK:g} -99.3 186 0.6 0.6 0.6 0.95 0.75 0.2 "../scripts/objective.lua"\n'
             '  parent "Story"\n  character "../models/props/pickups/ammo_can_rifle.glb" 1 0 ""\n  prop "message" "@quest.parcel.found"\n'
             '  prop "quest" "parcel"\n  prop "radius" "1.6"\n  prop "stage" "stash"')
    for name, (px, pz), item in (('Medkit in the yard', (37.0, -52.0), 'medkit'), ('Rounds in the burnt house', (-12.0, -74.0), 'ammo'),
                                 ('Medkit by the church', (-5.5, -228.0), 'medkit')):
        if item == 'medkit':
            E.append(f'entity "{name}" "pickup" {px:g} {WALK + 0.05:g} {pz:g} 30 0.245 0.1 0.165 0.9 0.8 0.3 ""\n  parent "Story"\n'
                     '  character "../models/props/pickups/medkit_pouch.glb" 1 0 ""\n  prop "amount" "1"\n  prop "item" "medkit"')
        else:
            E.append(f'entity "{name}" "pickup" {px:g} {WALK + 0.4:g} {pz:g} 75 0.285 0.105 0.197 0.9 0.8 0.3 ""\n  parent "Story"\n'
                     '  character "../models/props/pickups/ammo_can_rifle.glb" 1 0 ""\n  prop "ammo_type" "5.45x39"\n'
                     '  prop "amount" "30"\n  prop "item" "ammo"')
    for (sx_, sy_, sz_) in ((60.0, 4.0, -70.0), (-60.0, 4.0, 30.0), (30.0, 4.0, -150.0)):
        E.append(f'entity "Far smoke" "particle" {sx_:g} {sy_:g} {sz_:g} 0 1 1 1 0.5 0.5 0.5 ""\n  parent "Camp"\n'
                 '  prop "effect" "particles/smoke_column.particle"')


if __name__ == '__main__':
    if '--models' in sys.argv:
        build_models()
    build()
