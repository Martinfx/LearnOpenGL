import sys,json; sys.path.insert(0,'/tmp/tools')
from fp_base import *
def load_weapon(path,keep,mag_name=None,mag_off=(0,0,0)):
    before=set(bpy.data.objects)
    bpy.ops.import_scene.gltf(filepath=path)
    new=[o for o in bpy.data.objects if o not in before]
    for o in new:
        if o.type=='MESH' and o.name not in keep: bpy.data.objects.remove(o)
    new=[o for o in bpy.data.objects if o not in before]
    for o in new:
        if o.name==mag_name: o.location+=Vector(mag_off)
    bpy.context.view_layer.update()
    return new
def weapon_world(pos,fwd_axis_local='X',up_local='Z'):
    # weapon local X (barrel) -> +Y view, local Z -> +Z, local Y -> -X
    R=Matrix(((0,-1,0),(1,0,0),(0,0,1))).to_4x4()  # columns: localX->(0,1,0), localY->(-1,0,0), localZ->(0,0,1)
    R.translation=Vector(pos); return R
