import bpy,sys,math
import numpy as np
bpy.ops.wm.open_mainfile(filepath=sys.argv[1])
for o in list(bpy.data.objects):
    if o.type not in ('ARMATURE','MESH'): bpy.data.objects.remove(o)
arm=[o for o in bpy.data.objects if o.type=='ARMATURE'][0]; me=[o for o in bpy.data.objects if o.type=='MESH'][0]
def unhide(lc):
    lc.exclude=False; lc.hide_viewport=False; lc.collection.hide_viewport=False; lc.collection.hide_select=False
    for c in lc.children: unhide(c)
unhide(bpy.context.view_layer.layer_collection)
for o in bpy.data.objects:
    if o.name not in bpy.context.scene.collection.all_objects: bpy.context.scene.collection.objects.link(o)
    o.hide_viewport=False; o.hide_select=False; o.hide_set(False)
print('img',[(i.name,i.size[:],i.packed_file is not None,i.filepath) for i in bpy.data.images])
bpy.context.view_layer.objects.active=me
if bpy.context.object.mode!='OBJECT': bpy.ops.object.mode_set(mode='OBJECT')
for m in list(me.modifiers):
    if m.type in ('MIRROR','EDGE_SPLIT'): bpy.ops.object.modifier_apply(modifier=m.name)
arm.animation_data_clear() if arm.animation_data else None
# a pivot empty: rotate 180 about Z, scale to 0.45 m beak-tail, feet on ground
k=0.45/1.66
piv=bpy.data.objects.new('piv',None); bpy.context.scene.collection.objects.link(piv)
for o in (arm,me):
    if o.parent is None: o.parent=piv
piv.rotation_euler=(0,0,math.pi); piv.scale=(k,k,k); bpy.context.view_layer.update()
toe=(arm.matrix_world@arm.data.bones['toe.R'].tail_local).z; piv.location.z=-toe+0.004; bpy.context.view_layer.update()
for o in (arm,me):
    mw=o.matrix_world.copy(); 
    if o.parent==piv: o.parent=None; o.matrix_world=mw
bpy.data.objects.remove(piv)
bpy.ops.object.select_all(action='DESELECT'); arm.select_set(True); me.select_set(True); bpy.context.view_layer.objects.active=arm
print('DBG',bpy.context.mode,[o.name for o in bpy.context.selected_editable_objects],[s.name for s in bpy.data.scenes],bpy.context.scene.name,[o.library for o in (arm,me)]); bpy.ops.object.mode_set(mode='OBJECT'); bpy.ops.object.transform_apply(location=True,rotation=True,scale=True)

dg=bpy.context.evaluated_depsgraph_get(); ev=me.evaluated_get(dg); vs=np.array([me.matrix_world@v.co for v in ev.data.vertices])
print('dims',vs.max(0)-vs.min(0),'min',vs.min(0),'max',vs.max(0),'tris',sum(len(p.vertices)-2 for p in me.data.polygons))
for a in bpy.data.actions: print('action',a.name,a.frame_range[:],a.users)
bpy.ops.wm.save_as_mainfile(filepath=sys.argv[2])
