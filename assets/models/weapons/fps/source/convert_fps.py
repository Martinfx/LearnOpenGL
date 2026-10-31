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
idle=[a for a in bpy.data.actions if a.name.endswith(cfg['clips']['idle'])][0]
arm.animation_data.action=idle
try: arm.animation_data.action_slot=idle.slots[0]
except: pass
sc.frame_set(0)
for o in list(bpy.data.objects):
    if o.type=='CAMERA': bpy.data.objects.remove(o)
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
for o in tops: o.matrix_world=T@o.matrix_world
# rename clips
keep=set()
for new,old in cfg['clips'].items():
    a=[x for x in bpy.data.actions if x.name.endswith(old)][0]; a.name=new; keep.add(new)
for a in list(bpy.data.actions):
    if a.name not in keep: bpy.data.actions.remove(a)
# shrink textures
for im in bpy.data.images:
    if im.size[0]>cfg.get('max_tex',2048): im.scale(cfg.get('max_tex',2048),cfg.get('max_tex',2048)*im.size[1]//im.size[0])
arm.animation_data.action=bpy.data.actions['idle']
bpy.ops.export_scene.gltf(filepath=out,export_format='GLB',export_animations=True,export_animation_mode='ACTIONS',
    export_force_sampling=True,export_cameras=False,export_lights=False,export_image_format='JPEG',export_jpeg_quality=88,
    export_yup=True,export_skins=True,export_def_bones=False,export_optimize_animation_size=False)
