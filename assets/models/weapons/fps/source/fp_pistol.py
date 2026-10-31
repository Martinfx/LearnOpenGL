import sys,json; sys.path.insert(0,'/tmp/tools')
from fp_build import *
P=json.loads(open('/tmp/p1.json').read())
arm,body=base(); fix_arm_material(body)
objs=load_weapon('/tmp/out/ph_models/service_pistol/service_pistol.gltf',{'service_pistol_pistol_a','service_pistol_slide_a','service_pistol_trigger_a','service_pistol_hammer_a','service_pistol_magazine_loaded'},'service_pistol_magazine_loaded',(0.085,0,0))
W=weapon_world(P['wpos'])
for o in objs:
    if o.parent is None: o.matrix_world=W@o.matrix_world
bpy.context.view_layer.update()
grip=W@Vector(P['grip']); magc=W@Vector((-0.0995+0.085,0,0.0)); slide=W@Vector((0.035,0,0.062))
add_bones(arm,Matrix.Translation(grip),{'magazine':magc,'slide':slide})
wpn=skin_weapon(arm,objs,{'service_pistol_magazine_loaded':'magazine','service_pistol_slide_a':'slide'})
set_hand(arm,'R',grip+Vector(P['rofs']),P['rfwd'],P['rup'])
set_hand(arm,'L',grip+Vector(P['lofs']),P['lfwd'],P['lup'])
pb=arm.pose.bones; CR=pb['hand.R.control'].matrix.copy(); CL=pb['hand.L.control'].matrix.copy()
G0=Matrix.Translation(grip); Lloc=G0.inverted()@CL; Rloc=G0.inverted()@CR
MAG0=arm.data.bones['magazine'].matrix_local.copy(); SL0=arm.data.bones['slide'].matrix_local.copy()
def pose(wd=Matrix.Identity(4),L=None,mag=None,slide_d=0.0,trig=None):
    Wm=G0@wd; pb['weapon'].matrix=Wm; bpy.context.view_layer.update()
    pb['hand.R.control'].matrix=Wm@Rloc
    pb['hand.L.control'].matrix=L if L is not None else Wm@Lloc
    bpy.context.view_layer.update()
    pb['magazine'].matrix=mag if mag is not None else Wm@G0.inverted()@MAG0
    pb['slide'].matrix=Wm@G0.inverted()@SL0@Matrix.Translation((0,slide_d,0))
    curl(arm,'R',P['rcurl'],P['rthumb'],P['rindex'] if trig is None else trig); curl(arm,'L',P['lcurl'],P['lthumb'])
    bpy.context.view_layer.update()
def wdelta(pos=(0,0,0),pitch=0,yaw=0,roll=0): return Matrix.Translation(pos)@eul(pitch,yaw,roll)
def mlerp(A,B,k):
    t=A.translation.lerp(B.translation,k); q=A.to_quaternion().slerp(B.to_quaternion(),k); m=q.to_matrix().to_4x4(); m.translation=t; return m
A=Anim(arm)
RX,RY,RZ,RP,RW,RR=[float(x) for x in (sys.argv[2:8] if len(sys.argv)>7 else (-0.04,0.03,0.0,30,15,-25))]
def idle(f,t):
    s=2*math.pi*t/2.0; pose(wdelta((0.0015*math.sin(s),0,0.003*math.sin(s*2)),pitch=0.5*math.sin(s),yaw=0.4*math.cos(s)))
A.action('idle',60,idle)
def fire(f,t):
    k=seg(t,0,0.05)-seg(t,0.06,0.25); pose(wdelta((0,-0.035*k,0.01*k),pitch=9*k),slide_d=-0.025*(seg(t,0,0.04)-seg(t,0.05,0.12)),trig=P['rindex']+0.5*(1-seg(t,0.05,0.2)))
A.action('fire',8,fire)
BELT=Matrix.Translation((-0.08,0.15,-0.5))@eul(30,0,0)
def reload(f,t):
    tilt=seg(t,0,0.3)-seg(t,1.55,1.95)
    wd=wdelta((RX*tilt,RY*tilt,RZ*tilt),pitch=RP*tilt,yaw=RW*tilt,roll=RR*tilt)
    Wm=G0@wd; support=Wm@Lloc
    magin=Wm@G0.inverted()@MAG0
    below=magin@Matrix.Translation((0,-0.01,-0.07))
    hold_off=Matrix.Translation((0,-0.035,-0.06))@eul(-10,0,0)  # hand relative to mag
    hold=lambda M: M@hold_off@(G0.inverted()@CL).to_3x3().to_4x4() if False else Matrix.Translation((M@hold_off).translation)@support.to_3x3().to_4x4()
    mag=None; L=support
    if t<0.27: pass
    elif t<0.5:
        mag=magin@Matrix.Translation((0,-0.04*seg(t,0.27,0.5),-0.45*seg(t,0.27,0.5)**2))
    if 0.3<=t<0.75: L=mlerp(support,BELT,seg(t,0.3,0.7))
    if t>=0.5 and t<0.75: mag=Matrix.Translation((0,0,-2))
    if 0.75<=t<1.15:
        k=seg(t,0.75,1.15); M=mlerp(BELT@Matrix.Translation((0,0.06,0.08)),below,k); mag=M; L=hold(M)
    if 1.15<=t<1.3:
        k=seg(t,1.15,1.28); M=mlerp(below,magin,k); mag=M; L=hold(M)
    if 1.3<=t<1.6:
        k=seg(t,1.3,1.6); L=mlerp(hold(magin),support,k)
    bump=0.01*(seg(t,1.25,1.29)-seg(t,1.3,1.45))
    pose(wd@Matrix.Translation((0,0,bump)),L=L,mag=mag)
A.action('reload',60,reload)
def draw(f,t):
    k=1-seg(t,0,0.45); pose(wdelta((0.02*k,-0.05*k,-0.28*k),pitch=-55*k,roll=-20*k))
A.action('draw',15,draw)
def holster(f,t):
    k=seg(t,0,0.35); pose(wdelta((0.02*k,-0.05*k,-0.28*k),pitch=-55*k,roll=-20*k))
A.action('holster',12,holster)
arm.animation_data.action=bpy.data.actions['idle']
bpy.context.scene.frame_set(0)
if len(sys.argv)>1 and sys.argv[1]=='preview':
    camera(); bpy.context.scene.render.resolution_x=1280; bpy.context.scene.render.resolution_y=720
    for an,fr in [('idle',0),('reload',14),('reload',36),('draw',5)]:
        arm.animation_data.action=bpy.data.actions[an]; render(f'/tmp/prev/pr_{an}_{fr}.png',fr)
    cam=bpy.context.scene.camera; cam.location=(0.55,0.25,0.05); cam.rotation_euler=(math.radians(95),0,math.radians(90))
    arm.animation_data.action=bpy.data.actions['reload']; render('/tmp/prev/pr_side30.png',30)
else:
    for o in list(bpy.data.objects):
        if o.type in('CAMERA','LIGHT'): bpy.data.objects.remove(o)
    export(sys.argv[1])
