"""Author the wolf clips on the NewDLC dog rig: IK paws for gaits, FK for the rest, baked to plain bone keys."""
import bpy,sys,math
from mathutils import Vector,Quaternion,Matrix
bpy.ops.wm.open_mainfile(filepath=sys.argv[1]); OUTBLEND=sys.argv[2]
sc=bpy.context.scene; sc.render.fps=30
arm=[o for o in bpy.data.objects if o.type=='ARMATURE'][0]
B=arm.data.bones; P=arm.pose.bones
def ap(name,tail=False): b=B[name]; return (arm.matrix_world@(b.tail_local if tail else b.head_local)).copy()
# legs: (ik bone = middle segment, top bone, foot bone). IK puts the joint above the paw (hock/wrist); the paw segment keeps its angle.
LEGS={'LH':('b_LeftLeg02','b_LeftLeg01','b_LeftFoot01'),'RH':('b_RightLeg02','b_RightLeg01','b_RightFoot01'),
      'LF':('b_LeftForeArm','b_LeftUpperArm','b_LeftHand01'),'RF':('b_RightForeArm','b_RightUpperArm','b_RightHand01')}
PAW={k:ap(v[0],True) for k,v in LEGS.items()}
bpy.context.view_layer.objects.active=arm
T={}
for k,(mid,top,foot) in LEGS.items():
    e=bpy.data.objects.new('ik_'+k,None); sc.collection.objects.link(e); e.location=PAW[k]; T[k]=e
    c=P[mid].constraints.new('IK'); c.target=e; c.chain_count=2; c.name='IK'
def axis_local(name,ax):
    return (B[name].matrix_local.to_3x3().inverted()@Vector(ax)).normalized()
def rot(name,ax,ang):   # rotate bone about an armature-space axis (composed onto current)
    pb=P[name]; pb.rotation_mode='QUATERNION'; pb.rotation_quaternion=Quaternion(axis_local(name,ax),ang)@pb.rotation_quaternion
def loc(name,v): P[name].location=B[name].matrix_local.to_3x3().inverted()@Vector(v)
def reset():
    for pb in P: pb.rotation_mode='QUATERNION'; pb.rotation_quaternion=(1,0,0,0); pb.location=(0,0,0)
    for k in LEGS: T[k].location=PAW[k]; P[LEGS[k][0]].constraints['IK'].influence=1
X,Y,Z=(1,0,0),(0,1,0),(0,0,1)
def key(f):
    for pb in P: pb.keyframe_insert('rotation_quaternion',frame=f); pb.keyframe_insert('location',frame=f)
    for k in LEGS:
        T[k].keyframe_insert('location',frame=f); P[LEGS[k][0]].constraints['IK'].keyframe_insert('influence',frame=f)
def tail(t,amp=0.12,speed=1.0,base=0.0):
    for i,b in enumerate(('b_Tail01','b_Tail02','b_Tail03','b_Tail04')):
        rot(b,Z,amp*math.sin(2*math.pi*(speed*t-0.12*i))); rot(b,X,base*(0.6 if i==0 else 0.3))
def step(k,ph,stride,lift,duty):
    """paw on a treadmill: stance (ph<duty) slides back, swing arcs forward. ph in [0,1)"""
    if ph<duty: s=ph/duty; dy=-stride/2+stride*s; dz=0; flex=0
    else: s=(ph-duty)/(1-duty); dy=stride/2-stride*(0.5-0.5*math.cos(math.pi*s)); dz=lift*math.sin(math.pi*s); flex=math.sin(math.pi*s)
    T[k].location=PAW[k]+Vector((0,dy,dz))
    rot(LEGS[k][2],X,(0.9 if k[1]=='F' else -0.6)*flex)   # paw folds during the swing (wrist back, hock forward)
BAKED={}
def clip(name,n,fn,loop=True):
    frames=[]
    for f in range(n+1):
        reset(); fn(f/n if loop else f, f); bpy.context.view_layer.update()
        frames.append({pb.name:arm.convert_space(pose_bone=pb,matrix=pb.matrix,from_space='POSE',to_space='LOCAL').copy() for pb in P})
    if loop: frames[-1]=frames[0]
    BAKED[name]=frames; print('sampled',name,n)
def write_actions():
    for pb in P:
        for c in list(pb.constraints): pb.constraints.remove(c)
    arm.animation_data_create()
    for name,frames in BAKED.items():
        act=bpy.data.actions.new(name); act.use_fake_user=True; arm.animation_data.action=act
        for f,fr in enumerate(frames):
            for pb in P:
                l,r,_=fr[pb.name].decompose(); pb.location=l; pb.rotation_quaternion=r
                pb.keyframe_insert('rotation_quaternion',frame=f); pb.keyframe_insert('location',frame=f)
        print('wrote',name)
    arm.animation_data.action=None
def idle(t,f):
    br=math.sin(2*math.pi*2*t)            # 2 breaths per 3 s loop
    loc('b_Root',(0,0,0.004*br)); rot('b_Spine02',X,0.015*br); rot('b_Spine03',X,-0.01*br)
    look=math.sin(2*math.pi*t)
    rot('b_Neck',Z,0.25*look); rot('b_Head',Z,0.1*look); rot('b_Head',X,0.05*math.sin(2*math.pi*2*t+1))
    rot('b_Jaw',X,-0.08*max(0,math.sin(2*math.pi*3*t))**4)   # panting a little
    tail(t,0.10,2,0.15)
GAIT={'LH':0.0,'LF':0.25,'RH':0.5,'RF':0.75}
def walk(t,f):
    for k,o in GAIT.items(): step(k,(t+o)%1,0.42,0.09,0.62)
    b=math.sin(2*math.pi*2*t); loc('b_Root',(0,0,-0.01+0.012*b))
    rot('b_Hip',Z,0.06*math.sin(2*math.pi*t)); rot('b_Spine03',Z,-0.06*math.sin(2*math.pi*t))
    rot('b_Neck',X,0.05*math.sin(2*math.pi*2*t+0.8)); rot('b_Neck',X,0.12)
    tail(t,0.12,1,0.25)
RUN={'LH':0.0,'RH':0.1,'LF':0.5,'RF':0.6}
def run(t,f):
    for k,o in RUN.items(): step(k,(t+o)%1,0.95,0.18,0.38)
    flex=math.sin(2*math.pi*t)
    loc('b_Root',(0,0,0.02+0.04*math.sin(2*math.pi*t+1.2)))
    rot('b_Hip',X,0.18*flex); rot('b_Spine01',X,-0.10*flex); rot('b_Spine02',X,-0.08*flex); rot('b_Spine03',X,0.06*flex)
    rot('b_Neck',X,0.3+0.12*math.sin(2*math.pi*t+2.0)); rot('b_Head',X,-0.15)
    rot('b_Jaw',X,-0.15)
    for i,b in enumerate(('b_Tail01','b_Tail02','b_Tail03','b_Tail04')): rot(b,X,-0.1+0.12*math.sin(2*math.pi*t-0.5*i))
def sm(a,b,x): x=min(1,max(0,(x-a)/(b-a))); return x*x*(3-2*x)
def attack(t,f):   # 24 frames: crouch, lunge + bite, recover
    s=f/30
    crouch=sm(0,0.25,s)*(1-sm(0.25,0.4,s)); lunge=sm(0.25,0.38,s)*(1-sm(0.5,0.8,s))
    loc('b_Root',(0,-0.28*lunge+0.06*crouch,-0.09*crouch+0.03*lunge))
    for k in ('LF','RF'): T[k].location=PAW[k]+Vector((0,-0.22*lunge,0.12*math.sin(math.pi*sm(0.25,0.45,s))*(1-sm(0.45,0.6,s))))
    rot('b_Spine03',X,0.12*crouch-0.08*lunge); rot('b_Neck',X,0.35*crouch-0.25*lunge); rot('b_Head',X,-0.25*crouch+0.15*lunge)
    jaw=sm(0.15,0.32,s)*(1-sm(0.40,0.46,s)); rot('b_Jaw',X,-0.55*jaw)
    tail(s,0.05,1,-0.1*crouch)
def hit(t,f):      # 12 frames: jolt back and to the side, flinch
    s=f/30; j=math.sin(math.pi*sm(0,0.4,s))*math.exp(-3*s)
    k=sm(0,0.06,s)*(1-sm(0.06,0.4,s))
    loc('b_Root',(0.03*k,0.08*k,-0.04*k))
    rot('b_Spine02',Z,0.12*k); rot('b_Neck',Z,-0.35*k); rot('b_Neck',X,0.25*k); rot('b_Head',X,-0.2*k); rot('b_Jaw',X,-0.35*k)
    tail(s,0.0,1,-0.3*k)
def death(t,f):    # 45 frames: legs give, fall on the right side, last twitch, hold
    s=f/30
    give=sm(0,0.35,s); fall=sm(0.2,0.8,s)
    for k in LEGS: P[LEGS[k][0]].constraints['IK'].influence=1-sm(0.15,0.35,s)
    roll=1.3*fall
    loc('b_Root',(-0.12*fall,0,-0.18*give*(1-fall)+0.07*fall))
    P['b_Root'].rotation_quaternion=Quaternion(axis_local('b_Root',Y),-roll)
    for leg in ('b_LeftLeg01','b_RightLeg01'): rot(leg,X,-0.4*fall)
    for leg in ('b_LeftLeg02','b_RightLeg02'): rot(leg,X,0.3*fall)
    for leg in ('b_LeftUpperArm','b_RightUpperArm'): rot(leg,X,-0.5*fall)
    for leg in ('b_LeftForeArm','b_RightForeArm'): rot(leg,X,0.4*fall)
    for leg in ('b_LeftHand01','b_RightHand01'): rot(leg,X,0.5*fall)
    for leg in ('b_LeftFoot01','b_RightFoot01'): rot(leg,X,-0.4*fall)
    tw=math.exp(-6*max(0,s-0.95))*math.sin(30*max(0,s-0.95))*(s>0.95)
    rot('b_Neck',X,0.2*give-0.3*fall); rot('b_Neck',Z,0.4*fall); rot('b_Head',X,-0.2*fall+0.08*tw); rot('b_Jaw',X,-0.25*fall)
    tail(s,0,1,-0.4*fall)
clip('Idle_Loop',90,idle); clip('Walk_Loop',30,walk); clip('Run_Loop',18,run)
clip('Attack_Bite',24,attack,loop=False); clip('Hit',12,hit,loop=False); clip('Death',45,death,loop=False)
write_actions()
for k in list(T): bpy.data.objects.remove(T[k])
bpy.ops.wm.save_as_mainfile(filepath=OUTBLEND)
