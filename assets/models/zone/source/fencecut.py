import bpy,sys,bmesh
from mathutils import Vector
import numpy as np
bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.gltf(filepath=sys.argv[1])
o=[x for x in bpy.data.objects if x.type=='MESH'][0]
me=o.data
C=np.array([o.matrix_world@p.center for p in me.polygons])
mat=np.array([p.material_index for p in me.polygons])
# find densest y row (fence running along x)
mats=[s.material.name if s.material else '' for s in o.material_slots]
want=sys.argv[4] if len(sys.argv)>4 else ''
msel=np.array([want in mats[i] for i in mat])
ys=np.round(C[msel,1],1); vals,cnt=np.unique(ys,return_counts=True)
best=vals[np.argsort(-cnt)[:5]]; print('rows',list(zip(vals[np.argsort(-cnt)[:5]],sorted(cnt)[::-1][:5])))
y0=best[0]
sel=(np.abs(C[:,1]-y0)<0.6)
xs=C[sel&msel,0]; print('x range',xs.min(),xs.max())
L=float(sys.argv[3]); x0=xs.min()+1.0
keep=sel&(C[:,0]>=x0)&(C[:,0]<x0+L)
bm=bmesh.new(); bm.from_mesh(me)
dele=[f for f,k in zip(bm.faces,keep) if not k]
bmesh.ops.delete(bm,geom=dele,context='FACES')
xa=float(C[keep,0].min()); 
for (px,nx) in ((xa,1),(xa+L,-1)):
    g=bm.verts[:]+bm.edges[:]+bm.faces[:]
    bmesh.ops.bisect_plane(bm,geom=g,plane_co=(px,0,0),plane_no=(nx,0,0),clear_outer=False,clear_inner=True)
bm.to_mesh(me); bm.free()
vs=[o.matrix_world@v.co for v in me.vertices]
mn=Vector([min(v[i] for v in vs) for i in range(3)]);mx=Vector([max(v[i] for v in vs) for i in range(3)])
print('piece',mx-mn,len(me.polygons))
from mathutils import Matrix
o.data.transform(o.matrix_world); o.matrix_world=Matrix()
o.data.transform(Matrix.Translation(Vector((-mn.x,-(mn.y+mx.y)/2,-mn.z))))
for m in bpy.data.materials:
    if not m.use_nodes: continue
    nt=m.node_tree; bs=[n for n in nt.nodes if n.type=='BSDF_PRINCIPLED']; tx=[n for n in nt.nodes if n.type=='TEX_IMAGE' and n.image and n.image.name.lower().startswith('reshet')]
    if bs and tx:
        gt=nt.nodes.new('ShaderNodeMath'); gt.operation='GREATER_THAN'; gt.inputs[1].default_value=0.5
        nt.links.new(tx[0].outputs['Alpha'],gt.inputs[0]); nt.links.new(gt.outputs[0],bs[0].inputs['Alpha'])
bpy.ops.export_scene.gltf(filepath=sys.argv[2],export_format='GLB',export_yup=True)
