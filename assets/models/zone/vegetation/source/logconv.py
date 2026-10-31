import bpy,sys
from mathutils import Vector
src,out,target=sys.argv[1],sys.argv[2],int(sys.argv[3])
bpy.ops.wm.read_factory_settings(use_empty=True); bpy.ops.import_scene.gltf(filepath=src)
o=[x for x in bpy.data.objects if x.type=='MESH'][0]; mw=o.matrix_world.copy(); o.parent=None; o.matrix_world=mw
bpy.context.view_layer.objects.active=o; o.select_set(True); bpy.ops.object.transform_apply(location=True,rotation=True,scale=True)
ws=[v.co for v in o.data.vertices]; mn=Vector(map(min,*ws)); mx=Vector(map(max,*ws))
for v in o.data.vertices: v.co-=Vector(((mn.x+mx.x)/2,(mn.y+mx.y)/2,mn.z))
t=sum(len(p.vertices)-2 for p in o.data.polygons)
for i,(ratio,suffix) in enumerate([(target/t,''),(0.4,'_lod1')]):
    d=o.modifiers.new('d','DECIMATE'); d.ratio=ratio; bpy.ops.object.modifier_apply(modifier='d')
    for im in bpy.data.images:
        m=1024 if i==0 else 512
        if im.size[0]>m: im.scale(m,m)
    bpy.ops.export_scene.gltf(filepath=out+suffix+'.glb',export_format='GLB',use_selection=True,export_yup=True)
    print('LOG',suffix,sum(len(p.vertices)-2 for p in o.data.polygons),[round(x,2) for x in o.dimensions])
