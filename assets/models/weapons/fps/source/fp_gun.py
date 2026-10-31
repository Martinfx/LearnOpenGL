import sys,json; sys.path.insert(0,'/tmp/tools')
from fp_build import *
def append_blend(path):
    before=set(bpy.data.objects)
    with bpy.data.libraries.load(path,link=False) as (src,dst): dst.objects=src.objects
    for o in dst.objects:
        if o is not None: bpy.context.scene.collection.objects.link(o)
    bpy.context.view_layer.update()
    dg=bpy.context.evaluated_depsgraph_get(); res=[]
    for o in [o for o in dst.objects if o is not None and o.type=='MESH']:
        me=bpy.data.meshes.new_from_object(o.evaluated_get(dg),preserve_all_data_layers=True,depsgraph=dg)
        n=bpy.data.objects.new(o.name+'_e',me); bpy.context.scene.collection.objects.link(n); n.matrix_world=o.matrix_world.copy(); res.append((o.name,n))
    for o in [o for o in dst.objects if o is not None]: bpy.data.objects.remove(o)
    for name,n in res: n.name=name
    return [n for _,n in res]
def mlerp(A,B,k):
    t=A.translation.lerp(B.translation,k); q=A.to_quaternion().slerp(B.to_quaternion(),k); m=q.to_matrix().to_4x4(); m.translation=t; return m
def wdelta(pos=(0,0,0),pitch=0,yaw=0,roll=0): return Matrix.Translation(pos)@eul(pitch,yaw,roll)
def build(C,out,preview=None):
    arm,body=base(); fix_arm_material(body)
    objs=C['load']()
    # objs are in weapon-local frame (barrel +X, up +Z, metres)
    W=weapon_world(C['wpos'])
    for o in objs:
        if o.parent is None: o.matrix_world=W@o.matrix_world
    bpy.context.view_layer.update()
    grip=W@Vector(C['grip']); fore=W@Vector(C['fore'])
    extra={k:W@Vector(v) for k,v in C['bones'].items()}
    add_bones(arm,Matrix.Translation(grip),extra)
    skin_weapon(arm,objs,C['groups'])
    set_hand(arm,'R',grip+Vector(C['rofs']),C['rfwd'],C['rup'])
    set_hand(arm,'L',fore+Vector(C['lofs']),C['lfwd'],C['lup'])
    pb=arm.pose.bones; CR=pb['hand.R.control'].matrix.copy(); CL=pb['hand.L.control'].matrix.copy()
    G0=Matrix.Translation(grip); Lloc=G0.inverted()@CL; Rloc=G0.inverted()@CR
    REST={k:arm.data.bones[k].matrix_local.copy() for k in C['bones']}
    def pose(wd=Matrix.Identity(4),L=None,mag=None,slide_d=0.0,trig=None):
        Wm=G0@wd; pb['weapon'].matrix=Wm; bpy.context.view_layer.update()
        pb['hand.R.control'].matrix=Wm@Rloc
        pb['hand.L.control'].matrix=L if L is not None else Wm@Lloc
        bpy.context.view_layer.update()
        if 'magazine' in REST: pb['magazine'].matrix=mag if mag is not None else Wm@G0.inverted()@REST['magazine']
        if 'slide' in REST: pb['slide'].matrix=Wm@G0.inverted()@REST['slide']@Matrix.Translation((0,slide_d,0))
        curl(arm,'R',C['rcurl'],C['rthumb'],C['rindex'] if trig is None else trig); curl(arm,'L',C['lcurl'],C['lthumb'])
        bpy.context.view_layer.update()
    A=Anim(arm); K=C['anim']
    def idle(f,t):
        s=2*math.pi*t/2.0; pose(wdelta((0.0015*math.sin(s),0,0.003*math.sin(s*2)),pitch=0.5*math.sin(s),yaw=0.4*math.cos(s)))
    A.action('idle',60,idle)
    def fire(f,t):
        d=K['fire_len']/FPS; k=seg(t,0,d*0.25)-seg(t,d*0.3,d)
        pose(wdelta((0,-K['kick']*k,0.006*k),pitch=K['pitch']*k),slide_d=-K['slide']*(seg(t,0,d*0.2)-seg(t,d*0.25,d*0.6)),trig=C['rindex']+0.5*(1-seg(t,d*0.3,d)))
    A.action('fire',K['fire_len'],fire)
    BELT=Matrix.Translation((-0.08,0.15,-0.5))@eul(30,0,0)
    T=K['reload_len']/FPS; sc=T/2.0
    def reload(f,t):
        u=t/sc  # normalised to the 2 s pistol timeline
        tilt=seg(u,0,0.3)-seg(u,1.55,1.95)
        wd=wdelta(tuple(v*tilt for v in K['rpos']),pitch=K['rrot'][0]*tilt,yaw=K['rrot'][1]*tilt,roll=K['rrot'][2]*tilt)
        Wm=G0@wd; support=Wm@Lloc
        magin=Wm@G0.inverted()@REST['magazine']
        below=magin@Matrix.Translation(K['mag_out'])
        hold_off=Matrix.Translation(K['hold'])
        hold=lambda M: Matrix.Translation((M@hold_off).translation)@support.to_3x3().to_4x4()
        mag=None; L=support
        if 0.27<=u<0.5: k=seg(u,0.27,0.5); mag=magin@Matrix.Translation((0,K['mag_out'][1]*k*0.6,-0.45*k*k))
        if 0.3<=u<0.75: L=mlerp(support,BELT,seg(u,0.3,0.7))
        if 0.5<=u<0.75: mag=Matrix.Translation((0,0,-2))
        if 0.75<=u<1.15: k=seg(u,0.75,1.15); M=mlerp(BELT@Matrix.Translation((0,0.06,0.08)),below,k); mag=M; L=hold(M)
        if 1.15<=u<1.3: k=seg(u,1.15,1.28); M=mlerp(below,magin,k); mag=M; L=hold(M)
        if 1.3<=u<1.6: L=mlerp(hold(magin),support,seg(u,1.3,1.6))
        sl=0.0
        if K.get('charge'): sl=-K['slide']*1.6*(seg(u,1.45,1.52)-seg(u,1.55,1.6))
        bump=0.01*(seg(u,1.25,1.29)-seg(u,1.3,1.45))
        pose(wd@Matrix.Translation((0,0,bump)),L=L,mag=mag,slide_d=sl)
    A.action('reload',K['reload_len'],reload)
    def draw(f,t):
        k=1-seg(t,0,0.45); pose(wdelta((0.02*k,-0.05*k,-0.3*k),pitch=-55*k,roll=-20*k))
    A.action('draw',15,draw)
    def holster(f,t):
        k=seg(t,0,0.35); pose(wdelta((0.02*k,-0.05*k,-0.3*k),pitch=-55*k,roll=-20*k))
    A.action('holster',12,holster)
    arm.animation_data.action=bpy.data.actions['idle']; bpy.context.scene.frame_set(0)
    if preview:
        camera(); bpy.context.scene.render.resolution_x=960; bpy.context.scene.render.resolution_y=540
        outs=[]
        for spec in preview:
            an,fr=spec.split(':'); arm.animation_data.action=bpy.data.actions[an]; p=f'/tmp/prev/g_{an}{fr}.png'; render(p,int(fr)); outs.append(p)
        print('OUT',' '.join(outs)); return
    for o in list(bpy.data.objects):
        if o.type in('CAMERA','LIGHT'): bpy.data.objects.remove(o)
    export(out)
