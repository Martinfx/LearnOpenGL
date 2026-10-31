# Scale reference set (CC0, made for this project). Metres, +Y up (glTF), faces +Z.
import bpy,sys,math
from mathutils import Vector
out=sys.argv[1]
bpy.ops.wm.read_factory_settings(use_empty=True)
def mat(name,rgb):
    m=bpy.data.materials.new(name); m.use_nodes=True; b=m.node_tree.nodes['Principled BSDF']
    b.inputs['Base Color'].default_value=(*rgb,1); b.inputs['Roughness'].default_value=0.8; return m
RED=mat('ref_red',(0.8,0.1,0.08)); WHITE=mat('ref_white',(0.9,0.9,0.9)); YEL=mat('ref_yellow',(0.95,0.75,0.1)); GREY=mat('ref_grey',(0.4,0.4,0.42)); BLUE=mat('ref_blue',(0.15,0.3,0.8))
def box(name,size,loc,m):
    bpy.ops.mesh.primitive_cube_add(size=1,location=loc); o=bpy.context.object; o.name=name
    o.scale=size; bpy.ops.object.transform_apply(scale=True); o.data.materials.append(m); return o
# blender: x right, y depth (-y = glTF +Z front), z up
# 1) measuring pole 3 m, 0.5 m stripes, at x=0
for i in range(6): box(f'pole_{i}',(0.08,0.08,0.5),(0,0,0.25+0.5*i),RED if i%2==0 else WHITE)
# eye height 1.7 m marker and character height 1.8 m marker
box('eye_1_7m',(0.4,0.02,0.02),(0.2,0,1.7),YEL); box('head_1_8m',(0.4,0.02,0.02),(0.2,0,1.8),BLUE)
# 2) door frame 0.9 x 2.1 m opening (frame 0.1 m), at x=1.5
for n,s,l in (('door_l',(0.1,0.2,2.2),(1.0,0,1.1)),('door_r',(0.1,0.2,2.2),(2.0,0,1.1)),('door_top',(1.1,0.2,0.1),(1.5,0,2.15))): box(n,s,l,GREY)
# 3) one-metre cube at x=3
box('cube_1m',(1,1,1),(3.0,0,0.5),WHITE)
# 4) storey 3 m: wall slice with floor lines at 0, 3, 6 m, at x=5
box('wall_2_storeys',(1.5,0.2,6.0),(5.0,0,3.0),GREY)
for k in range(3): box(f'floor_{k*3}m',(1.7,0.3,0.06),(5.0,0,k*3.0),YEL)
# 5) crate 0.5 m and step 0.2 m (stairs riser) at x=7
box('crate_0_5m',(0.5,0.5,0.5),(7.0,0,0.25),BLUE)
for k in range(5): box(f'step_{k}',(1.0,0.3,0.2),(8.5,-0.3*k,0.1+0.2*k),WHITE if k%2 else GREY)
bpy.ops.export_scene.gltf(filepath=out,export_format='GLB',export_yup=True)
print('ok')
