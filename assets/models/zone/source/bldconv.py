# Convert a building/prop source (obj/fbx/blend) to an engine glTF: metres, +Y up, ground at y=0,
# centred on X/Z, textures <= max_tex, optional grime weathering, plus decimated LOD files.
import bpy,sys,os,json,math
from mathutils import Vector,Matrix
src,out,cfg=sys.argv[1],sys.argv[2],json.loads(sys.argv[3]) if len(sys.argv)>3 else {}
bpy.ops.wm.read_factory_settings(use_empty=True)
ext=src.lower().rsplit('.',1)[1]
if ext=='obj': bpy.ops.wm.obj_import(filepath=src)
elif ext=='fbx': bpy.ops.import_scene.fbx(filepath=src)
elif ext=='blend':
    with bpy.data.libraries.load(src) as (a,b): b.objects=[n for n in a.objects]
    for o in b.objects:
        if o and o.type=='MESH': bpy.context.scene.collection.objects.link(o)
elif ext in('glb','gltf'): bpy.ops.import_scene.gltf(filepath=src)
sc=bpy.context.scene
for o in list(bpy.data.objects):
    if o.type!='MESH' or any(o.name.startswith(d) for d in cfg.get('drop',[])) or (cfg.get('keep') and not any(o.name.startswith(k) for k in cfg['keep'])):
        if o.type!='MESH' or cfg.get('keep') or cfg.get('drop'): bpy.data.objects.remove(o)
ms=[o for o in bpy.data.objects if o.type=='MESH']
# bake transforms into mesh data
for o in ms:
    mw=o.matrix_world.copy(); o.parent=None; o.data=o.data.copy(); o.data.transform(mw); o.matrix_world=Matrix()
R=Matrix.Rotation(math.radians(cfg.get('rot_z',0)),4,'Z')
s=cfg.get('scale',1.0)
pts=[R@v.co*s for o in ms for v in o.data.vertices]
mn=Vector([min(p[i] for p in pts) for i in range(3)]);mx=Vector([max(p[i] for p in pts) for i in range(3)])
if 'height' in cfg: s*=cfg['height']/(mx.z-mn.z); mn*=cfg['height']/(mx.z-mn.z); mx*=cfg['height']/(mx.z-mn.z)
T=Matrix.Translation(Vector((-(mn.x+mx.x)/2,-(mn.y+mx.y)/2,-mn.z)))@Matrix.Scale(s,4)@R
for o in ms: o.data.transform(T)
dims=[round(x,2) for x in ((mx-mn))]
tris=sum(sum(len(p.vertices)-2 for p in o.data.polygons) for o in ms)
print('DIMS',dims,'TRIS',tris)
# optional texture override: one new PBR material for every mesh
if 'tex' in cfg:
    m=bpy.data.materials.new(cfg.get('mat_name','mat')); m.use_nodes=True; nt=m.node_tree; bs=nt.nodes['Principled BSDF']
    t=nt.nodes.new('ShaderNodeTexImage'); t.image=bpy.data.images.load(cfg['tex']['base']); nt.links.new(t.outputs['Color'],bs.inputs['Base Color'])
    bs.inputs['Roughness'].default_value=cfg.get('rough',0.75); bs.inputs['Metallic'].default_value=cfg.get('metal',0.0)
    if 'normal' in cfg['tex']:
        n=nt.nodes.new('ShaderNodeTexImage'); n.image=bpy.data.images.load(cfg['tex']['normal']); n.image.colorspace_settings.name='Non-Color'
        nm=nt.nodes.new('ShaderNodeNormalMap'); nt.links.new(n.outputs['Color'],nm.inputs['Color']); nt.links.new(nm.outputs['Normal'],bs.inputs['Normal'])
    for o in ms:
        o.data.materials.clear(); o.data.materials.append(m)
# textures
mt=cfg.get('max_tex',1024)
for im in bpy.data.images:
    if im.size[0]==0: continue
    if max(im.size)>mt: f=mt/max(im.size); im.scale(max(1,int(im.size[0]*f)),max(1,int(im.size[1]*f)))
    if cfg.get('grime',0)>0 and im.channels>=3 and not any(k in im.name.lower() for k in ('norm','nrm','spec','rough','metal','_ao')):
        import numpy as np
        w,h=im.size; px=np.array(im.pixels[:]).reshape(h,w,im.channels)
        rng=np.random.default_rng(abs(hash(im.name))%2**32)
        def nz(n):
            g=rng.random((n,n)); from PIL import Image as I
            return np.array(I.fromarray((g*255).astype('uint8')).resize((w,h),I.BICUBIC))/255.0
        streak=np.linspace(0,1,h)[:,None]  # blender pixel rows start at bottom: more dirt low
        dirt=np.clip(0.55*nz(6)+0.3*nz(24)+0.35*(1-streak),0,1)
        g=cfg['grime']; rgb=px[...,:3]
        lum=rgb.mean(axis=2,keepdims=True)
        rgb=rgb*(1-0.35*g)+lum*0.35*g   # desaturate
        tint=np.array([0.32,0.29,0.24])
        k=(g*0.55*dirt)[...,None]; rgb=rgb*(1-k)+rgb*tint*1.6*k
        px[...,:3]=np.clip(rgb,0,1); im.pixels[:]=px.ravel()
def export(path):
    bpy.ops.export_scene.gltf(filepath=path,export_format='GLB',export_yup=True,export_image_format='JPEG' if not cfg.get('png') else 'AUTO',export_jpeg_quality=85,export_apply=True)
export(out)
for i,r in enumerate(cfg.get('lods',[]),1):
    for o in ms:
        m=o.modifiers.new('dec','DECIMATE'); m.ratio=r
    export(out.replace('.glb',f'_lod{i}.glb'))
    for o in ms: o.modifiers.remove(o.modifiers['dec'])
print('OK',out)
