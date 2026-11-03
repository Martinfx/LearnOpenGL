"""Incendiary bottle mesh for Blender (bpy): a 0.5 l green glass rye bottle, a torn paper label and a
fuel-soaked rag stuffed in the neck. Z up, metres, the base at z = 0, the rag's top at about z = 0.33.
Used by build_world.py (the thrown bottle) and build_view.py (the first-person view model)."""
import math, os
import bpy, bmesh

HERE = os.path.dirname(os.path.abspath(__file__))
TEX = os.path.normpath(os.path.join(HERE, '..', 'textures'))
SEG = 28
# (radius, z) of the outer glass profile, bottom to lip
PROFILE = [(0.0, 0.002), (0.030, 0.0), (0.0345, 0.004), (0.0355, 0.012), (0.0355, 0.150), (0.034, 0.168),
           (0.029, 0.185), (0.021, 0.200), (0.0155, 0.214), (0.0138, 0.226), (0.0135, 0.258), (0.0152, 0.260),
           (0.0152, 0.268), (0.0125, 0.270)]
RAG_TOP = 0.33


def material(name, image=None, color=(0.8, 0.8, 0.8), rough=0.8, metal=0.0):
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    b = m.node_tree.nodes['Principled BSDF']
    b.inputs['Base Color'].default_value = (*color, 1)
    b.inputs['Roughness'].default_value = rough
    b.inputs['Metallic'].default_value = metal
    if image:
        t = m.node_tree.nodes.new('ShaderNodeTexImage')
        t.image = bpy.data.images.load(os.path.join(TEX, image))
        m.node_tree.links.new(t.outputs['Color'], b.inputs['Base Color'])
    return m


def lathe(name, profile, seg, mat, v_from=0.0, v_to=1.0, u_wrap=1.0, closed_top=False):
    bm = bmesh.new()
    uv = bm.loops.layers.uv.new('UVMap')
    rings = []
    for r, z in profile:
        ring = []
        for i in range(seg + 1):
            a = 2 * math.pi * i / seg
            ring.append(bm.verts.new((r * math.cos(a), r * math.sin(a), z)))
        rings.append(ring)
    zs = [z for _, z in profile]
    z0, z1 = min(zs), max(zs)
    for k in range(len(rings) - 1):
        for i in range(seg):
            f = bm.faces.new((rings[k][i], rings[k][i + 1], rings[k + 1][i + 1], rings[k + 1][i]))
            for loop in f.loops:
                idx_ring = 0 if loop.vert in rings[k] else 1
                ii = (rings[k] if idx_ring == 0 else rings[k + 1]).index(loop.vert)
                z = profile[k + idx_ring][1]
                loop[uv].uv = (ii / seg * u_wrap, v_from + (z - z0) / max(z1 - z0, 1e-6) * (v_to - v_from))
    bmesh.ops.remove_doubles(bm, verts=bm.verts, dist=1e-6)
    me = bpy.data.meshes.new(name)
    bm.to_mesh(me)
    bm.free()
    for p in me.polygons:
        p.use_smooth = True
    ob = bpy.data.objects.new(name, me)
    bpy.context.scene.collection.objects.link(ob)
    ob.data.materials.append(mat)
    return ob


def build(prefix=''):
    """Returns the bottle's objects (glass, label, rag), all with the origin at the bottle's base."""
    glass = material(prefix + 'molotov_glass', 'glass_albedo.png', rough=0.12)
    paper = material(prefix + 'molotov_label', 'label_albedo.png', rough=0.92)
    cloth = material(prefix + 'molotov_rag', 'rag_albedo.png', rough=0.95)
    objs = [lathe(prefix + 'bottle_glass', PROFILE, SEG, glass)]
    # Label: 3/4 around the body, slightly torn at the bottom edge (uneven lower ring)
    lab = lathe(prefix + 'bottle_label', [(0.0359, 0.055), (0.0359, 0.135)], SEG, paper, 0.0, 1.0, u_wrap=1.0 / 0.75)
    bm = bmesh.new(); bm.from_mesh(lab.data)
    gone = [f for f in bm.faces if math.atan2(f.calc_center_median().y, f.calc_center_median().x) % (2 * math.pi) > math.pi * 1.5]
    bmesh.ops.delete(bm, geom=gone, context='FACES')
    for v in bm.verts:
        if v.co.z < 0.06:
            a = math.atan2(v.co.y, v.co.x)
            v.co.z += 0.008 * (0.5 + 0.5 * math.sin(a * 7.0)) + 0.006 * (0.5 + 0.5 * math.sin(a * 13.0 + 1.0))
    bm.to_mesh(lab.data); bm.free()
    objs.append(lab)
    # Rag: a twisted roll in the neck that sticks out and flops to one side, and a strip hanging down.
    rag_prof = [(0.0118, 0.215), (0.0128, 0.262), (0.0165, 0.275), (0.0195, 0.292), (0.0180, 0.312), (0.0120, 0.326),
                (0.004, RAG_TOP)]
    rag = lathe(prefix + 'bottle_rag', rag_prof, 12, cloth, 0.0, 1.0)
    for v in rag.data.vertices:
        t = max(0.0, (v.co.z - 0.262) / (RAG_TOP - 0.262))
        a = math.atan2(v.co.y, v.co.x)
        r = math.hypot(v.co.x, v.co.y) * (1.0 + 0.18 * math.sin(a * 3 + v.co.z * 180))
        a += t * 1.2  # twist
        v.co.x = r * math.cos(a) + 0.012 * t * t
        v.co.y = r * math.sin(a)
    objs.append(rag)
    # Hanging strip: a thin bent quad strip down the outside of the neck
    bm = bmesh.new()
    uv = bm.loops.layers.uv.new('UVMap')
    pts = [(0.0158, 0.272), (0.0175, 0.255), (0.0195, 0.235), (0.0235, 0.215), (0.030, 0.198)]
    rows = []
    for k, (r, z) in enumerate(pts):
        row = []
        for j, a in enumerate((-0.32, 0.0, 0.32)):
            rr = r + 0.0006 * j
            row.append(bm.verts.new((rr * math.cos(a + 0.4), rr * math.sin(a + 0.4), z - 0.002 * abs(a) * (k + 1))))
        rows.append(row)
    for k in range(len(rows) - 1):
        for j in range(2):
            f = bm.faces.new((rows[k][j], rows[k][j + 1], rows[k + 1][j + 1], rows[k + 1][j]))
            for loop in f.loops:
                kk = 0 if loop.vert in rows[k] else 1
                jj = (rows[k] if kk == 0 else rows[k + 1]).index(loop.vert)
                loop[uv].uv = (jj / 2 * 0.3, 0.45 + (k + kk) / (len(rows) - 1) * 0.55)
    me = bpy.data.meshes.new(prefix + 'bottle_rag_strip'); bm.to_mesh(me); bm.free()
    strip = bpy.data.objects.new(prefix + 'bottle_rag_strip', me)
    bpy.context.scene.collection.objects.link(strip)
    me.materials.append(cloth)
    # thickness so it renders from both sides
    sol = strip.modifiers.new('solid', 'SOLIDIFY'); sol.thickness = 0.0015
    objs.append(strip)
    return objs
