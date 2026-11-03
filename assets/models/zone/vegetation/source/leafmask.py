# Rasterize the UV triangles of a Poly Haven tree's leaf material into an alpha mask (the leaf outlines).
import bpy,sys,numpy as np,json
from PIL import Image,ImageDraw
src,matname,out=sys.argv[-3:]
bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.gltf(filepath=src)
o=[o for o in bpy.data.objects if o.type=='MESH'][0]; me=o.data
mi=[i for i,m in enumerate(me.materials) if m.name==matname][0]
me.calc_loop_triangles()
n=len(me.loop_triangles)
tl=np.zeros(n*3,np.int32); me.loop_triangles.foreach_get('loops',tl)
tm=np.zeros(n,np.int32); me.loop_triangles.foreach_get('material_index',tm)
uv=np.zeros(len(me.loops)*2,np.float32); me.uv_layers[0].data.foreach_get('uv',uv); uv=uv.reshape(-1,2)
tl=tl.reshape(-1,3)[tm==mi]
tri=uv[tl]                      # (n,3,2)
tri=np.unique(np.round(tri,4).reshape(-1,6),axis=0).reshape(-1,3,2)
print('unique tris',len(tri))
S=2048; im=Image.new('L',(S*2,S*2),0); d=ImageDraw.Draw(im)
for t in tri:
    d.polygon([(float(u)*S*2,(1-float(v))*S*2) for u,v in t],fill=255)
im=im.resize((S,S),Image.LANCZOS); im.save(out); print('saved',out)
