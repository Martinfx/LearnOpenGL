import sys; sys.path.insert(0,'/tmp/tools')
from fp_gun import *
def load():
    objs=load_weapon('/tmp/out/ph_models/service_pistol/service_pistol.gltf',{'service_pistol_pistol_a','service_pistol_slide_a','service_pistol_trigger_a','service_pistol_hammer_a','service_pistol_magazine_loaded'},'service_pistol_magazine_loaded',(0.085,0,0))
    for o in objs:
        if o.parent is None: o.matrix_world=Matrix.Translation((0.02,0,0.02))@o.matrix_world  # grip centre -> origin
    bpy.context.view_layer.update(); return objs
C=dict(load=load,wpos=[0.10,0.30,-0.12],grip=[0,0,0],fore=[0,0,0],
 bones={'magazine':Vector((0.0055,0,0.02)),'slide':Vector((0.055,0,0.082))},
 groups={'service_pistol_magazine_loaded':'magazine','service_pistol_slide_a':'slide'},
 rofs=[0.035,-0.06,-0.015],rfwd=[-0.2,1,0.1],rup=[-1,0,0],rcurl=1.45,rthumb=0.5,rindex=0.45,
 lofs=[-0.04,-0.05,-0.035],lfwd=[0.2,1,0.3],lup=[0.7,0,0.7],lcurl=1.3,lthumb=0.3,
 anim=dict(fire_len=8,kick=0.022,pitch=6,slide=0.025,reload_len=60,rpos=(-0.04,0.03,0.0),rrot=(30,15,-25),mag_out=(0,-0.01,-0.07),hold=(0,-0.035,-0.06),charge=False))
if __name__=='__main__':
    if sys.argv[1]=='preview': build(C,None,sys.argv[2:])
    else: build(C,sys.argv[1])
