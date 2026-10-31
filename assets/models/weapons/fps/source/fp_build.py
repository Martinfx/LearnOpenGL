import sys,json; sys.path.insert(0,'/tmp/tools')
from fp_weap import *
FPS=30
import contextlib
@contextlib.contextmanager
def ctx(arm):
    bpy.context.view_layer.objects.active=arm
    for o in bpy.context.view_layer.objects: o.select_set(o==arm)
    yield
def add_bones(arm,W,extra):
    with ctx(arm): bpy.ops.object.mode_set(mode='EDIT')
    eb=arm.data.edit_bones
    w=eb.new('weapon'); w.head=W.translation; w.tail=W.translation+Vector((0,0.08,0)); w.use_deform=True
    for side in 'RL': eb[f'hand.{side}.control'].parent=w
    for name,pos in extra.items():
        b=eb.new(name); b.head=pos; b.tail=pos+Vector((0,0.05,0)); b.parent=w; b.use_deform=True
    with ctx(arm): bpy.ops.object.mode_set(mode='POSE')
    with ctx(arm): bpy.ops.object.mode_set(mode='OBJECT')
def fix_arm_material(body):
    for m in body.data.materials:
        nt=m.node_tree; img=[n for n in nt.nodes if n.type=='TEX_IMAGE'][0]
        out=[n for n in nt.nodes if n.type=='OUTPUT_MATERIAL'][0]
        for n in list(nt.nodes):
            if n.type=='BSDF_DIFFUSE': nt.nodes.remove(n)
        p=nt.nodes.new('ShaderNodeBsdfPrincipled'); p.inputs['Roughness'].default_value=0.65
        nt.links.new(img.outputs['Color'],p.inputs['Base Color']); nt.links.new(p.outputs[0],out.inputs[0])
        img.image.pack()
def skin_weapon(arm,objs,groups):
    # groups: object name -> bone name ; join all into one mesh
    meshes=[o for o in objs if o.type=='MESH']
    for o in meshes:
        g=groups.get(o.name,'weapon'); vg=o.vertex_groups.new(name=g); vg.add(range(len(o.data.vertices)),1.0,'REPLACE')
    for o in meshes:
        mw=o.matrix_world.copy(); o.parent=None; o.data=o.data.copy(); o.data.transform(mw); o.matrix_world=Matrix.Identity(4)
    main=meshes[0]
    with bpy.context.temp_override(active_object=main,object=main,selected_objects=meshes,selected_editable_objects=meshes):
        bpy.ops.object.join()
    for o in list(bpy.data.objects):
        if o.type=='EMPTY': bpy.data.objects.remove(o)
    main.name='Weapon'; main.parent=arm; main.matrix_parent_inverse=Matrix.Identity(4)
    m=main.modifiers.new('Armature','ARMATURE'); m.object=arm
    return main
class Anim:
    def __init__(s,arm): s.arm=arm; s.keys=[]
    def action(s,name,frames,fn,loop=False):
        arm=s.arm; arm.animation_data_create()
        act=bpy.data.actions.new(name); act.use_fake_user=True; arm.animation_data.action=act
        for f in range(frames+1):
            t=f/FPS; fn(f,t)
            bpy.context.view_layer.update()
            for pb in arm.pose.bones:
                if pb.name.startswith(('weapon','hand.','f_','thumb','magazine','slide','bolt','blade')) and not pb.name.startswith('hand.R') or pb.name in('hand.R.control',):
                    pb.keyframe_insert('location',frame=f); pb.keyframe_insert('rotation_quaternion',frame=f)
                    if pb.rotation_mode!='QUATERNION': pb.keyframe_insert('rotation_euler',frame=f)
        act.frame_range=(0,frames)
        try: act.use_frame_range=True
        except: pass
        return act
def smooth(x): x=max(0,min(1,x)); return x*x*(3-2*x)
def seg(t,a,b): return smooth((t-a)/(b-a)) if b>a else (1.0 if t>=b else 0.0)
def lerpv(a,b,k): return Vector(a).lerp(Vector(b),k)
def eul(p,y,r): return Euler((math.radians(p),math.radians(r),math.radians(y)),'XYZ').to_matrix().to_4x4()
def export(path):
    bpy.ops.export_scene.gltf(filepath=path,export_format='GLB',export_animations=True,export_animation_mode='ACTIONS',
        export_force_sampling=True,export_frame_range=False,export_def_bones=False,export_cameras=False,export_lights=False,
        export_image_format='JPEG',export_jpeg_quality=90,export_apply=False,export_yup=True,export_skins=True,export_optimize_animation_size=False)
