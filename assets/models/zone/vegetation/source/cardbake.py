# Render foliage geometry to an unlit RGBA card (albedo + alpha), orthographic.
import bpy,sys,json,math
from mathutils import Vector
cfg=json.loads(sys.argv[1])
bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.gltf(filepath=cfg['src'])
keep=cfg.get('objects')
for o in list(bpy.data.objects):
    if o.type=='MESH' and keep and o.name not in keep: bpy.data.objects.remove(o,do_unlink=True)
alphas={k:bpy.data.images.load(v) for k,v in cfg.get('alpha',{}).items()}
for alph in alphas.values(): alph.colorspace_settings.name='Non-Color'
for m in bpy.data.materials:
    if not m.node_tree: continue
    nt=m.node_tree; b=[n for n in nt.nodes if n.type=='BSDF_PRINCIPLED']
    if not b: continue
    b=b[0]; src=b.inputs['Base Color'].links[0].from_node if b.inputs['Base Color'].links else None
    img=src.image if src is not None and src.type=='TEX_IMAGE' else None
    for n in list(nt.nodes):
        if n.type not in ('OUTPUT_MATERIAL','TEX_IMAGE','UVMAP'): nt.nodes.remove(n)
    out=[n for n in nt.nodes if n.type=='OUTPUT_MATERIAL'][0]
    em=nt.nodes.new('ShaderNodeEmission'); tr=nt.nodes.new('ShaderNodeBsdfTransparent'); mx=nt.nodes.new('ShaderNodeMixShader')
    tint=cfg.get('tint',{}).get(m.name)
    if img:
        t=[n for n in nt.nodes if n.type=='TEX_IMAGE' and n.image==img][0]
        col=t.outputs['Color']
        if tint:
            mul=nt.nodes.new('ShaderNodeMix'); mul.data_type='RGBA'; mul.blend_type='MULTIPLY'; mul.inputs[0].default_value=1.0
            nt.links.new(col,mul.inputs[6]); mul.inputs[7].default_value=(*tint,1); col=mul.outputs[2]
        nt.links.new(col,em.inputs['Color'])
        a=None
        for k,al in alphas.items():
            if k in m.name:
                ta=nt.nodes.new('ShaderNodeTexImage'); ta.image=al
                uv=t.inputs['Vector'].links[0].from_socket if t.inputs['Vector'].links else None
                if uv: nt.links.new(uv,ta.inputs['Vector'])
                a=ta.outputs['Color']
        if a: nt.links.new(a,mx.inputs[0])
        else: mx.inputs[0].default_value=1.0
    else: mx.inputs[0].default_value=1.0
    if m.name in cfg.get('hide',[]):
        for l in list(mx.inputs[0].links): nt.links.remove(l)
        mx.inputs[0].default_value=0.0
    nt.links.new(tr.outputs[0],mx.inputs[1]); nt.links.new(em.outputs[0],mx.inputs[2]); nt.links.new(mx.outputs[0],out.inputs['Surface'])
ms=[o for o in bpy.data.objects if o.type=='MESH']
mn=Vector((1e9,)*3); mx_=-mn
for o in ms:
    for c in o.bound_box:
        w=o.matrix_world@Vector(c); mn=Vector(map(min,mn,w)); mx_=Vector(map(max,mx_,w))
print('BOUNDS',[round(x,2) for x in mn],[round(x,2) for x in mx_])
cen=Vector(cfg['center']) if 'center' in cfg else (mn+mx_)/2
cam=bpy.data.cameras.new('c'); cam.type='ORTHO'
W,H=cfg.get('res',[1024,1024]); cam.ortho_scale=cfg['size']
c=bpy.data.objects.new('c',cam); bpy.context.scene.collection.objects.link(c)
v=cfg.get('view','side'); D=50
if v=='top': c.location=cen+Vector((0,0,D)); c.rotation_euler=(0,0,cfg.get('spin',0))
elif v=='front': c.location=cen+Vector((0,-D,0)); c.rotation_euler=(math.radians(90),0,0)
else: c.location=cen+Vector((D,0,0)); c.rotation_euler=(math.radians(90),0,math.radians(90))
cam.clip_end=200
bpy.context.scene.camera=c
sc=bpy.context.scene; sc.render.engine='CYCLES'; sc.cycles.samples=cfg.get('samples',32); sc.render.resolution_x=W; sc.render.resolution_y=H
sc.render.film_transparent=True; sc.view_settings.view_transform='Standard'; sc.render.image_settings.color_mode='RGBA'
sc.cycles.transparent_max_bounces=64; sc.cycles.max_bounces=64
sc.render.filepath=cfg['out']; bpy.ops.render.render(write_still=True)
