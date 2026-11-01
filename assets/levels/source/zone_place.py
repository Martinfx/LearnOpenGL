"""Small hand-designed zone locations on the engine terrain (used by make_test_levels.py).

A Place is a square of terrain (1 m height grid) with gentle hills, flat pads for buildings and yards,
dirt tracks and ditches. Vegetation and junk follow the same rules as Skrunda-1 so that everything
looks like it grew or was dropped there: a closed sward of last year's straw grass on open ground,
lusher green in hollows and by water, nettles and weeds along walls and fences, bushes in clumps at the
edges of clearings and along walls, trees in groves on the hills around (not in the middle of a yard),
no grass on tracks, pads or under buildings. Junk sits where people left it: by doors, walls, fences and
along tracks. Everything is written as ordinary level lines (models, boxes, entities, terrain foliage),
so it stays editable in the editor (Landscape and Foliage modes, Details panel).
"""
import math
import os
import random
import struct
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, '..', '..', '..'))
sys.path.insert(0, os.path.join(ROOT, 'assets', 'models', 'zone', 'source'))
from zonegen.terrain import Noise, smoothstep          # noqa: E402

SK = '../models/zone/skrunda/'
V = '../models/zone/vegetation/'
Z = '../models/zone/'
P = '../models/props/'
C = '../models/props/clutter/'
H = '../models/characters/humans/'

SPRING_ENV = [
    'sun 0.42 -0.68 -0.6', 'env sun_color 1 0.94 0.84', 'env sun_intensity 3.4', 'env sky_zenith 0.28 0.44 0.7',
    'env sky_horizon 0.74 0.8 0.86', 'env ground_color 0.34 0.31 0.24', 'env cloud_cover 0.4', 'env ambient_intensity 1.0',
    'env fog_color 0.72 0.78 0.85', 'env fog_density 0.004', 'env fog_height 2', 'env fog_height_falloff 0.03',
    'env fog_sun_scatter 0.8', 'env exposure 1.0', 'env bloom_intensity 0.04', 'env bloom_threshold 1.4', 'env ssao_radius 0.8',
    'env ssao_intensity 1.2', 'env shadow_distance 90', 'env saturation 0.95', 'env contrast 1.06', 'env tint 1.0 1.0 1.0',
    'env vignette 0.18', 'env grain 0.02']
OVERCAST_ENV = [
    'sun 0.3 -0.8 -0.5', 'env sun_color 0.92 0.92 0.9', 'env sun_intensity 1.3', 'env sky_zenith 0.42 0.47 0.53',
    'env sky_horizon 0.68 0.7 0.72', 'env ground_color 0.3 0.29 0.25', 'env cloud_cover 0.85', 'env ambient_intensity 1.15',
    'env fog_color 0.62 0.65 0.68', 'env fog_density 0.006', 'env fog_height 2', 'env fog_height_falloff 0.04',
    'env fog_sun_scatter 0.3', 'env exposure 1.05', 'env bloom_intensity 0.03', 'env bloom_threshold 1.4', 'env ssao_radius 0.8',
    'env ssao_intensity 1.2', 'env shadow_distance 80', 'env saturation 0.85', 'env contrast 1.05', 'env tint 1.0 1.0 1.0',
    'env vignette 0.2', 'env grain 0.025']
NIGHT_ENV = [
    'sun -0.45 -0.75 -0.3', 'env sun_color 0.6 0.7 1', 'env sun_intensity 0.05', 'env sky_zenith 0.02 0.025 0.04',
    'env sky_horizon 0.05 0.055 0.07', 'env ground_color 0.02 0.02 0.02', 'env cloud_cover 0.8', 'env ambient_intensity 0.18',
    'env fog_color 0.03 0.035 0.045', 'env fog_density 0.01', 'env fog_height 1', 'env fog_height_falloff 0.12',
    'env fog_sun_scatter 0', 'env exposure 1', 'env bloom_intensity 0.08', 'env bloom_threshold 1.2', 'env ssao_radius 0.6',
    'env ssao_intensity 1.1', 'env shadow_distance 60', 'env saturation 0.8', 'env contrast 1.1', 'env tint 1 1 1',
    'env vignette 0.35', 'env grain 0.035']
DUSK_ENV = [
    'sun 0.85 -0.18 -0.3', 'env sun_color 1 0.62 0.38', 'env sun_intensity 1.4', 'env sky_zenith 0.12 0.16 0.3',
    'env sky_horizon 0.78 0.52 0.36', 'env ground_color 0.12 0.1 0.08', 'env cloud_cover 0.5', 'env ambient_intensity 0.45',
    'env fog_color 0.42 0.34 0.3', 'env fog_density 0.006', 'env fog_height 2', 'env fog_height_falloff 0.05',
    'env fog_sun_scatter 1.0', 'env exposure 1.05', 'env bloom_intensity 0.06', 'env bloom_threshold 1.3', 'env ssao_radius 0.7',
    'env ssao_intensity 1.15', 'env shadow_distance 80', 'env saturation 0.9', 'env contrast 1.08', 'env tint 1 1 1',
    'env vignette 0.25', 'env grain 0.03']

# foliage kinds: brush density (instances / m2) and far density, as in Skrunda-1
KIND = {'Tree': (0.03, 1.0), 'Bush': (0.02, 0.8), 'Tall grass': (0.25, 0.5), 'Weeds': (0.1, 0.6), 'Grass tuft': (0.3, 0.5),
        'Reeds': (0.2, 0.6)}
FAR = {SK + 'veg/grass_dry': (SK + 'veg/grass_dry_far.glb', 18), V + 'grass_patch_tall': (SK + 'veg/grass_patch_far.glb', 18),
       V + 'weed_patch': (SK + 'veg/weed_patch_far.glb', 16)}
# (file without .glb, lod distance, cull distance, collision, casts shadows)
TREE = dict(lod=30, cull=200, coll=True, shadow=True)
BUSH = dict(lod=20, cull=110, coll=False, shadow=True)
GRASS = dict(lod=8, cull=55, coll=False, shadow=False)
WEED = dict(lod=8, cull=45, coll=False, shadow=False)


class Place:
    def __init__(self, name, title, hint, seed, size=192, env=SPRING_ENV, start=(0, 0), start_yaw=0):
        self.name, self.title, self.hint = name, title, hint
        self.rng = random.Random(seed)
        self.noise = Noise(seed)
        self.size = size
        self.x0 = self.z0 = -size / 2
        n = size + 1
        xs = np.arange(n, dtype=np.float64) + self.x0
        self.X, self.Z = np.meshgrid(xs, xs)                 # row = z
        self.H = np.zeros((n, n))
        self.env = env
        self.start, self.start_yaw = start, start_yaw
        self.groups, self.lines = [], []
        self.weapons = ['ak74', 'pistol', 'knife']
        self.free_mask = np.ones((n, n), dtype=bool)          # False where nothing may grow (pads, tracks, water)
        self.dirt = np.zeros((n, n))                         # 0..1 bare ground (tracks, trodden yards)
        self.wet = np.zeros((n, n))                          # 0..1 damp ground (ditches, banks)
        self.walls = []                                      # segments ((ax, az), (bx, bz)) where weeds and bushes grow
        self.blocks = []                                     # (x0, z0, x1, z1) footprints: no trees, bushes or junk
        self.foliage_types, self.foliage = {}, []
        self.extra_header = []
        self.used = set()

    # ------------------------------------------------------------------ terrain shaping
    def hills(self, amp=3.0, scale=70.0, rim=6.0, rim_from=55.0):
        """Rolling ground, rising towards the edges so the place sits in a shallow hollow (closes the view)."""
        r = np.hypot(self.X, self.Z)
        self.H += amp * (self.noise.fbm(self.X + 500, self.Z - 300, scale, 3) - 0.5) * 2
        self.H += rim * smoothstep(rim_from, self.size / 2, r) * (0.6 + 0.8 * self.noise(self.X, self.Z, 40.0))

    def _rect_dist(self, x0, z0, x1, z1):
        dx = np.maximum(np.maximum(x0 - self.X, self.X - x1), 0)
        dz = np.maximum(np.maximum(z0 - self.Z, self.Z - z1), 0)
        return np.hypot(dx, dz)

    def pad(self, x0, z0, x1, z1, blend=7.0, height=None, block=False, bare=0.0):
        """Flatten a rectangle (building or yard) to its mean height, blended into the surroundings."""
        d = self._rect_dist(x0, z0, x1, z1)
        inside = d <= 0
        h = float(np.mean(self.H[inside])) if height is None else height
        w = smoothstep(blend, 0.0, d)
        self.H = self.H * (1 - w) + h * w
        if block:
            self.blocks.append((x0, z0, x1, z1))
            self.free_mask &= d > 0.6
        if bare:
            self.dirt = np.maximum(self.dirt, bare * smoothstep(2.5, 0.0, d))
        return h

    def track(self, pts, width=3.0, sink=0.08):
        """Dirt track: ruts without grass, the ground slightly worn down."""
        d = self._poly_dist(pts)
        self.H -= sink * smoothstep(width / 2 + 0.5, 0, d)
        self.dirt = np.maximum(self.dirt, smoothstep(width / 2 + 0.6, width / 2 - 0.6, d +
                                                     0.6 * (self.noise(self.X, self.Z + 9, 2.0) - 0.5)))
        self.free_mask &= d > width / 2 - 0.3

    def trodden(self, x, z, r):
        """Ground worn bare around a campfire or a door: no grass, dirt showing."""
        d = np.hypot(self.X - x, self.Z - z)
        self.dirt = np.maximum(self.dirt, smoothstep(r + 1.0, r * 0.4, d))
        self.free_mask &= d > r * 0.7

    def ditch(self, pts, width, depth, flat=0.0):
        """Trench (canal, drainage ditch). flat: width of a flat bottom."""
        d = self._poly_dist(pts)
        prof = np.where(d < flat / 2, 1.0, smoothstep(width / 2, flat / 2, d))
        self.H -= depth * prof
        self.wet = np.maximum(self.wet, smoothstep(width / 2 + 5, width / 2, d))

    def _poly_dist(self, pts):
        best = np.full(self.X.shape, 1e9)
        for (ax, az), (bx, bz) in zip(pts[:-1], pts[1:]):
            vx, vz = bx - ax, bz - az
            t = np.clip(((self.X - ax) * vx + (self.Z - az) * vz) / (vx * vx + vz * vz), 0, 1)
            best = np.minimum(best, np.hypot(self.X - ax - t * vx, self.Z - az - t * vz))
        return best

    def h(self, x, z):
        fx, fz = x - self.x0, z - self.z0
        i, j = int(math.floor(fx)), int(math.floor(fz))
        i, j = max(0, min(self.size - 1, i)), max(0, min(self.size - 1, j))
        u, v = fx - i, fz - j
        a, b, c, d = self.H[j, i], self.H[j, i + 1], self.H[j + 1, i], self.H[j + 1, i + 1]
        return float((a * (1 - u) + b * u) * (1 - v) + (c * (1 - u) + d * u) * v)

    def _sample(self, A, x, z):
        i = int(round(x - self.x0))
        j = int(round(z - self.z0))
        if 0 <= i <= self.size and 0 <= j <= self.size:
            return A[j, i]
        return 0

    def blocked(self, x, z, r=0.5):
        for (x0, z0, x1, z1) in self.blocks:
            if x0 - r < x < x1 + r and z0 - r < z < z1 + r:
                return True
        return False

    def free(self, x, z, r=0.5):
        return bool(self._sample(self.free_mask, x, z)) and not self.blocked(x, z, r)

    def wall(self, a, b, weeds=('weed_patch', 'nettle_tall_a', 'nettle_medium_a'), every=1.2, bushes=True):
        """A wall, fence or bank edge: weeds grow along it (one per `every` metres), bushes if `bushes`."""
        self.walls.append((a, b, weeds, every, bushes))

    # ------------------------------------------------------------------ level lines
    def group(self, g, parent=''):
        if g not in [x[0] for x in self.groups]:
            self.groups.append((g, parent))

    def uname(self, n):
        base, k = n, 2
        while n in self.used:
            n = f'{base} {k}'
            k += 1
        self.used.add(n)
        return n

    def model(self, name, path, x, y, z, parent, yaw=0.0, lod=None, lod_dist=60, collision=True, cull=0, shadows=True,
              rx=0.0, rz=0.0, scale=1.0, physics=None):
        L = self.lines
        L.append(f'model "{self.uname(name)}" "{path}" {x:.2f} {y:.2f} {z:.2f}')
        L.append(f'  parent "{parent}"')
        if yaw or rx or rz or scale != 1.0:
            L.append(f'  transform {rx:.1f} {yaw:.1f} {rz:.1f} {scale:g}')
        if lod:
            L.append(f'  lod "{lod}" {lod_dist:g}')
        if not collision:
            L.append('  collision 0')
        if cull:
            L.append(f'  cull_distance {cull:g}')
        if not shadows:
            L.append('  cast_shadows 0')
        if physics:
            L.append(f'  physics {physics[0]:g} "{physics[1]}"')

    def zone(self, name, file, x, z, parent, yaw=0.0, dy=0.0, lod=True, **kw):
        """Generated zone model (assets/models/zone/...glb) on the ground."""
        lodf = file[:-4] + '_lod1.glb'
        has = os.path.exists(os.path.join(ROOT, 'assets', 'levels', lodf))
        self.model(name, file, x, self.h(x, z) + dy, z, parent, yaw, lod=lodf if (lod and has) else None, lod_dist=70, **kw)

    def prop(self, name, folder, x, z, parent, yaw=None, y=None, dy=0.0, cull=70, **kw):
        yaw = self.rng.uniform(0, 360) if yaw is None else yaw
        yy = (self.h(x, z) if y is None else y) + dy
        self.model(name, f'{P}{folder}/{folder}.gltf', x, yy, z, parent, yaw, cull=cull, **kw)

    def junk(self, name, file, x, z, parent, yaw=None, y=None, collision=None, cull=45, **kw):
        solid = file.startswith(('brick', 'cinder', 'wooden_pallet', 'rubble', 'planks', 'cardboard_box'))
        coll = solid if collision is None else collision
        yaw = self.rng.uniform(0, 360) if yaw is None else yaw
        yy = (self.h(x, z) - 0.02) if y is None else y
        self.model(name, f'{C}{file}.glb', x, yy, z, parent, yaw, collision=coll, cull=cull, shadows=coll, **kw)

    def box(self, name, x, y, z, sx, sy, sz, parent, material, color=(0.6, 0.6, 0.6), rot=None):
        self.lines.append(f'box "{self.uname(name)}" {x:g} {y:g} {z:g} {sx:g} {sy:g} {sz:g} {color[0]:g} {color[1]:g} {color[2]:g}')
        self.lines.append(f'  parent "{parent}"')
        if rot:
            self.lines.append(f'  rotation {rot[0]:g} {rot[1]:g} {rot[2]:g}')
        self.lines.append(f'  material "../materials/{material}.material"')

    def entity(self, name, etype, x, y, z, parent, yaw=0, size=(0.6, 1.8, 0.6), color=(0.55, 0.5, 0.5), script='',
               props=(), outputs=(), extra=()):
        L = self.lines
        L.append(f'entity "{self.uname(name)}" "{etype}" {x:.2f} {y:.2f} {z:.2f} {yaw:g} {size[0]:g} {size[1]:g} {size[2]:g} '
                 f'{color[0]:g} {color[1]:g} {color[2]:g} "{script}"')
        L.append(f'  parent "{parent}"')
        L.extend(extra)
        for k, v in props:
            L.append(f'  prop "{k}" "{v}"')
        for o in outputs:
            L.append('  output ' + ' '.join(f'"{v}"' if isinstance(v, str) else f'{v:g}' for v in o))

    def person(self, name, etype, x, z, parent, yaw=0, char='bandit', y=None, script='../scripts/enemy.lua', props=(),
               outputs=(), clip='Idle_Loop'):
        yy = self.h(x, z) if y is None else y
        self.entity(name, etype, x, yy, z, parent, yaw, script=script, props=props, outputs=outputs,
                    extra=(f'  character "{H}{char}.glb" 1 0 "{clip}"', '  animgraph "../animgraphs/human.animgraph"'))

    def light(self, name, x, y, z, parent, color, props, yaw=0):
        self.entity(name, 'light', x, y, z, parent, yaw, size=(0.3, 0.3, 0.3), color=color, props=props)

    def interior(self, name, cx, y, cz, sx, sy, sz, parent, yaw=0):
        """Interior volume: darker inside, rain-sheltered, room reverb (engine interior entity)."""
        self.entity(name, 'interior', cx, y, cz, parent, yaw, size=(sx, sy, sz), color=(0.85, 0.55, 0.25),
                    props=(('sky', '0.12'), ('fog', '0.2'), ('fade', '0.3'), ('preset', 'room')))

    # ------------------------------------------------------------------ vegetation
    def veg(self, kind, file, x, z, opts, scale=1.0, dy=-0.03, yaw=None):
        key = (kind, file, opts['lod'], opts['cull'], opts['coll'], opts['shadow'])
        if key not in self.foliage_types:
            self.foliage_types[key] = len(self.foliage_types)
        self.foliage.append((x, self.h(x, z) + dy, z, self.rng.uniform(0, 360) if yaw is None else yaw, scale,
                             self.foliage_types[key]))

    def vegetate(self, core=55.0, grass_step=1.7, trees=1.0, bushes=1.0, groves=0.55):
        """The natural layer: sward, weeds at walls, bushes at edges, groves on the hills."""
        rng, n = self.rng, self.noise
        half = self.size / 2 - 3
        # trees: closed groves (forest masses) where the grove noise is high, young trees and bushes at their
        # edges, only a few lone birches in the open; the middle of the place stays open
        step = 4.2
        z = -half
        while z < half:
            x = -half
            while x < half:
                jx, jz = x + rng.uniform(-1.6, 1.6), z + rng.uniform(-1.6, 1.6)
                r = math.hypot(jx, jz)
                g = float(n.fbm(jx + 1300, jz - 700, 26.0, 2)) + 0.12 * smoothstep(core * 0.8, half, r)
                inner = g > groves + 0.03
                edge = groves - 0.015 < g <= groves + 0.03
                open_ok = r > core * 0.45
                if self.free(jx, jz, 2.5) and open_ok:
                    if inner and rng.random() < 0.75 * trees:
                        conif = smoothstep(groves + 0.03, groves + 0.14, g)
                        if rng.random() < 0.25 + 0.6 * conif:
                            f = rng.choice(['spruce_a', 'spruce_b', 'spruce_a', 'pine_a', 'pine_b'])
                        else:
                            f = rng.choice(['birch_a', 'birch_b', 'birch_a_spring', 'birch_b_spring', 'broadleaf_a',
                                            'broadleaf_b_spring'])
                        self.veg('Tree', SK + 'veg/' + f, jx, jz, TREE, scale=rng.uniform(0.85, 1.2))
                    elif edge and rng.random() < 0.45 * trees:
                        f = rng.choice(['birch_young', 'spruce_young', 'birch_young', 'birch_b_spring'])
                        self.veg('Tree', SK + 'veg/' + f, jx, jz, TREE, scale=rng.uniform(0.7, 1.05))
                    elif not inner and not edge and rng.random() < 0.012 * trees:
                        self.veg('Tree', SK + 'veg/' + rng.choice(['birch_a', 'birch_b_spring']), jx, jz, TREE,
                                 scale=rng.uniform(0.9, 1.15))
                x += step
            z += step
        # bushes: along walls and fences, and in clumps at the edges of groves
        for (ax, az), (bx, bz), _, _, wb in self.walls:
            ln = math.hypot(bx - ax, bz - az)
            for k in range(int(ln / 3.5 * bushes * wb)):
                t = rng.random()
                nx, nz = -(bz - az) / ln, (bx - ax) / ln
                s = rng.choice((-1, 1)) * rng.uniform(1.0, 2.2)
                x, z = ax + (bx - ax) * t + nx * s, az + (bz - az) * t + nz * s
                if rng.random() < 0.55 and self.free(x, z, 0.8):
                    self.veg('Bush', V + rng.choice(['bush_a', 'bush_b', 'bush_c', 'bush_wide_a']), x, z, BUSH,
                             scale=rng.uniform(0.8, 1.2))
        for _ in range(int(900 * bushes)):
            x, z = rng.uniform(-half, half), rng.uniform(-half, half)
            g = float(n.fbm(x + 1300, z - 700, 26.0, 2))
            g += 0.12 * smoothstep(core * 0.8, half, math.hypot(x, z))
            if groves - 0.05 < g < groves + 0.04 and rng.random() < 0.8 and self.free(x, z, 1.0):
                for _ in range(rng.randint(1, 3)):
                    bx_, bz_ = x + rng.uniform(-1.8, 1.8), z + rng.uniform(-1.8, 1.8)
                    if self.free(bx_, bz_, 0.8):
                        self.veg('Bush', V + rng.choice(['bush_a', 'bush_b', 'bush_c', 'bush_dense_a']), bx_, bz_, BUSH,
                                 scale=rng.uniform(0.75, 1.15))
        # weeds and nettles hug the walls
        for (ax, az), (bx, bz), kinds, every, _ in self.walls:
            ln = math.hypot(bx - ax, bz - az)
            for k in range(int(ln / every)):
                t = rng.random()
                nx, nz = -(bz - az) / ln, (bx - ax) / ln
                s = rng.choice((-1, 1)) * rng.uniform(0.35, 1.2)
                x, z = ax + (bx - ax) * t + nx * s, az + (bz - az) * t + nz * s
                if self.free(x, z, 0.2) and rng.random() < 0.7:
                    self.veg('Weeds', V + rng.choice(kinds), x, z, WEED)
        # sward: dense in the place, sparser outside; none on tracks, pads and bare ground
        step = grass_step
        z = -half
        while z < half:
            x = -half
            while x < half:
                jx, jz = x + rng.uniform(-0.8, 0.8) * step / 2, z + rng.uniform(-0.8, 0.8) * step / 2
                r = math.hypot(jx, jz)
                if r > core * 1.5 and rng.random() < 0.35:
                    x += step
                    continue
                bare = float(self._sample(self.dirt, jx, jz))
                if self.free(jx, jz, 0.2) and rng.random() > bare * 0.95:
                    wet = float(self._sample(self.wet, jx, jz))
                    if wet > 0.5 and rng.random() < 0.6:
                        self.veg('Reeds', V + rng.choice(['grass_tall_a', 'grass_tall_b']), jx, jz, GRASS,
                                 scale=rng.uniform(1.1, 1.5))
                    else:
                        f = SK + 'veg/grass_dry' if rng.random() < 0.6 else V + 'grass_patch_tall'
                        self.veg('Tall grass', f, jx, jz, GRASS, scale=rng.uniform(0.8, 1.15))
                    if rng.random() < 0.7:
                        self.veg('Grass tuft', V + rng.choice(['grass_tall_a', 'grass_dry_a', 'grass_short_a']),
                                 jx + step * 0.5, jz + step * 0.5, GRASS, scale=rng.uniform(0.7, 1.0))
                elif bare > 0.2 and self.free(jx, jz, 0.2) and rng.random() < 0.12:
                    self.veg('Grass tuft', V + rng.choice(['grass_clump_a', 'grass_short_a', 'weed_a']), jx, jz, GRASS)
                x += step
            z += step

    def slabs(self, x0, z0, x1, z1, y, parent, plate=(6.0, 2.0), missing=0.04, material='pbr/concrete_floor_02',
              along_x=True, level=()):
        """Yard of Soviet road plates (PAG, 6 x 2 m): slightly sunk and tilted, a few missing, grass in the joints."""
        rng = self.rng
        # ground under the plates: 12 cm lower and bare, so the joints and the missing plates show dirt
        d = self._rect_dist(x0, z0, x1, z1)
        w = smoothstep(1.2, 0.0, d)
        self.H = self.H * (1 - w) + (y - 0.12) * w
        self.dirt = np.maximum(self.dirt, smoothstep(1.5, 0.0, d))
        self.free_mask &= d > 0.4
        pl, pw = plate if along_x else plate[::-1]
        z = z0
        row = 0
        while z < z1 - 0.3:
            x = x0 - (pl / 2 if row % 2 else 0)
            while x < x1 - 0.3:
                a, b = max(x, x0), min(x + pl, x1)
                c, d = z, min(z + pw, z1)
                cx, cz = (a + b) / 2, (c + d) / 2
                flat = any(lx0 < b and a < lx1 and lz0 < d and c < lz1 for lx0, lz0, lx1, lz1 in level)
                if b - a > 0.5 and (flat or rng.random() > missing):
                    dy, tx, tz = rng.uniform(-0.035, 0.01), rng.uniform(-0.8, 0.8), rng.uniform(-0.8, 0.8)
                    if flat:                    # props stand here: plates exactly level (physics tests)
                        dy = tx = tz = 0.0
                    self.box('Road plate', round(cx, 2), round(y - 0.09 + dy, 3), round(cz, 2),
                             round(b - a - 0.06, 2), 0.18, round(d - c - 0.06, 2), parent, material,
                             rot=(round(tx, 2), 0, round(tz, 2)) if not flat else None)
                # grass in the joint along the plate's end and side
                for _ in range(3):
                    self._tuft(b, rng.uniform(c, d), y)
                    self._tuft(rng.uniform(a, b), d, y)
                x += pl
            z += pw
            row += 1

    def _tuft(self, x, z, y):
        if self.blocked(x, z, 0.2) or self.rng.random() < 0.35:
            return
        key = ('Grass tuft', V + self.rng.choice(['grass_clump_a', 'grass_short_a', 'grass_short_b', 'grass_tall_b']),
               WEED['lod'], WEED['cull'], False, False)
        if key not in self.foliage_types:
            self.foliage_types[key] = len(self.foliage_types)
        self.foliage.append((x, y - 0.02, z, self.rng.uniform(0, 360), self.rng.uniform(0.5, 0.9), self.foliage_types[key]))

    def cracks(self, x0, z0, x1, z1, n, y=None):
        """Grass and weeds coming up through the cracks of a concrete yard, thicker towards its edges."""
        rng = self.rng
        for _ in range(n):
            x, z = rng.uniform(x0, x1), rng.uniform(z0, z1)
            edge = min(x - x0, x1 - x, z - z0, z1 - z)
            if self.blocked(x, z, 0.3) or rng.random() > 0.25 + 0.75 * smoothstep(4.0, 0.5, edge):
                continue
            yy = self.h(x, z) if y is None else y
            key = ('Grass tuft', V + rng.choice(['grass_clump_a', 'grass_short_a', 'grass_short_b', 'grass_tall_b']),
                   WEED['lod'], WEED['cull'], False, False)
            if key not in self.foliage_types:
                self.foliage_types[key] = len(self.foliage_types)
            self.foliage.append((x, yy - 0.02, z, rng.uniform(0, 360), rng.uniform(0.6, 1.0), self.foliage_types[key]))

    def foliage_lines(self):
        out = []
        for (kind, file, lod, cull, coll, shadow), _ in sorted(self.foliage_types.items(), key=lambda kv: kv[1]):
            dens, far = KIND.get(kind, (0.1, 1.0))
            out.append(f'foliage_type "{file}.glb" {dens:g} 0.85 1.15 {cull:g} {int(coll)} 0 {int(shadow)} {far:g}')
            out.append(f'foliage_lod "{file}_lod1.glb" {lod:g}')
            if file in FAR:
                out.append(f'foliage_lod "{FAR[file][0]}" {FAR[file][1]:g}')
            if kind == 'Tree':
                out.append('foliage_impostor 90')
        return out

    # ------------------------------------------------------------------ splat and output
    def splat(self, n=512):
        """RGBA8 weights per terrain layer: 0 meadow, 1 straw (dry), 2 lush green, 3 dirt."""
        xs = (np.arange(n) + 0.5) / n * self.size + self.x0
        X, Zg = np.meshgrid(xs, xs)

        def up(A):              # nearest sample of a 1 m field
            i = np.clip(np.round(X - self.x0).astype(int), 0, self.size)
            j = np.clip(np.round(Zg - self.z0).astype(int), 0, self.size)
            return A[j, i]
        nz = self.noise
        dirt = np.clip(up(self.dirt) + 0.25 * (nz(X + 3, Zg, 1.5) - 0.5), 0, 1)
        wet = up(self.wet)
        dry = smoothstep(0.35, 0.6, nz.fbm(X - 400, Zg + 250, 22.0, 2)) * (1 - wet)
        lush = np.clip(wet * 0.9 + 0.35 * smoothstep(0.6, 0.75, nz.fbm(X + 60, Zg - 900, 15.0, 2)), 0, 1)
        meadow = np.ones_like(dry)
        W = np.stack([meadow * 0.6, dry * 1.4 + 0.35, lush, dirt * 2.2], axis=-1)
        W[..., :3] *= (1 - np.clip(dirt * 1.3, 0, 1))[..., None]
        W /= np.maximum(W.sum(axis=-1, keepdims=True), 1e-6)
        return np.clip(W * 255 + 0.5, 0, 255).astype(np.uint8)

    def write(self):
        lvl = os.path.join(ROOT, 'assets', 'levels', self.name + '.level')
        ter = os.path.join(ROOT, 'assets', 'levels', self.name + '.terrain')
        res = self.size + 1
        S = self.splat()
        with open(ter, 'wb') as f:
            f.write(struct.pack('<IIiifffI', 0x52524554, 1, res, S.shape[0], float(self.x0), float(self.z0), float(self.size),
                                len(self.foliage)))
            f.write(np.asarray(self.H, dtype='<f4').tobytes())
            f.write(S.tobytes())
            for x, y, z, yaw, sc, t in self.foliage:
                f.write(struct.pack('<7fI', x, y, z, yaw, sc, 0.0, 0.0, t))
        sx, sz = self.start
        L = ['# FPS engine level',
             f'# {self.title}: generated by assets/levels/source/make_test_levels.py (zone_place.py), then edited in the editor.',
             'version 6', f'player_start {sx:.2f} {self.h(sx, sz) + 0.05:.2f} {sz:.2f}',
             f'map_bounds {self.x0:g} {self.z0:g} {self.size:g}', f'terrain "{self.name}.terrain"',
             'terrain_layer 0 "../materials/skrunda/terrain_meadow.material" 0.4',
             'terrain_layer 1 "../materials/skrunda/terrain_dry.material" 0.5',
             'terrain_layer 2 "../materials/skrunda/terrain_lush.material" 0.5',
             'terrain_layer 3 "../materials/skrunda/terrain_dirt.material" 0.5']
        L += self.foliage_lines()
        L += self.env
        L += ['loading_image "../textures/ui/loading_zone.jpg"', f'loading_title "{self.title}"', f'loading_hint "{self.hint}"']
        L += [f'weapon "../weapons/{w}.weapon"' for w in self.weapons]
        L += self.extra_header
        L += [f'group "{g}" "{p}"' for g, p in self.groups]
        L += self.lines
        L.append('player_sounds "../sounds/player/step_gravel.sound" "../sounds/player/land.sound" "../sounds/player/hurt.sound"')
        with open(lvl, 'w', encoding='utf-8') as f:
            f.write('\n'.join(L) + '\n')
        print(f'wrote {os.path.relpath(lvl, ROOT)}: {sum(l.startswith("model") for l in L)} models, '
              f'{sum(l.startswith("entity") for l in L)} entities, {len(self.foliage)} foliage')
