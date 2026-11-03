# Realistic deciduous trees and shrubs (CC0, made for this project), no Blender needed: python3 -I treegen2.py <species> <name> <height> <seed> [lod]
#
# A branch skeleton grown level by level (trunk, limbs, branches, shoots: golden-angle phyllotaxis, gravi- and
# phototropism, a crown envelope per species) carries photographic twig cards (source/twigbake.py: scanned Poly Haven
# leaves on real twigs, albedo + normal + baked AO, 2x2 atlas in textures/twigs_*.png). Cards sit along the outer shoots,
# turned toward the sky and out of the crown like real leaves, never as balls around the crown centre.
#
# _FOLIAGE per vertex (custom glTF attribute, engine: GltfLoader -> mesh.vert / pbr.frag):
#   R  ambient occlusion of the crown (deep inside dark, the sunny shell light), baked from the card cloud
#   G  branch flexibility for the wind (0 trunk, rising along limbs and shoots to 1 at the outer twigs)
#   B  branch phase (each limb and its shoots swing on their own)
#   A  leaf flutter (0 at a twig's base, 1 at its tip)
# glTF +Y up, metres, base at the origin. Textures are shared files next to the models (textures/, ../../../textures/pbr).
import json, math, os, random, struct, sys
import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.normpath(os.path.join(HERE, '..'))
UP = np.array([0.0, 1.0, 0.0])


def nrm(v):
    n = np.linalg.norm(v)
    return v / n if n > 1e-9 else v


def rot(axis, ang, v):  # Rodrigues
    axis = nrm(axis)
    return v * math.cos(ang) + np.cross(axis, v) * math.sin(ang) + axis * np.dot(axis, v) * (1 - math.cos(ang))


def perp(d):
    a = UP if abs(d[1]) < 0.9 else np.array([1.0, 0.0, 0.0])
    return nrm(np.cross(d, a))


# ---------------------------------------------------------------- species
# level: n = children per metre of parent (or 'count'), t = attach range on the parent, down = angle from the parent
# axis at the start / end of that range (deg), len = length ratio of the parent, rad = radius ratio, grav = bend down
# (+) or up (-) along the branch, wob = wiggle, segs = segments. crown: envelope of the limb lengths along the trunk.
# cards: twig atlas, card size (m), spacing (in card sizes) and from which point of a shoot they grow.
BROWN = dict(bark='../../../textures/pbr/bark_brown_02/albedo.jpg', bark_nor='../../../textures/pbr/bark_brown_02/normal.jpg', bark_tile=(0.9, 1.4))
WILLOW = dict(bark='../../../textures/pbr/bark_willow/albedo.jpg', bark_nor='../../../textures/pbr/bark_willow/normal.jpg', bark_tile=(0.8, 1.2))
BIRCH = dict(bark='textures/bark_birch.png', bark_nor='textures/bark_birch_nor.png', bark_tile=(0.55, 1.8))
SPECIES = {
    # Norway maple: short trunk forking into ascending scaffold limbs, a dense round crown
    'maple': dict(BROWN, trunk=0.36, radius=0.02, lean=0.03, fork=(3, 5), crown='round', atlas='broad', card=1.3, spacing=0.4,
                  tint=(0.92, 1.0, 0.86),
                  levels=[dict(n=1.6, t=(0.25, 1.0), down=(40, 25), len=0.62, rad=0.55, grav=0.12, wob=0.12, segs=6),
                          dict(n=2.0, t=(0.18, 1.0), down=(55, 40), len=0.5, rad=0.5, grav=0.2, wob=0.18, segs=4),
                          dict(n=2.2, t=(0.25, 1.0), down=(50, 40), len=0.45, rad=0.5, grav=0.12, wob=0.22, segs=3)],
                  leaf_levels=(2, 3), leaf_t=0.3),
    # ash / tree of heaven: taller oval crown on few stout ascending limbs, compound leaves, an open, airy crown
    'ash': dict(BROWN, trunk=0.45, radius=0.019, lean=0.04, fork=(2, 4), crown='oval', atlas='pinnate', card=1.05, spacing=0.55,
                tint=(0.95, 1.0, 0.9),
                levels=[dict(n=1.3, t=(0.25, 1.0), down=(30, 20), len=0.6, rad=0.55, grav=0.08, wob=0.1, segs=6),
                        dict(n=1.8, t=(0.2, 1.0), down=(50, 35), len=0.5, rad=0.5, grav=0.18, wob=0.15, segs=4),
                        dict(n=2.2, t=(0.3, 1.0), down=(45, 35), len=0.4, rad=0.5, grav=0.1, wob=0.2, segs=3)],
                leaf_levels=(2, 3), leaf_t=0.35),
    # silver birch: one trunk to the top, thin limbs climbing at a steep angle, the outer twigs hanging down
    'birch': dict(BIRCH, trunk=1.0, radius=0.012, lean=0.05, fork=None, crown='flame', crown_base=0.35, atlas='birch', card=1.0,
                  spacing=0.5, tint=(0.95, 1.0, 0.85), pendulous=0.65,
                  levels=[dict(n=2.6, t=(0.33, 0.97), down=(62, 35), len=0.36, rad=0.42, grav=0.3, wob=0.12, segs=6),
                          dict(n=2.0, t=(0.2, 1.0), down=(55, 40), len=0.55, rad=0.5, grav=0.7, wob=0.15, segs=4),
                          dict(n=1.6, t=(0.25, 1.0), down=(40, 30), len=0.45, rad=0.5, grav=0.6, wob=0.2, segs=3)],
                  leaf_levels=(2, 3), leaf_t=0.2),
    # poplar / alder: straight trunk far into the crown, an egg-shaped crown of short limbs
    'poplar': dict(BROWN, trunk=0.9, radius=0.016, lean=0.03, fork=None, crown='egg', crown_base=0.28, atlas='broad', card=1.2,
                   spacing=0.45, tint=(0.9, 1.0, 0.85),
                   levels=[dict(n=2.6, t=(0.28, 0.98), down=(55, 30), len=0.3, rad=0.45, grav=0.1, wob=0.12, segs=5),
                           dict(n=2.2, t=(0.2, 1.0), down=(50, 40), len=0.55, rad=0.5, grav=0.2, wob=0.18, segs=4),
                           dict(n=1.8, t=(0.3, 1.0), down=(45, 35), len=0.45, rad=0.5, grav=0.1, wob=0.2, segs=3)],
                   leaf_levels=(2, 3), leaf_t=0.25),
    # crack willow: short leaning trunk, wide spreading limbs, narrow leaves
    'willow': dict(WILLOW, trunk=0.3, radius=0.024, lean=0.12, fork=(3, 4), crown='wide', atlas='lance', card=0.9, spacing=0.42,
                   tint=(0.95, 1.0, 0.9),
                   levels=[dict(n=1.5, t=(0.2, 1.0), down=(50, 35), len=0.62, rad=0.55, grav=0.2, wob=0.14, segs=6),
                           dict(n=2.6, t=(0.2, 1.0), down=(55, 45), len=0.5, rad=0.5, grav=0.35, wob=0.18, segs=4),
                           dict(n=3.0, t=(0.25, 1.0), down=(45, 35), len=0.42, rad=0.5, grav=0.3, wob=0.2, segs=3)],
                   leaf_levels=(2, 3), leaf_t=0.3),
    # self-seeded young tree from a crack (tree of heaven, box elder, locust): 2-4 slender stems, few side shoots
    'sapling': dict(BROWN, trunk=1.0, radius=0.009, lean=0.12, fork=None, stems=(2, 4), crown='flame', crown_base=0.3, atlas='pinnate',
                    card=0.95, spacing=0.5, tint=(0.95, 1.0, 0.9),
                    levels=[dict(n=2.2, t=(0.3, 0.97), down=(50, 30), len=0.4, rad=0.5, grav=0.15, wob=0.15, segs=4),
                            dict(n=2.0, t=(0.3, 1.0), down=(45, 35), len=0.5, rad=0.5, grav=0.2, wob=0.2, segs=3)],
                    leaf_levels=(0, 1, 2), leaf_t=0.45),
    # the same with simple leaves (box elder, young poplar)
    'sapling_broad': None,
    # wild shrub (elder, hazel, lilac): many stems from the ground, a dense mound
    'bush': dict(BROWN, trunk=1.0, radius=0.012, lean=0.0, fork=None, stems=(7, 11), stem_spread=0.6, crown='mound', crown_base=0.12,
                 atlas='broad', card=0.8, spacing=0.36, tint=(0.9, 1.0, 0.85),
                 levels=[dict(n=3.5, t=(0.15, 0.97), down=(55, 35), len=0.45, rad=0.5, grav=0.3, wob=0.18, segs=4),
                         dict(n=3.0, t=(0.2, 1.0), down=(45, 35), len=0.5, rad=0.5, grav=0.25, wob=0.2, segs=3)],
                 leaf_levels=(0, 1, 2), leaf_t=0.15),
}


SPECIES['sapling_broad'] = dict(SPECIES['sapling'], atlas='broad', card=1.1, spacing=0.4)


def envelope(kind, s):
    """Limb length along the crown (s = 0 crown base .. 1 top), 0..1."""
    s = min(max(s, 0.0), 1.0)
    if kind == 'round':
        return 0.35 + 0.65 * math.sin(math.pi * (0.15 + 0.75 * s)) ** 0.6
    if kind == 'oval':
        return 0.3 + 0.7 * math.sin(math.pi * (0.1 + 0.85 * s)) ** 0.8
    if kind == 'flame':
        return 0.25 + 0.75 * (1 - s) ** 0.5 * min(1.0, s * 4 + 0.6)
    if kind == 'egg':
        return 0.25 + 0.75 * math.sin(math.pi * (0.05 + 0.9 * s ** 0.8)) ** 0.9
    if kind == 'wide':
        return 0.5 + 0.5 * math.sin(math.pi * (0.2 + 0.7 * s))
    if kind == 'mound':
        return 0.5 + 0.5 * math.sin(math.pi * (0.2 + 0.6 * s))
    return 1.0


class Branch:
    __slots__ = ('pts', 'rad', 'flex', 'level', 'phase', 'length', 'sides')


class Tree:
    def __init__(self, species, height, seed, lod):
        self.sp = SPECIES[species]
        self.H = height
        self.R = random.Random(seed)
        self.lod = lod
        self.small = height < 8.0 and species != 'bush'   # young trees: denser shoots, leaves already on the limbs
        self.branches = []
        self.cards = []  # (origin, up, normal, size, cell, flex0, phase)

    # -- skeleton
    def path(self, p0, d, length, segs, grav, wob, up=0.0, pendulous=0.0):
        R = self.R
        pts = [p0.copy()]
        d = nrm(d)
        for i in range(segs):
            s = (i + 1) / segs
            d = d + np.array([R.uniform(-1, 1), R.uniform(-1, 1), R.uniform(-1, 1)]) * wob
            d = d + UP * (up - grav * (0.4 + s) - pendulous * s * s) * (1.0 / segs)
            d = nrm(d)
            pts.append(pts[-1] + d * (length / segs))
        return pts

    def add_branch(self, pts, r0, r1, level, flex0, flex1, phase):
        b = Branch()
        b.pts = pts
        n = len(pts)
        L = [0.0]
        for i in range(1, n):
            L.append(L[-1] + float(np.linalg.norm(pts[i] - pts[i - 1])))
        tot = L[-1] or 1.0
        b.length = tot
        b.rad = [r0 + (r1 - r0) * (l / tot) ** 0.85 for l in L]
        b.flex = [flex0 + (flex1 - flex0) * (l / tot) ** 1.4 for l in L]
        b.level = level
        b.phase = phase
        self.branches.append(b)
        return b

    @staticmethod
    def at(b, t):
        f = t * (len(b.pts) - 1)
        i = min(int(f), len(b.pts) - 2)
        k = f - i
        p = b.pts[i] * (1 - k) + b.pts[i + 1] * k
        d = nrm(b.pts[i + 1] - b.pts[i])
        r = b.rad[i] * (1 - k) + b.rad[i + 1] * k
        fl = b.flex[i] * (1 - k) + b.flex[i + 1] * k
        return p, d, r, fl

    def grow_children(self, parent, lvl, crown_lo, crown_hi):
        sp = self.sp
        if lvl >= len(sp['levels']) or (self.lod >= 2 and lvl >= 2) or (self.lod >= 1 and lvl >= 3):
            return
        P = sp['levels'][lvl]
        R = self.R
        t0, t1 = P['t']
        count = max(1, int(round(P['n'] * parent.length * (t1 - t0) * R.uniform(0.85, 1.15) * (1.5 if self.small else 1.0))))
        if self.lod >= 1 and lvl >= 2:
            count = max(1, int(count * 0.75))
        az = R.uniform(0, 6.283)
        for k in range(count):
            t = t0 + (t1 - t0) * (k + R.uniform(0.2, 0.8)) / count
            p, d, r, fl = self.at(parent, t)
            az += math.radians(137.5) + R.uniform(-0.35, 0.35)
            down = math.radians(P['down'][0] + (P['down'][1] - P['down'][0]) * (t - t0) / max(t1 - t0, 1e-3) + R.uniform(-8, 8))
            side = perp(d)
            side = rot(d, az, side)
            cd = nrm(d * math.cos(down) + side * math.sin(down))
            if lvl == 0:  # limbs on the trunk: the crown envelope sets their length
                s = (p[1] - crown_lo) / max(crown_hi - crown_lo, 1e-3)
                L = self.H * P['len'] * envelope(sp['crown'], s) * R.uniform(0.85, 1.12)
                cd = nrm(cd + UP * 0.15)
            else:
                L = parent.length * P['len'] * (1.15 - 0.6 * (t - t0)) * R.uniform(0.75, 1.15)
            if L < 0.15:
                continue
            r0 = min(r * P['rad'], r * 0.8)
            segs = P['segs'] if self.lod == 0 else max(2, P['segs'] - 1 - lvl // 2)
            pend = sp.get('pendulous', 0.0) * (lvl >= 1)
            pts = self.path(p - d * r * 0.3, cd, L, segs, P['grav'], P['wob'], pendulous=pend)
            phase = parent.phase if lvl > 0 else R.random()
            phase = (phase + (R.uniform(-0.12, 0.12) if lvl > 0 else 0.0)) % 1.0
            flex1 = min(1.0, fl + (0.45 if lvl == 0 else 0.3 if lvl == 1 else 0.2) * min(1.0, L / 3.0 + 0.4))
            b = self.add_branch(pts, r0, max(0.0025, r0 * 0.2), lvl + 1, fl, flex1, phase)
            self.grow_children(b, lvl + 1, crown_lo, crown_hi)
            if lvl + 1 in sp['leaf_levels'] or (self.small and lvl + 1 >= 1):
                self.leaf_branch(b)

    def leaf_branch(self, b):
        sp = self.sp
        R = self.R
        size = sp['card'] * (1.4 if self.lod == 1 else 2.0 if self.lod == 2 else 1.0)
        spacing = sp['spacing'] * size * (1.0 if self.lod == 0 else 1.25) * 0.6   # summer crowns: twigs close together
        t0 = sp['leaf_t'] if b.level > 0 else max(sp['leaf_t'], 0.55)
        n = max(1, int(b.length * (1 - t0) / spacing))
        if self.lod == 2:
            n = max(1, n // 2)
        side = 1 if R.random() < 0.5 else -1
        for k in range(n):
            t = t0 + (1 - t0) * (k + R.uniform(0.3, 0.7)) / n
            p, d, r, fl = self.at(b, t)
            side = -side
            ref = UP - d * float(np.dot(UP, d))
            ref = nrm(ref) if np.linalg.norm(ref) > 1e-3 else perp(d)
            left = np.cross(d, ref)
            a = math.radians(R.uniform(30, 65))
            e = R.uniform(-0.5, 0.7)
            cu = nrm(d * math.cos(a) + (left * side * math.cos(e) + ref * math.sin(e)) * math.sin(a))
            cu = nrm(cu - UP * sp.get('pendulous', 0.0) * 0.9 * R.uniform(0.5, 1.0))
            self.add_card(p, cu, size * R.uniform(0.85, 1.15), fl, b.phase, edge=t > 0.85)
        p, d, r, fl = self.at(b, 1.0)  # the shoot's own tip
        self.add_card(p - d * 0.05, nrm(d - UP * sp.get('pendulous', 0.0) * 0.6), size * R.uniform(0.9, 1.1), fl, b.phase, edge=True)

    def add_card(self, p, cu, size, fl, phase, edge):
        R = self.R
        ctr = self.crown_center
        radial = p - ctr
        radial = nrm(radial) if np.linalg.norm(radial) > 1e-3 else UP
        target = nrm(UP * 0.75 + radial * 0.6)
        n0 = target - cu * float(np.dot(target, cu))
        n0 = nrm(n0) if np.linalg.norm(n0) > 1e-3 else perp(cu)
        n0 = rot(cu, R.uniform(-0.6, 0.6), n0)
        cell = R.choice([3, 3, 0, 1]) if edge else R.choice([0, 1, 2, 2, 0, 1])
        self.cards.append((p - cu * size * 0.04, cu, n0, size, cell, fl, phase))

    def build(self):
        sp = self.sp
        R = self.R
        H = self.H
        cb = sp.get('crown_base', sp['trunk'] * 0.85)
        crown_lo, crown_hi = H * cb, H
        self.crown_center = np.array([0.0, H * (cb + 1) * 0.5, 0.0])
        stems = sp.get('stems')
        r0 = sp['radius'] * H
        lean = np.array([R.uniform(-1, 1) * sp['lean'], 1.0, R.uniform(-1, 1) * sp['lean']])
        if stems:  # several stems from one root (saplings, shrubs)
            ns = R.randint(*stems)
            for m in range(ns):
                a = m * 6.283 / ns + R.uniform(-0.4, 0.4)
                spread = sp.get('stem_spread', 0.22) * R.uniform(0.5, 1.2)
                d = np.array([math.cos(a) * spread, 1.0, math.sin(a) * spread])
                L = H * R.uniform(0.7, 1.0)
                pts = self.path(np.array([math.cos(a) * 0.04, 0.0, math.sin(a) * 0.04]), d, L, 7 if self.lod == 0 else 4, 0.05, 0.05)
                b = self.add_branch(pts, r0 * R.uniform(0.7, 1.1), r0 * 0.15, 0, 0.0, 0.5, R.random())
                self.grow_children(b, 0, crown_lo, crown_hi)
                if 0 in sp['leaf_levels']:
                    self.leaf_branch(b)
        elif sp['fork']:  # decurrent: a trunk forking into scaffold limbs
            th = H * sp['trunk']
            pts = self.path(np.zeros(3), lean, th, 4, 0.0, 0.03)
            trunk = self.add_branch(pts, r0, r0 * 0.8, 0, 0.0, 0.04, 0.0)
            top = pts[-1]
            nf = R.randint(*sp['fork'])
            for m in range(nf):
                a = m * 6.283 / nf + R.uniform(-0.3, 0.3)
                out = np.array([math.cos(a), 0.0, math.sin(a)])
                tilt = R.uniform(0.35, 0.7)
                d = nrm(UP + out * tilt)
                L = (H - th) * R.uniform(0.85, 1.0) / max(d[1], 0.4)
                pts = self.path(top - lean * 0.15, d, L, 7 if self.lod == 0 else 5, 0.06, 0.07, up=0.05)
                b = self.add_branch(pts, r0 * 0.7 * R.uniform(0.85, 1.0), r0 * 0.12, 0, 0.04, 0.55, R.random())
                self.grow_scaffold(b, crown_lo, crown_hi)
        else:  # excurrent: one trunk to the top
            pts = self.path(np.zeros(3), lean, H, 10 if self.lod == 0 else 6, 0.0, 0.035, up=0.02)
            trunk = self.add_branch(pts, r0, r0 * 0.12, 0, 0.0, 0.35, R.random())
            self.grow_children(trunk, 0, crown_lo, crown_hi)
            if 0 in sp['leaf_levels']:
                self.leaf_branch(trunk)
        return self

    def grow_scaffold(self, b, crown_lo, crown_hi):
        """A scaffold limb of a forked crown acts as a small trunk: its branches follow the crown envelope too."""
        sp = self.sp
        P = sp['levels'][0]
        R = self.R
        t0, t1 = P['t']
        count = max(2, int(round(P['n'] * b.length * (t1 - t0) * 1.6 * R.uniform(0.85, 1.15))))
        az = R.uniform(0, 6.283)
        trunk_axis = nrm(b.pts[-1] - b.pts[0])
        for k in range(count):
            t = t0 + (t1 - t0) * (k + R.uniform(0.2, 0.8)) / count
            p, d, r, fl = self.at(b, t)
            az += math.radians(137.5) + R.uniform(-0.4, 0.4)
            # mostly outward: branches grow toward the light at the crown surface
            out = p - np.array([0.0, p[1], 0.0])
            out = nrm(out) if np.linalg.norm(out) > 0.05 else rot(UP, az, np.array([1.0, 0.0, 0.0]))
            side = rot(d, az, perp(d))
            down = math.radians(P['down'][0] + R.uniform(-10, 10))
            cd = nrm(d * math.cos(down) + side * math.sin(down) + out * 0.5)
            s = (p[1] - crown_lo) / max(crown_hi - crown_lo, 1e-3)
            L = self.H * 0.42 * envelope(sp['crown'], s) * R.uniform(0.75, 1.1) * (1.1 - 0.5 * t)
            if L < 0.3:
                continue
            P1 = sp['levels'][1]
            segs = P1['segs'] if self.lod == 0 else max(2, P1['segs'] - 1)
            pts = self.path(p - d * r * 0.3, cd, L, segs, P1['grav'], P1['wob'])
            phase = (b.phase + R.uniform(-0.15, 0.15)) % 1.0
            c = self.add_branch(pts, min(r * 0.55, r * 0.8), 0.003, 2, fl, min(1.0, fl + 0.35), phase)
            self.grow_children(c, 2, crown_lo, crown_hi)
            if 2 in sp['leaf_levels']:
                self.leaf_branch(c)
        self.leaf_branch(b)


# ---------------------------------------------------------------- meshes
def bark_mesh(tree):
    sp = tree.sp
    tu, tv = sp['bark_tile']
    P, N, UV, C, I = [], [], [], [], []
    for b in tree.branches:
        if tree.lod == 0:
            sides = {0: 12, 1: 8, 2: 6, 3: 4}.get(b.level, 4)
        else:
            sides = {0: 8, 1: 6, 2: 4, 3: 3}.get(b.level, 3) if tree.lod == 1 else {0: 6, 1: 4}.get(b.level, 3)
        if tree.lod >= 1 and b.rad[0] < 0.006:
            continue
        if tree.lod >= 2 and b.level >= 2:
            continue
        if b.rad[0] < 0.004 and b.level >= 3:
            sides = 3
        n = len(b.pts)
        d0 = nrm(b.pts[1] - b.pts[0])
        ax = perp(d0)
        base = len(P)
        L = 0.0
        circ = 2 * math.pi * b.rad[0]
        urep = max(1, round(circ / tu))
        for i in range(n):
            if i:
                L += float(np.linalg.norm(b.pts[i] - b.pts[i - 1]))
            d = nrm(b.pts[min(i + 1, n - 1)] - b.pts[max(i - 1, 0)])
            ax = nrm(ax - d * float(np.dot(ax, d)))
            bx = np.cross(d, ax)
            r = b.rad[i]
            if b.level == 0 and i == 0 and not sp.get('stems'):
                r *= 1.35  # root flare
            for k in range(sides + 1):
                a = 2 * math.pi * k / sides
                dirv = ax * math.cos(a) + bx * math.sin(a)
                P.append(b.pts[i] + dirv * r)
                N.append(dirv)
                UV.append((k / sides * urep, -L / tv))
                C.append((1.0, b.flex[i], b.phase, 0.0))
        for i in range(n - 1):
            for k in range(sides):
                a = base + i * (sides + 1) + k
                c = a + sides + 1
                I += [a, c, a + 1, a + 1, c, c + 1]
    return np.array(P, np.float32), np.array(N, np.float32), np.array(UV, np.float32), np.array(C, np.float32), np.array(I, np.uint32)


def card_mesh(tree):
    P, N, UV, C, I = [], [], [], [], []
    ctr = tree.crown_center
    for (o, cu, n0, s, cell, fl, phase) in tree.cards:
        right = np.cross(cu, n0)
        u0 = (cell % 2) * 0.5
        v0 = (cell // 2) * 0.5
        radial = nrm(o + cu * s * 0.5 - ctr)
        # the lighting normal: the card's own facing bent out of the crown, so the crown shades like a volume
        ln = nrm(n0 * 0.45 + radial * 0.55)
        base = len(P)
        for (a, b) in ((-0.5, 0.0), (0.5, 0.0), (0.5, 1.0), (-0.5, 1.0)):
            P.append(o + right * (a * s) + cu * (b * s))
            N.append(ln)
            # atlas cell: twig base at the bottom centre of the cell; image rows top-down (glTF UV)
            UV.append((u0 + (a + 0.5) * 0.5 * 0.998 + 0.001, v0 + (1.0 - b) * 0.5 * 0.998 + 0.001))
            C.append((1.0, min(1.0, fl + 0.12 * b), phase, b))
        I += [base, base + 1, base + 2, base, base + 2, base + 3]
    return np.array(P, np.float32), np.array(N, np.float32), np.array(UV, np.float32), np.array(C, np.float32), np.array(I, np.uint32)


def bake_ao(tree, parts):
    """Crown occlusion from the card cloud: how deep a point lies inside the crown's envelope (direction bins
    from the crown centre), darker underneath; written into COLOR_0.r."""
    cards = np.array([c[0] + c[1] * c[3] * 0.5 for c in tree.cards]) if tree.cards else np.zeros((1, 3))
    ctr = cards.mean(axis=0)
    ext = np.maximum(np.abs(cards - ctr).max(axis=0), 0.3)  # per axis
    def bins(p):
        q = (p - ctr) / ext
        r = np.linalg.norm(q, axis=1) + 1e-6
        az = (np.arctan2(q[:, 2], q[:, 0]) / (2 * np.pi) + 0.5) * 16
        el = (np.arcsin(np.clip(q[:, 1] / r, -1, 1)) / np.pi + 0.5) * 8
        return np.clip(az.astype(int), 0, 15), np.clip(el.astype(int), 0, 7), r, q
    a, e, r, _ = bins(cards)
    hull = np.zeros((16, 8))
    np.maximum.at(hull, (a, e), r)
    # fill and smooth the envelope (neighbouring bins)
    for _ in range(2):
        h2 = hull.copy()
        for da in (-1, 0, 1):
            for de in (-1, 0, 1):
                h2 = np.maximum(h2, np.roll(np.roll(hull, da, 0), de, 1) * (0.85 if da or de else 1.0))
        hull = h2
    hull = np.maximum(hull, 0.35)
    for P, C in parts:
        if len(P) == 0:
            continue
        a, e, r, q = bins(P)
        depth = np.clip(1.0 - r / hull[a, e], 0.0, 1.0)
        ao = 1.0 - 0.78 * np.clip(depth / 0.65, 0, 1) ** 0.9
        ao *= 0.82 + 0.18 * np.clip(q[:, 1] / np.maximum(r, 1e-3) * 0.5 + 0.5, 0, 1)   # the underside sees less sky
        C[:, 0] = np.clip(ao, 0.18, 1.0) * C[:, 0]


# ---------------------------------------------------------------- glTF writer
def write_glb(path, name, prims, materials, images):
    bin_ = bytearray()
    views, accs = [], []

    def add(arr, target, comp, typ, normalized=False, minmax=False):
        nonlocal bin_
        while len(bin_) % 4:
            bin_.append(0)
        off = len(bin_)
        data = arr.tobytes()
        bin_ += data
        views.append(dict(buffer=0, byteOffset=off, byteLength=len(data), **({'target': target} if target else {})))
        acc = dict(bufferView=len(views) - 1, componentType=comp, count=int(arr.shape[0]), type=typ)
        if normalized:
            acc['normalized'] = True
        if minmax:
            acc['min'] = [float(x) for x in arr.min(axis=0)]
            acc['max'] = [float(x) for x in arr.max(axis=0)]
        accs.append(acc)
        return len(accs) - 1
    gprims = []
    for (P, N, UV, C, I, mat) in prims:
        if len(I) == 0:
            continue
        col = np.clip(np.round(C * 255), 0, 255).astype(np.uint8)
        attrs = dict(POSITION=add(P, 34962, 5126, 'VEC3', minmax=True), NORMAL=add(N, 34962, 5126, 'VEC3'),
                     TEXCOORD_0=add(UV, 34962, 5126, 'VEC2'), _FOLIAGE=add(col, 34962, 5121, 'VEC4', normalized=True))
        gprims.append(dict(attributes=attrs, indices=add(I, 34963, 5125, 'SCALAR'), material=mat))
    gl = dict(asset=dict(version='2.0', generator='treegen2.py (FPS engine, CC0)'), scene=0, scenes=[dict(nodes=[0])],
              nodes=[dict(name=name, mesh=0)], meshes=[dict(name=name, primitives=gprims)], materials=materials,
              textures=[dict(source=i, sampler=0) for i in range(len(images))], images=[dict(uri=u) for u in images],
              samplers=[dict(magFilter=9729, minFilter=9987, wrapS=10497, wrapT=10497)],
              buffers=[dict(byteLength=len(bin_))], bufferViews=views, accessors=accs)
    js = json.dumps(gl, separators=(',', ':')).encode()
    while len(js) % 4:
        js += b' '
    while len(bin_) % 4:
        bin_.append(0)
    with open(path, 'wb') as f:
        f.write(struct.pack('<III', 0x46546C67, 2, 12 + 8 + len(js) + 8 + len(bin_)))
        f.write(struct.pack('<II', len(js), 0x4E4F534A) + js)
        f.write(struct.pack('<II', len(bin_), 0x004E4942) + bytes(bin_))


def bounds(t):
    pts = [p for b in t.branches for p in b.pts]
    for (o, cu, n0, sz, cell, fl, ph) in t.cards:
        right = np.cross(cu, n0)
        pts += [o + right * sz * 0.5, o - right * sz * 0.5, o + cu * sz + right * sz * 0.5, o + cu * sz - right * sz * 0.5]
    a = np.array(pts)
    return a.min(axis=0), a.max(axis=0)


def grown_height(species, height, seed):
    """The height to grow with so that the finished tree (cards included) is `height` tall (the leaves keep their size)."""
    g = height
    for _ in range(4):
        h = bounds(Tree(species, g, seed, 0).build())[1][1]
        if abs(h - height) < 0.02 * height:
            break
        g *= height / h
    return g


def generate(species, name, height, seed, lod, out_dir=OUT):
    grown = grown_height(species, height, seed)
    t = Tree(species, grown, seed, lod).build()
    if lod:  # the lower levels keep the silhouette of LOD 0 (bigger cards would grow the crown: a visible pop)
        lo0, hi0 = bounds(Tree(species, grown, seed, 0).build())
        lo, hi = bounds(t)
        k = np.minimum((hi0 - lo0) / np.maximum(hi - lo, 1e-3), 1.0)
        c0 = np.array([0.0, 0.0, 0.0])
        for b in t.branches:
            b.pts = [c0 + (p - c0) * k for p in b.pts]
        t.cards = [(c0 + (o - c0) * k, cu, n0, sz * float(k.mean()), cell, fl, ph) for (o, cu, n0, sz, cell, fl, ph) in t.cards]
        t.crown_center = t.crown_center * k
    sp = t.sp
    bP, bN, bUV, bC, bI = bark_mesh(t)
    cP, cN, cUV, cC, cI = card_mesh(t)
    bake_ao(t, [(bP, bC), (cP, cC)])
    images = [sp['bark'], sp['bark_nor'], f"textures/twigs_{sp['atlas']}.png", f"textures/twigs_{sp['atlas']}_nor.png"]
    tint = list(sp.get('tint', (1, 1, 1))) + [1.0]
    materials = [
        dict(name=name + '_bark', pbrMetallicRoughness=dict(baseColorTexture=dict(index=0), metallicFactor=0.0, roughnessFactor=0.92),
             normalTexture=dict(index=1)),
        dict(name=name + '_leaves', pbrMetallicRoughness=dict(baseColorTexture=dict(index=2), baseColorFactor=tint, metallicFactor=0.0,
                                                               roughnessFactor=0.62),
             normalTexture=dict(index=3, scale=0.8), alphaMode='MASK', alphaCutoff=0.5, doubleSided=True),
    ]
    sfx = '' if lod == 0 else f'_lod{lod}'
    path = os.path.join(out_dir, name + sfx + '.glb')
    write_glb(path, name, [(bP, bN, bUV, bC, bI, 0), (cP, cN, cUV, cC, cI, 1)], materials, images)
    allp = np.concatenate([bP, cP]) if len(cP) else bP
    dims = allp.max(axis=0) - allp.min(axis=0)
    print('TREE', name + sfx, 'tris', (len(bI) + len(cI)) // 3, 'cards', len(t.cards), 'dims', [round(float(x), 2) for x in dims])


if __name__ == '__main__':
    species, name, height, seed = sys.argv[1], sys.argv[2], float(sys.argv[3]), int(sys.argv[4])
    lods = [int(x) for x in sys.argv[5].split(',')] if len(sys.argv) > 5 else [0, 1, 2]
    for lod in lods:
        generate(species, name, height, seed, lod)
