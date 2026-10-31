# Convert a Sketchfab-style FPS arms glTF into the engine convention:
# eye (Head_Cam bone at frame 0 of idle) at the origin, forward -Z (glTF), +Y up, metres.
import bpy,sys,math,json,mathutils
from mathutils import Matrix,Vector
src,out,cfg=sys.argv[1],sys.argv[2],json.loads(sys.argv[3])
bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.gltf(filepath=src,guess_original_bind_pose=False)
sc=bpy.context.scene; sc.render.fps=24
for o in list(bpy.data.objects):
    if o.type=='MESH' and any(o.name.startswith(d) for d in cfg.get('drop',[])): bpy.data.objects.remove(o)
arm=[o for o in bpy.data.objects if o.type=='ARMATURE'][0]
# repair meshes whose vertex data is in different units than the skin's bind pose
for name,k in cfg.get('fix_mesh',{}).items():
    me=bpy.data.objects[name].data
    me.transform(Matrix.Scale(k,4))
    if k<0: me.flip_normals()
idle=[a for a in bpy.data.actions if a.name.endswith(list(cfg['clips'].values())[0] if 'idle' not in cfg['clips'] else cfg['clips']['idle'])][0]
arm.animation_data.action=idle
try: arm.animation_data.action_slot=idle.slots[0]
except: pass
sc.frame_set(0)
for o in list(bpy.data.objects):
    if o.type=='CAMERA': bpy.data.objects.remove(o)
if 'eye' in cfg: eye=Vector(cfg['eye'])
else:
    M=arm.matrix_world@arm.pose.bones[cfg['cam']].matrix
    eye=M.translation.copy()
# real-world scale from weapon length
ms=[o for o in bpy.data.objects if o.type=='MESH']
dg=bpy.context.evaluated_depsgraph_get()
if 'scale' in cfg: s=cfg['scale']
else:
    w=[o for o in ms if o.name==cfg['weapon_mesh']][0].evaluated_get(dg); me=w.to_mesh()
    pts=[w.matrix_world@v.co for v in me.vertices]
    L=max(max(p[i] for p in pts)-min(p[i] for p in pts) for i in range(3)); s=cfg['weapon_length']/L
print('scale',s,'eye',eye)
# model looks along Blender -Y; glTF -Z == Blender +Y  -> rotate 180 deg about Z
T=Matrix.Scale(s,4)@Matrix.Rotation(math.pi,4,'Z')@Matrix.Translation(-eye)
tops=[o for o in bpy.data.objects if o.parent is None]
pr=cfg.get('post_rot',[0,0,0])
T=Matrix.Translation(Vector(cfg.get('post_offset',[0,0,0])))@__import__('mathutils').Euler([math.radians(a) for a in pr],'XYZ').to_matrix().to_4x4()@T
for o in tops: o.matrix_world=T@o.matrix_world
# remove one arm (vertices mostly weighted to bones with a prefix)
if cfg.get('drop_arm'):
    import bmesh
    for o in [o for o in bpy.data.objects if o.type=='MESH' and any(m.type=='ARMATURE' for m in o.modifiers)]:
        gi={g.index for g in o.vertex_groups if g.name.startswith(cfg['drop_arm'])}
        if not gi: continue
        bad=set(v.index for v in o.data.vertices if sum(g.weight for g in v.groups if g.group in gi)>0.5)
        bm=bmesh.new(); bm.from_mesh(o.data); bm.verts.ensure_lookup_table()
        bmesh.ops.delete(bm,geom=[bm.verts[i] for i in bad],context='VERTS'); bm.to_mesh(o.data); bm.free()
        print('dropped arm verts',len(bad),'from',o.name)
# bake node-animated (non-skinned) meshes into bones so engines only need skinning
if cfg.get('bake_nodes'):
    act0=arm.animation_data.action
    f0,f1=[int(round(x)) for x in act0.frame_range]
    rigid=[o for o in bpy.data.objects if o.type=='MESH' and not any(m.type=='ARMATURE' for m in o.modifiers)]
    # sample world matrices
    samples={o.name:[] for o in rigid}
    for f in range(f0,f1+1):
        sc.frame_set(f)
        for o in rigid: samples[o.name].append(o.matrix_world.copy())
    sc.frame_set(f0)
    bpy.context.view_layer.objects.active=arm
    for o in bpy.context.selected_objects: o.select_set(False)
    arm.select_set(True); bpy.ops.object.mode_set(mode='EDIT')
    Ainv=arm.matrix_world.inverted()
    for o in rigid:
        eb=arm.data.edit_bones.new('W_'+o.name); eb.head=(0,0,0); eb.tail=(0,0.05,0)
    bpy.ops.object.mode_set(mode='OBJECT')
    for o in rigid:
        bn='W_'+o.name; pb=arm.pose.bones[bn]; pb.rotation_mode='QUATERNION'
        rest=arm.data.bones[bn].matrix_local
        M0=samples[o.name][0]
        # mesh data -> armature space at the first sample, bone rest = identity
        o.animation_data_clear()
        o.data=o.data.copy(); o.data.transform(Ainv@M0)
        o.parent=arm; o.matrix_parent_inverse=Matrix(); o.matrix_world=arm.matrix_world.copy()
        vg=o.vertex_groups.new(name=bn); vg.add(range(len(o.data.vertices)),1.0,'REPLACE')
        md=o.modifiers.new('Armature','ARMATURE'); md.object=arm
        for i,f in enumerate(range(f0,f1+1)):
            D=Ainv@samples[o.name][i]@M0.inverted()@arm.matrix_world   # bone pose in armature space
            L=rest.inverted()@D@rest
            pb.location=L.translation; pb.rotation_quaternion=L.to_quaternion(); pb.scale=L.to_scale()
            for p in ('location','rotation_quaternion','scale'): pb.keyframe_insert(p,frame=f)
    print('baked',[o.name for o in rigid])
# rename clips
keep=set()
for new,old in cfg['clips'].items():
    a=[x for x in bpy.data.actions if x.name.endswith(old)][0]; a.name=new; keep.add(new)
for a in list(bpy.data.actions):
    if a.name not in keep: bpy.data.actions.remove(a)
# shrink textures
for im in bpy.data.images:
    if im.size[0]>cfg.get('max_tex',2048): im.scale(cfg.get('max_tex',2048),cfg.get('max_tex',2048)*im.size[1]//im.size[0])
arm.animation_data.action=bpy.data.actions['idle' if 'idle' in cfg['clips'] else list(cfg['clips'])[0]]
bpy.ops.export_scene.gltf(filepath=out,export_format='GLB',export_animations=True,export_animation_mode='ACTIONS',
    export_force_sampling=True,export_cameras=False,export_lights=False,export_image_format='JPEG',export_jpeg_quality=88,
    export_yup=True,export_skins=True,export_def_bones=False,export_optimize_animation_size=False)
