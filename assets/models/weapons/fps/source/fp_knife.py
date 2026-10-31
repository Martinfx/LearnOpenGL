import sys,os; sys.path.insert(0,'/tmp/tools')
from fp_gun import *
TEX='/tmp/dl/k_m9_knife_zip/Textures/'
S=0.026
def load():
    with bpy.data.libraries.load('/tmp/dl/k_m9_knife_zip/m9_knife.blend',link=False) as (src,dst): dst.objects=[n for n in src.objects if n=='M9 Knife']
    o=dst.objects[0]; bpy.context.scene.collection.objects.link(o)
    o.parent=None
    for slot in o.material_slots:
        part='Knife' if slot.material.name.startswith('Knife') else 'Handle'
        m=bpy.data.materials.new('M9_'+part); m.use_nodes=True; nt=m.node_tree; p=nt.nodes['Principled BSDF']
        def img(name,col):
            n=nt.nodes.new('ShaderNodeTexImage'); n.image=bpy.data.images.load(TEX+f'{part}_{name}.png'); 
            if not col: n.image.colorspace_settings.name='Non-Color'
            return n
        nt.links.new(img('albedo',True).outputs[0],p.inputs['Base Color'])
        nt.links.new(img('roughness',False).outputs[0],p.inputs['Roughness'])
        nt.links.new(img('metallic',False).outputs[0],p.inputs['Metallic'])
        nm=nt.nodes.new('ShaderNodeNormalMap'); nt.links.new(img('nm',False).outputs[0],nm.inputs['Color']); nt.links.new(nm.outputs[0],p.inputs['Normal'])
        slot.material=m
    M=Matrix.Rotation(math.radians(-40),4,'Y')@Matrix.Scale(S,4)@Matrix.Translation((3.3,0,-0.4))
    o.matrix_world=M@o.matrix_world
    bpy.context.view_layer.update(); return [o]
C=dict(wpos=[0.17,0.36,-0.13],grip=[0,0,0],rofs=[0.035,-0.04,0.05],rfwd=[-0.1,0.64,-0.77],rup=[-1,0,0],rcurl=1.55,rthumb=0.7,rindex=1.5)
def build_knife(out,preview=None):
    arm,body=base(); fix_arm_material(body); dress(arm,body)
    objs=load(); W=weapon_world(C['wpos'])
    for o in objs: o.matrix_world=W@o.matrix_world
    bpy.context.view_layer.update()
    grip=W@Vector(C['grip'])
    add_bones(arm,Matrix.Translation(grip),{})
    skin_weapon(arm,objs,{})
    set_hand(arm,'R',grip+Vector(C['rofs']),C['rfwd'],C['rup'])
    pb=arm.pose.bones; CR=pb['hand.R.control'].matrix.copy(); G0=Matrix.Translation(grip); Rloc=G0.inverted()@CR
    LOW=Matrix.Translation((-0.25,0.1,-0.75))@eul(40,0,0)
    def pose(wd=Matrix.Identity(4)):
        Wm=G0@wd; pb['weapon'].matrix=Wm; bpy.context.view_layer.update()
        pb['hand.R.control'].matrix=Wm@Rloc; pb['hand.L.control'].matrix=LOW; bpy.context.view_layer.update()
        curl(arm,'R',C['rcurl'],C['rthumb'],C['rindex']); curl(arm,'L',0.4,0.2); bpy.context.view_layer.update()
    A=Anim(arm)
    def idle(f,t):
        s=2*math.pi*t/2.0; pose(wdelta((0.002*math.sin(s),0,0.004*math.sin(s*2)),pitch=0.8*math.sin(s),yaw=0.6*math.cos(s)))
    A.action('idle',60,idle)
    def slash(f,t):
        a=seg(t,0,0.12); b=seg(t,0.12,0.26); c=seg(t,0.3,0.6)
        wind=a*(1-b); sw=b*(1-c)
        pose(wdelta((0.05*wind-0.28*sw, 0.02*wind+0.10*sw, 0.06*wind-0.10*sw),pitch=10*wind-15*sw,yaw=-15*wind+55*sw,roll=25*wind-70*sw))
    A.action('slash',18,slash)
    def stab(f,t):
        a=seg(t,0,0.1); b=seg(t,0.1,0.2); c=seg(t,0.25,0.5)
        back=a*(1-b); fwd=b*(1-c)
        pose(wdelta((-0.04*fwd,-0.05*back+0.18*fwd,0.01*back+0.05*fwd),pitch=-5*back-25*fwd,yaw=5*fwd,roll=0))
    A.action('stab',15,stab)
    def draw(f,t):
        k=1-seg(t,0,0.4); pose(wdelta((0.03*k,-0.05*k,-0.3*k),pitch=-60*k,roll=-30*k))
    A.action('draw',12,draw)
    def holster(f,t):
        k=seg(t,0,0.3); pose(wdelta((0.03*k,-0.05*k,-0.3*k),pitch=-60*k,roll=-30*k))
    A.action('holster',10,holster)
    arm.animation_data.action=bpy.data.actions['idle']; bpy.context.scene.frame_set(0)
    if preview:
        cam=camera(60); bpy.context.scene.render.resolution_x=960; bpy.context.scene.render.resolution_y=540; outs=[]
        for spec in preview:
            an,fr,v=(spec.split(':')+['eye'])[:3]; arm.animation_data.action=bpy.data.actions[an]
            loc,rot=VIEWS[v]; cam.location=loc; cam.rotation_euler=[math.radians(r) for r in rot]
            p=f'/tmp/prev/k_{an}{fr}_{v}.png'; render(p,int(fr)); outs.append(p)
        print('OUT',' '.join(outs)); return
    for o in list(bpy.data.objects):
        if o.type in('CAMERA','LIGHT'): bpy.data.objects.remove(o)
    export(out)
if __name__=='__main__':
    if sys.argv[1]=='preview': build_knife(None,sys.argv[2:])
    else: build_knife(sys.argv[1])
