# Retarget Quaternius UAL clips (CC0) onto a Microsoft Rocketbox (MIT) Biped character.
import bpy,sys,os,math,json
from mathutils import Matrix,Vector,Quaternion
src_ual,src_fbx,out=sys.argv[1],sys.argv[2],sys.argv[3]
clips=sys.argv[4].split(',') if len(sys.argv)>4 else None
bpy.ops.wm.read_factory_settings(use_empty=True)
sc=bpy.context.scene; sc.render.fps=30
bpy.ops.import_scene.gltf(filepath=src_ual)
U=[o for o in bpy.data.objects if o.type=='ARMATURE'][0]; U.name='UAL'
ual_objs=[o for o in bpy.data.objects]
ual_actions=list(bpy.data.actions)
bpy.ops.import_scene.fbx(filepath=src_fbx)
B=[o for o in bpy.data.objects if o.type=='ARMATURE' and o!=U][0]
meshes=[o for o in bpy.data.objects if o.type=='MESH' and o not in ual_objs]
# textures next to Export/
td=os.path.join(os.path.dirname(os.path.dirname(src_fbx)),'Textures')
for im in bpy.data.images:
    p=os.path.join(td,os.path.basename(im.filepath))
    if os.path.exists(p): im.filepath=p; im.reload()
for a in list(bpy.data.actions):
    if a not in ual_actions: bpy.data.actions.remove(a)
# bake B object transform into data
A=B.matrix_world.copy()
for m in meshes:
    mw=m.matrix_world.copy(); m.parent=None; m.data.transform(mw); m.matrix_world=Matrix()
B.data.transform(A); B.matrix_world=Matrix()
for m in meshes:
    for md in m.modifiers:
        if md.type=='ARMATURE': md.object=B
    m.parent=B
# normalise scale: bone matrices could carry scale; check
P=lambda n:'Bip01 '+n
MAP={'DEF-hips':P('Pelvis'),'DEF-spine.001':P('Spine'),'DEF-spine.002':P('Spine1'),'DEF-spine.003':P('Spine2'),
 'DEF-neck':P('Neck'),'DEF-head':P('Head')}
for s,S in (('L','L'),('R','R')):
    MAP.update({f'DEF-shoulder.{s}':P(f'{S} Clavicle'),f'DEF-upper_arm.{s}':P(f'{S} UpperArm'),f'DEF-forearm.{s}':P(f'{S} Forearm'),
      f'DEF-hand.{s}':P(f'{S} Hand'),f'DEF-thigh.{s}':P(f'{S} Thigh'),f'DEF-shin.{s}':P(f'{S} Calf'),f'DEF-foot.{s}':P(f'{S} Foot'),f'DEF-toe.{s}':P(f'{S} Toe0')})
    for fu,fb in (('thumb',0),('f_index',1),('f_middle',2),('f_ring',3),('f_pinky',4)):
        for i,suf in ((1,''),(2,'1'),(3,'2')):
            ub=f'DEF-{fu}.0{i}.{s}' if fu!='thumb' else f'DEF-thumb.0{i}.{s}'
            MAP[ub]=P(f'{S} Finger{fb}{"" if i==1 else ("1" if i==2 else "2")}')
            if fb==0: MAP[ub]=P(f'{S} Finger0{"" if i==1 else ("1" if i==2 else "2")}')
MAP={u:b for u,b in MAP.items() if u in U.data.bones and b in B.data.bones}
print('mapped',len(MAP))
# direction "aim" per bone: child mapped head (both rigs), so rest alignment matches
def aim_child(ub):
    pref={'DEF-hips':'DEF-spine.001','DEF-hand.L':'DEF-f_middle.01.L','DEF-hand.R':'DEF-f_middle.01.R','DEF-spine.003':'DEF-neck','DEF-shoulder.L':'DEF-upper_arm.L','DEF-shoulder.R':'DEF-upper_arm.R','DEF-thigh.L':'DEF-shin.L','DEF-thigh.R':'DEF-shin.R'}
    if ub in pref: return pref[ub]
    for c in U.data.bones[ub].children:
        if c.name in MAP: return c.name
    return None
Urest={b.name:b.matrix_local.copy() for b in U.data.bones}
Brest={b.name:b.matrix_local.copy() for b in B.data.bones}
Q={}
for ub,bb in MAP.items():
    c=aim_child(ub)
    if c is None: Q[ub]=None; continue
    dU=(Urest[c].translation-Urest[ub].translation).normalized()
    dB=(Brest[MAP[c]].translation-Brest[bb].translation).normalized()
    Q[ub]=dB.rotation_difference(dU)
# bones without aim inherit the parent's alignment
def getQ(ub):
    q=Q.get(ub)
    if q is not None: return q
    p=U.data.bones[ub].parent
    while p is not None and p.name not in MAP: p=p.parent
    return getQ(p.name) if p else Quaternion()
inv={b:u for u,b in MAP.items()}
order=[b for b in B.data.bones]  # bpy lists parents before children
def depth(b):
    d=0
    while b.parent: b=b.parent; d+=1
    return d
order.sort(key=depth)
hipsU0=Urest['DEF-hips'].translation; hipsB0=Brest[P('Pelvis')].translation
ratio=hipsB0.z/hipsU0.z
print('ratio',ratio)
for pb in B.pose.bones: pb.rotation_mode='QUATERNION'
names=[a.name for a in ual_actions] if not clips else clips
newacts=[]
for an in names:
    act=bpy.data.actions[an]
    U.animation_data.action=act
    try: U.animation_data.action_slot=act.slots[0]
    except: pass
    f0,f1=[int(round(x)) for x in act.frame_range]
    nb=bpy.data.actions.new('RT_'+an); nb.use_fake_user=True; newacts.append((nb,an))
    B.animation_data_create(); B.animation_data.action=nb
    frames={}
    for f in range(f0,f1+1):
        sc.frame_set(f)
        Mw={}
        res={}
        for b in order:
            n=b.name
            Rb=Brest[n]
            if b.parent:
                Mp=Mw[b.parent.name]; Rp=Brest[b.parent.name]
                base=Mp@Rp.inverted()@Rb
            else:
                base=Rb.copy()
            if n in inv:
                ub=inv[n]
                Wu=U.pose.bones[ub].matrix.to_quaternion(); Wu0=Urest[ub].to_quaternion()
                D=Wu@Wu0.inverted()
                rot=(D@getQ(ub)@Rb.to_quaternion()).to_matrix().to_4x4()
                pos=base.translation.copy()
                if ub=='DEF-hips':
                    pos=hipsB0+ (U.pose.bones[ub].head-hipsU0)*ratio
                M=Matrix.Translation(pos)@rot
                L=base.inverted()@M if not b.parent else (Mw[b.parent.name]@Brest[b.parent.name].inverted()@Rb).inverted()@M
                res[n]=L
                Mw[n]=M
            else:
                Mw[n]=base
        frames[f]=res
    for n in inv.keys():
        pb=B.pose.bones[n]
        dp=f'pose.bones["{n}"]'
        for f,res in frames.items():
            L=res[n]
            pb.rotation_quaternion=L.to_quaternion(); pb.keyframe_insert('rotation_quaternion',frame=f,group=n)
            if n==P('Pelvis'):
                pb.location=L.translation; pb.keyframe_insert('location',frame=f,group=n)
    print('clip',an,f1-f0)
# drop UAL rig
for o in ual_objs: bpy.data.objects.remove(o)
for a in ual_actions: bpy.data.actions.remove(a)
for nb,an in newacts: nb.name=an
B.animation_data.action=newacts[0][0]
for pb in B.pose.bones: pb.rotation_quaternion=Quaternion(); pb.location=(0,0,0)
for im in bpy.data.images:
    if im.size[0]>2048: im.scale(2048,2048*im.size[1]//im.size[0])
bpy.ops.export_scene.gltf(filepath=out,export_format='GLB',export_animations=True,export_animation_mode='ACTIONS',
    export_force_sampling=True,export_image_format='JPEG',export_jpeg_quality=88,export_yup=True,export_skins=True,export_def_bones=False)
