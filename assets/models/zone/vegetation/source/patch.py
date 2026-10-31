# Combine several plant variants into one scattered patch (CC0). args: out.glb json{"items":[[glb,count],...],"radius":r,"seed":s,"lod":0/1}
import bpy,sys,json,random,math
cfg=json.loads(sys.argv[2]); out=sys.argv[1]; R=random.Random(cfg.get('seed',1))
bpy.ops.wm.read_factory_settings(use_empty=True)
objs=[]
for f,cnt in cfg['items']:
    before=set(bpy.data.objects); bpy.ops.import_scene.gltf(filepath=f)
    src=[o for o in set(bpy.data.objects)-before if o.type=='MESH'][0]
    for i in range(cnt):
        o=src.copy(); o.data=src.data; bpy.context.scene.collection.objects.link(o)
        a=R.uniform(0,6.28); r=cfg['radius']*math.sqrt(R.random())
        o.location=(math.cos(a)*r,math.sin(a)*r,0); o.rotation_euler=(0,0,R.uniform(0,6.28)); s=R.uniform(0.8,1.2); o.scale=(s,s,s*R.uniform(0.85,1.15))
        objs.append(o)
    bpy.data.objects.remove(src,do_unlink=True)
bpy.ops.object.select_all(action='DESELECT')
for o in objs: o.select_set(True)
bpy.context.view_layer.objects.active=objs[0]
bpy.ops.object.make_single_user(object=True,obdata=True); bpy.ops.object.join()
o=bpy.context.object; bpy.ops.object.transform_apply(location=True,rotation=True,scale=True)
if cfg.get('dec'):
    d=o.modifiers.new('d','DECIMATE'); d.ratio=cfg['dec']; bpy.ops.object.modifier_apply(modifier='d')
print('PATCH',out.split('/')[-1],sum(len(p.vertices)-2 for p in o.data.polygons),[round(x,2) for x in o.dimensions])
bpy.ops.export_scene.gltf(filepath=out,export_format='GLB',use_selection=True,export_yup=True)
