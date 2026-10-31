# Procedural spent cartridge cases (made for this project, CC0). Units metres.
# Each GLB: one mesh, axis along +Z (glTF), mouth toward +Z, origin at the centre of the case.
import bpy,bmesh,math,sys
out=sys.argv[1]
mm=0.001
# profiles: list of (radius_mm, z_mm) from base centre outward along the outline, ending at the mouth inner edge
CASES={
 'casing_545x39':dict(mat='steel',p=[(0,0.6),(2.2,0.6),(2.2,0),(5.0,0),(5.0,1.2),(4.2,1.6),(4.2,2.4),(5.0,3.0),(4.95,29.0),(3.35,32.5),(3.15,33.0),(3.15,39.6),(2.85,39.6),(2.85,34.0),(4.5,30.0),(4.5,3.4),(0,3.4)]),
 'casing_9x18':dict(mat='steel',p=[(0,0.5),(2.4,0.5),(2.4,0),(4.95,0),(4.95,1.0),(4.2,1.4),(4.2,2.0),(4.95,2.5),(4.85,18.0),(4.5,18.0),(4.5,3.0),(0,3.0)]),
 'casing_9x19':dict(mat='brass',p=[(0,0.5),(2.4,0.5),(2.4,0),(4.95,0),(4.95,1.1),(4.3,1.5),(4.3,2.1),(4.95,2.6),(4.82,19.15),(4.45,19.15),(4.45,3.0),(0,3.0)]),
 'casing_50ae':dict(mat='brass',p=[(0,0.6),(2.7,0.6),(2.7,0),(6.0,0),(6.0,1.4),(5.6,1.8),(5.6,2.6),(6.95,3.2),(6.9,32.6),(6.45,32.6),(6.45,3.8),(0,3.8)]),
}
MATS={'brass':((0.86,0.64,0.30,1),1.0,0.28),'steel':((0.24,0.21,0.12,1),0.45,0.5)}
for name,c in CASES.items():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    me=bpy.data.meshes.new(name); ob=bpy.data.objects.new(name,me); bpy.context.scene.collection.objects.link(ob)
    bm=bmesh.new(); seg=32
    prof=c['p']; length=max(z for r,z in prof)
    rings=[]
    for (r,z) in prof:
        ring=[]
        for i in range(seg if r>0 else 1):
            a=2*math.pi*i/seg
            ring.append(bm.verts.new((r*mm*math.cos(a),r*mm*math.sin(a),(z-length/2)*mm)))
        rings.append(ring)
    for A,Bv in zip(rings[:-1],rings[1:]):
        if len(A)==1 and len(Bv)==1: continue
        if len(A)==1:
            for i in range(seg): bm.faces.new((A[0],Bv[(i+1)%seg],Bv[i]))
        elif len(Bv)==1:
            for i in range(seg): bm.faces.new((A[i],A[(i+1)%seg],Bv[0]))
        else:
            for i in range(seg): bm.faces.new((A[i],A[(i+1)%seg],Bv[(i+1)%seg],Bv[i]))
    bmesh.ops.recalc_face_normals(bm,faces=bm.faces)
    bm.to_mesh(me); bm.free()
    for p in me.polygons: p.use_smooth=True
    col,met,rough=MATS[c['mat']]
    m=bpy.data.materials.new(c['mat']); m.use_nodes=True; bs=m.node_tree.nodes['Principled BSDF']
    bs.inputs['Base Color'].default_value=col; bs.inputs['Metallic'].default_value=met; bs.inputs['Roughness'].default_value=rough
    me.materials.append(m)
    # glTF +Z == Blender -Y: rotate so the mouth (Blender +Z) points to Blender -Y
    me.transform(__import__('mathutils').Matrix.Rotation(math.radians(90),4,'X'))
    bpy.ops.export_scene.gltf(filepath=f'{out}/{name}.glb',export_format='GLB',export_yup=True)
    print(name,round(length,1),'mm',len(me.polygons))
