import bpy,sys,math,os
from mathutils import Vector,Quaternion
bpy.ops.wm.open_mainfile(filepath=sys.argv[1]); OUT=sys.argv[2]; TEX=sys.argv[3]
sc=bpy.context.scene; sc.render.fps=30
arm=[o for o in bpy.data.objects if o.type=='ARMATURE'][0]; me=[o for o in bpy.data.objects if o.type=='MESH'][0]
B=arm.data.bones; P=arm.pose.bones
# material
m=me.data.materials[0]; nt=m.node_tree; nt.nodes.clear()
o=nt.nodes.new('ShaderNodeOutputMaterial'); bs=nt.nodes.new('ShaderNodeBsdfPrincipled'); nt.links.new(bs.outputs[0],o.inputs[0])
t=nt.nodes.new('ShaderNodeTexImage'); t.image=bpy.data.images.load(TEX); nt.links.new(t.outputs[0],bs.inputs['Base Color'])
bs.inputs['Roughness'].default_value=0.55; bs.inputs['Metallic'].default_value=0
for img in list(bpy.data.images):
    if img.name=='raven': bpy.data.images.remove(img)
fly=bpy.data.actions['fly']
def axis_local(n,ax): return (B[n].matrix_local.to_3x3().inverted()@Vector(ax)).normalized()
def rot(n,ax,a): pb=P[n]; pb.rotation_mode='QUATERNION'; pb.rotation_quaternion=Quaternion(axis_local(n,ax),a)@pb.rotation_quaternion
def loc(n,v): P[n].location=B[n].matrix_local.to_3x3().inverted()@Vector(v)
X,Y,Z=(1,0,0),(0,1,0),(0,0,1)
def reset():
    for pb in P: pb.rotation_mode='QUATERNION'; pb.rotation_quaternion=(1,0,0,0); pb.location=(0,0,0); pb.scale=(1,1,1)
def snapshot(): return {pb.name:(pb.location.copy(),pb.rotation_quaternion.copy() if pb.rotation_mode=='QUATERNION' else pb.rotation_euler.to_quaternion()) for pb in P}
def apply(snap):
    for n,(l,q) in snap.items(): P[n].rotation_mode='QUATERNION'; P[n].location=l; P[n].rotation_quaternion=q
def blend(a,b,t): return {n:(a[n][0].lerp(b[n][0],t),a[n][1].slerp(b[n][1],t)) for n in a}
WING={s:['Bone.001_'+s,'Bone.001_'+s+'.003','Bone.001_'+s+'.001','Bone.001_'+s+'.004','Bone.001_'+s+'.002','Bone.001_'+s+'.005'] for s in 'LR'}
SX={s:(1 if (arm.matrix_world@B[WING[s][0]].tail_local).x>0 else -1) for s in 'LR'}
def fold(k=1.0):
    for s in 'LR':
        w=WING[s]; sx=SX[s]
        rot(w[0],Z,sx*math.radians(75)*k); rot(w[0],Y,sx*math.radians(25)*k)
        rot(w[1],Z,-sx*math.radians(140)*k)
        rot(w[2],Z,sx*math.radians(150)*k); rot(w[2],Y,-sx*math.radians(10)*k)
def fly_pose(f):
    arm.animation_data_create(); arm.animation_data.action=fly
    if fly.slots: arm.animation_data.action_slot=fly.slots[0]
    sc.frame_set(int(f),subframe=f-int(f)); s=snapshot(); arm.animation_data.action=None; return s
FRAMES={}
def clip(name,n,fn):
    fr=[]
    for f in range(n+1):
        reset(); fn(f); bpy.context.view_layer.update(); fr.append(snapshot())
    FRAMES[name]=fr; print('sampled',name,n)
def idle(f):
    t=f/90; fold()
    look=[0,0.5,0.5,-0.4,-0.4,0.1,0][int(t*6)]  # head turns in steps like a bird
    rot('Bone',Z,look); rot('Bone',X,0.05*math.sin(2*math.pi*3*t))
    rot('Bone.004',X,0.12*max(0,math.sin(2*math.pi*4*t))**8)   # tail flick
    rot('Bone.005',X,0.12*max(0,math.sin(2*math.pi*4*t))**8)
    loc('ROOT',(0,0,0.003*math.sin(2*math.pi*2*t)))
N_FLY=22
def flyc(f): apply(fly_pose(34*f/N_FLY))
H=None
def glide(f):
    t=f/60; base=fly_pose(0); apply(blend(base,base,0))
    reset(); 
    for n,(l,q) in base.items(): P[n].location=l
    for s in 'LR':
        w=WING[s]; sx=SX[s]
        rot(w[0],Y,-sx*(0.08+0.04*math.sin(2*math.pi*t)))
        rot(w[4],Y,-sx*0.12); rot(w[5],Y,-sx*0.1)
    rot('ROOT',Y,0.08*math.sin(2*math.pi*t)); rot('Bone.005',Z,0.08*math.sin(2*math.pi*t))
def takeoff(f):
    s=f/24.0
    reset(); fold(); g=snapshot()
    reset(); fold(0.0); 
    if s<0.25:   # crouch, wings start to open
        k=s/0.25; reset(); fold(1-0.6*k); rot('Bone.001',X,0.25*k); loc('ROOT',(0,0,-0.02*k)); return
    fp=fly_pose(34*((s-0.25)/0.75*1.0)%34)
    k=min(1,(s-0.25)/0.35); reset(); fold(0.4*(1-k)); a=snapshot(); apply(blend(a,fp,k))
    P['ROOT'].location=fp['ROOT'][0]*min(1,(s-0.25)/0.75)
clip('Idle_Loop',90,idle); clip('Fly_Loop',N_FLY,flyc); clip('Glide_Loop',60,glide); clip('TakeOff',24,takeoff)
for a in list(bpy.data.actions): bpy.data.actions.remove(a)
arm.animation_data_create()
for name,fr in FRAMES.items():
    act=bpy.data.actions.new(name); act.use_fake_user=True; arm.animation_data.action=act
    for f,snap in enumerate(fr):
        apply(snap)
        for pb in P: pb.keyframe_insert('rotation_quaternion',frame=f); pb.keyframe_insert('location',frame=f)
arm.animation_data.action=None; reset()
bpy.ops.wm.save_as_mainfile(filepath=OUT)
