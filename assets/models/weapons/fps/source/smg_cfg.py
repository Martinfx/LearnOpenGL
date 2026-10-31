import sys; sys.path.insert(0,'/tmp/tools')
from fp_gun import *
S=0.097
def load():
    obs=append_blend('/tmp/dl/a_smg_blend/smg.blend')
    M=Matrix.Scale(S,4)@Matrix.Rotation(math.radians(90),4,'Z')@Matrix.Translation((-0.22,-0.25,2.9))  # grip -> origin
    for o in obs:
        if o.parent is None: o.matrix_world=M@o.matrix_world
    m1=bpy.data.materials['Material.001']; p=m1.node_tree.nodes['Principled BSDF']
    p.inputs['Base Color'].default_value=(0.16,0.17,0.13,1); p.inputs['Roughness'].default_value=0.55; p.inputs['Metallic'].default_value=0.3
    bpy.context.view_layer.update(); return obs
MAGP=['Plane.063','Plane.064','Plane.065','Plane.066','Plane.067','Plane.069']
def g(local): return [round(x,4) for x in (Matrix.Scale(S,4)@Matrix.Rotation(math.radians(90),4,'Z')@Matrix.Translation((-0.22,-0.25,2.9))@Vector(local))]
C=dict(load=load,wpos=[0.13,0.38,-0.27],grip=[0,0,0],fore=g((0.22,-1.9,-2.3)),
 bones={'magazine':Vector(g((0.22,1.1,-3.0)))},groups={n:'magazine' for n in MAGP},
 rofs=[0.035,-0.06,-0.01],rfwd=[-0.25,1,-0.35],rup=[-1,0,0],rcurl=1.4,rthumb=0.4,rindex=0.6,
 lofs=[-0.035,-0.05,-0.01],lfwd=[0.25,1,-0.35],lup=[1,0,0],lcurl=1.3,lthumb=0.4,
 anim=dict(fire_len=4,kick=0.018,pitch=3.5,slide=0.0,reload_len=75,rpos=(-0.04,0.02,0.02),rrot=(20,12,-28),mag_out=(0,-0.01,-0.08),hold=(0,-0.03,-0.07),charge=False))
if __name__=='__main__':
    if sys.argv[1]=='preview': build(C,None,sys.argv[2:])
    else: build(C,sys.argv[1])
