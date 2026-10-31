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
import bmesh
def dress(arm,body,sleeve_wrist_gap=0.035):
    # dirty hands texture
    for m in body.data.materials:
        img=[n for n in m.node_tree.nodes if n.type=='TEX_IMAGE'][0]
        img.image=bpy.data.images.load('/tmp/out/hands_dirty.jpg'); img.image.pack()
    # sleeve: duplicate arm mesh, keep faces far enough from the wrists, push out along normals
    me=body.data.copy(); sl=bpy.data.objects.new('Sleeves',me); bpy.context.scene.collection.objects.link(sl)
    sl.parent=arm; sl.matrix_parent_inverse=Matrix.Identity(4)
    for mo in body.modifiers:
        if mo.type=='ARMATURE': m2=sl.modifiers.new('Armature','ARMATURE'); m2.object=arm
    wr={s:arm.data.bones[f'hand.{s}'].head_local.copy() for s in 'RL'}
    fa={s:arm.data.bones[f'forearm.{s}'] for s in 'RL'}
    bm=bmesh.new(); bm.from_mesh(me)
    def keep(v):
        co=v.co; s='R' if co.x>0 else 'L'
        b=fa[s]; axis=(b.tail_local-b.head_local).normalized()
        return (wr[s]-co).dot(axis)>sleeve_wrist_gap
    kill=[f for f in bm.faces if not all(keep(v) for v in f.verts)]
    bmesh.ops.delete(bm,geom=kill,context='FACES')
    bm.normal_update()
    for v in bm.verts: v.co+=v.normal*0.006
    bm.to_mesh(me); bm.free()
    mat=bpy.data.materials.new('Sleeve_Knit'); mat.use_nodes=True; nt=mat.node_tree; p=nt.nodes['Principled BSDF']
    t=nt.nodes.new('ShaderNodeTexImage'); t.image=bpy.data.images.load('/tmp/out/sleeve_knit.jpg'); t.image.pack()
    nt.links.new(t.outputs[0],p.inputs['Base Color']); p.inputs['Roughness'].default_value=0.95
    tn=nt.nodes.new('ShaderNodeTexImage'); tn.image=bpy.data.images.load('/tmp/out/sleeve_knit_normal.png'); tn.image.colorspace_settings.name='Non-Color'; tn.image.pack()
    nm=nt.nodes.new('ShaderNodeNormalMap'); nt.links.new(tn.outputs[0],nm.inputs['Color']); nt.links.new(nm.outputs[0],p.inputs['Normal'])
    # tile the knit 4x over the arm UVs
    tc=nt.nodes.new('ShaderNodeTexCoord'); mp=nt.nodes.new('ShaderNodeMapping'); mp.inputs['Scale'].default_value=(4,4,1)
    nt.links.new(tc.outputs['UV'],mp.inputs['Vector']); nt.links.new(mp.outputs[0],t.inputs['Vector']); nt.links.new(mp.outputs[0],tn.inputs['Vector'])
    me.materials.clear(); me.materials.append(mat)
    return sl
