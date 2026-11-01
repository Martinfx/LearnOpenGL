import bpy,sys,os,math
from mathutils import Vector
SRC=sys.argv[1]; TEX=os.path.dirname(SRC)
bpy.ops.wm.open_mainfile(filepath=SRC)
for o in list(bpy.data.objects):
    if o.type in ('CAMERA','LIGHT'): bpy.data.objects.remove(o)
arm=[o for o in bpy.data.objects if o.type=='ARMATURE'][0]; me=[o for o in bpy.data.objects if o.type=='MESH'][0]
# material
m=me.data.materials[0]; m.use_nodes=True; nt=m.node_tree; nt.nodes.clear()
out=nt.nodes.new('ShaderNodeOutputMaterial'); bs=nt.nodes.new('ShaderNodeBsdfPrincipled'); nt.links.new(bs.outputs[0],out.inputs[0])
def img(name,cs):
    n=nt.nodes.new('ShaderNodeTexImage'); n.image=bpy.data.images.load(os.path.join(TEX,name)); n.image.colorspace_settings.name=cs; return n
c=img('wolf_albedo.png','sRGB'); nt.links.new(c.outputs[0],bs.inputs['Base Color'])
r=img('dog2Roughness.png','Non-Color'); nt.links.new(r.outputs[0],bs.inputs['Roughness'])
n=img('dog2Normal.png','Non-Color'); nm=nt.nodes.new('ShaderNodeNormalMap'); nt.links.new(n.outputs[0],nm.inputs['Color']); nt.links.new(nm.outputs[0],bs.inputs['Normal'])
bs.inputs['Metallic'].default_value=0; 
# scale so withers (top of spine03 head ~ z max of shoulders) = 0.6 m
bpy.context.view_layer.update()
withers=(arm.matrix_world@arm.data.bones['b_Spine03'].head_local).z
k=0.6/withers
bpy.ops.object.select_all(action='DESELECT'); arm.select_set(True); me.select_set(True); bpy.context.view_layer.objects.active=arm
arm.scale=arm.scale*k; bpy.context.view_layer.update()
bpy.ops.object.transform_apply(location=False,rotation=True,scale=True)
bpy.context.view_layer.update()
import numpy as np
dg=bpy.context.evaluated_depsgraph_get(); ev=me.evaluated_get(dg); vs=np.array([me.matrix_world@v.co for v in ev.data.vertices])
print('dims',vs.max(0)-vs.min(0),'min',vs.min(0),'withers',(arm.matrix_world@arm.data.bones['b_Spine03'].head_local).z, 'arm scale',tuple(arm.scale),'me scale',tuple(me.scale))
bpy.ops.wm.save_as_mainfile(filepath=sys.argv[2])
