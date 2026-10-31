# Split a Poly Haven foliage glTF into one glb per variant, RGBA alpha-tested texture, feet at y=0, centred X/Z, + _lod1.
import bpy,sys,json,os,math
from mathutils import Vector
src,outdir,rgba,cfg=sys.argv[1],sys.argv[2],sys.argv[3],json.loads(sys.argv[4])
# cfg: {"variants":{"node_name":"out_name",...}, "max_tris":5000, "lod":0.4, "scale":1.0}
def reset():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.ops.import_scene.gltf(filepath=src)
reset()
names=list(cfg['variants'].items())
for node,outname in names:
    vs=1.0
    if isinstance(outname,list): outname,vs=outname
    reset()
    o=bpy.data.objects[node]; mw=o.matrix_world.copy()
    for x in list(bpy.data.objects):
        if x.name!=node: bpy.data.objects.remove(x,do_unlink=True)
    o.parent=None; o.matrix_world=mw
    bpy.context.view_layer.objects.active=o; o.select_set(True)
    bpy.ops.object.transform_apply(location=False,rotation=True,scale=True)
    s=cfg.get('scale',1.0)*vs
    if s!=1.0: o.scale=(s,s,s); bpy.ops.object.transform_apply(scale=True)
    ws=[o.matrix_world@v.co for v in o.data.vertices]
    mn=Vector(map(min,*ws)); mx=Vector(map(max,*ws))
    o.location-= Vector(((mn.x+mx.x)/2,(mn.y+mx.y)/2,mn.z)); bpy.ops.object.transform_apply(location=True)
    # texture swap: RGBA base colour with alpha
    img=bpy.data.images.load(rgba)
    for m in o.data.materials:
        nt=m.node_tree; b=[n for n in nt.nodes if n.type=='BSDF_PRINCIPLED'][0]
        tex=nt.nodes.new('ShaderNodeTexImage'); tex.image=img
        for l in list(b.inputs['Base Color'].links): nt.links.remove(l)
        nt.links.new(tex.outputs['Color'],b.inputs['Base Color']); nt.links.new(tex.outputs['Alpha'],b.inputs['Alpha'])
        for l in list(b.inputs['Roughness'].links)+list(b.inputs['Metallic'].links): nt.links.remove(l)
        b.inputs['Roughness'].default_value=0.85; b.inputs['Metallic'].default_value=0.0
        # keep uv from existing tex coord: images share UV0
    def tris(): return sum(len(p.vertices)-2 for p in o.data.polygons)
    t0=tris(); mt=cfg.get('max_tris',5000)
    if t0>mt:
        d=o.modifiers.new('d','DECIMATE'); d.ratio=mt/t0; bpy.ops.object.modifier_apply(modifier='d')
    def export(p):
        bpy.ops.export_scene.gltf(filepath=p,export_format='GLB',use_selection=True,export_yup=True,export_image_format='AUTO')
    p=os.path.join(outdir,outname+'.glb'); export(p)
    t1=tris()
    if cfg.get('lod'):
        for im in bpy.data.images:
            if im.size[0]>512: im.scale(im.size[0]//2,im.size[1]//2)
        d=o.modifiers.new('d','DECIMATE'); d.ratio=cfg['lod']; bpy.ops.object.modifier_apply(modifier='d')
        export(os.path.join(outdir,outname+'_lod1.glb'))
    print('VEG',outname,'tris',t0,'->',t1,'lod1',tris(),'size',[round(x,2) for x in (mx-mn)])
