#!/usr/bin/env python3
"""Far LOD for ground cover (grass clumps, weeds, flowers): keep a few whole blades/leaves, not a decimated mush.

    python3 -I vegthin.py <in.glb> <out.glb> [target_ratio=0.1]

A Decimate modifier on alpha-tested cards collapses each card into slivers and the clump shrinks to a stain; at
20-45 m a clump is a few pixels anyway. So the loose parts (blades, leaf cards) are thinned to about `target_ratio`
of the triangles, preferring the big ones, and the survivors are widened about their own base so the clump keeps its
coverage. Parts that are not separable (one fused mesh) fall back to the Decimate modifier. Needs Blender's bpy.
"""
import math
import random
import sys

import bpy  # noqa: I001  (bpy first: it puts bmesh on the path)
import bmesh
from mathutils import Vector

src, dst = sys.argv[1], sys.argv[2]
ratio = float(sys.argv[3]) if len(sys.argv) > 3 else 0.1
bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.gltf(filepath=src)
objs = [o for o in bpy.data.objects if o.type == 'MESH']
t0 = t1 = 0
rng = random.Random(7)
for o in objs:
    bm = bmesh.new()
    bm.from_mesh(o.data)
    bm.faces.ensure_lookup_table()
    t0 += sum(len(f.verts) - 2 for f in bm.faces)
    # islands by shared vertices
    seen, islands = set(), []
    for f in bm.faces:
        if f.index in seen:
            continue
        stack, isl = [f], []
        seen.add(f.index)
        while stack:
            g = stack.pop()
            isl.append(g)
            for v in g.verts:
                for h in v.link_faces:
                    if h.index not in seen:
                        seen.add(h.index)
                        stack.append(h)
        islands.append(isl)
    if len(islands) >= 12:
        tri = lambda isl: sum(len(f.verts) - 2 for f in isl)
        area = lambda isl: sum(f.calc_area() for f in isl)
        total = sum(tri(i) for i in islands)
        # big parts first (they carry the silhouette), with some randomness so it is not only the tallest blades
        order = sorted(islands, key=lambda i: -area(i) * rng.uniform(0.5, 1.5))
        keep, n = [], 0
        for isl in order:
            if n >= total * ratio and len(keep) >= 3:
                break
            keep.append(isl)
            n += tri(isl)
        a_all, a_keep = sum(area(i) for i in islands), sum(area(i) for i in keep)
        widen = min(2.2, math.sqrt(a_all / max(a_keep, 1e-6)) ** 0.8)
        kept = {f.index for isl in keep for f in isl}
        for isl in keep:                        # widen about the island's lowest point, horizontally only
            vs = {v for f in isl for v in f.verts}
            base = min(vs, key=lambda v: v.co.z).co.copy()
            for v in vs:
                d = v.co - base
                v.co = base + Vector((d.x * widen, d.y * widen, d.z))
        bmesh.ops.delete(bm, geom=[f for f in bm.faces if f.index not in kept], context='FACES')
        bm.to_mesh(o.data)
        bm.free()
    else:
        bm.free()
        tr = sum(len(p.vertices) - 2 for p in o.data.polygons)
        bpy.context.view_layer.objects.active = o
        d = o.modifiers.new('d', 'DECIMATE')
        d.ratio = max(ratio, 300.0 / max(tr, 1)) if tr > 300 else 1.0
        bpy.ops.object.modifier_apply(modifier='d')
    t1 += sum(len(p.vertices) - 2 for p in o.data.polygons)
for im in bpy.data.images:                     # far away: a quarter of the texels is plenty
    if im.size[0] > 512:
        im.scale(im.size[0] // 2, im.size[1] // 2)
for o in objs:
    o.select_set(True)
bpy.ops.export_scene.gltf(filepath=dst, export_format='GLB', use_selection=True, export_yup=True, export_image_format='AUTO')
print('THIN', src.split('/')[-1], t0, '->', t1)
