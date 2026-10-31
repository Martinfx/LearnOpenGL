import bpy,math,mathutils
from mathutils import Vector,Euler,Matrix
S=0.12
def base():
    bpy.ops.wm.open_mainfile(filepath="/tmp/dl/a_fps_20arms_7z/FPS ARMS RIG 1.blend",load_ui=False,use_scripts=False)
    for o in list(bpy.data.objects):
        if o.type in('CAMERA','LIGHT'): bpy.data.objects.remove(o)
    arm=bpy.data.objects['caucasian_male_1']; body=bpy.data.objects['caucasian_male_1:Body']
    arm.animation_data_clear() if arm.animation_data else None
    for a in list(bpy.data.actions): bpy.data.actions.remove(a)
    # eye at origin: original eye approx (0,0,1.19) in armature space
    arm.location=Vector((0,0.153,-1.19))*S  # undo object offset then shift
    arm.location=Vector((0, 0.153*S, -1.19*S))
    arm.rotation_euler=(0,0,math.pi); arm.scale=(S,S,S)
    bpy.context.view_layer.update()
    Mb=body.matrix_world.copy(); Ma=arm.matrix_world.copy()
    body.data.transform(Mb); arm.data.transform(Ma)
    arm.matrix_world=Matrix.Identity(4); body.matrix_parent_inverse=Matrix.Identity(4); body.matrix_basis=Matrix.Identity(4)
    bpy.context.view_layer.update()
    arm.name='FP_Arms'; arm.data.name='FP_Arms'; body.name='Arms'
    bpy.context.scene.render.fps=30
    return arm,body
def camera(fov=70):
    sc=bpy.context.scene
    cam=bpy.data.objects.new('Eye',bpy.data.cameras.new('Eye')); sc.collection.objects.link(cam); sc.camera=cam
    cam.rotation_euler=(math.pi/2,0,0)  # look +Y
    cam.data.angle=math.radians(fov); cam.data.clip_start=0.01
    for loc,e in [((0.5,-0.5,1.0),60),((-1,0.5,0.5),25)]:
        l=bpy.data.objects.new('l',bpy.data.lights.new('l','POINT')); l.data.energy=e; l.location=loc; sc.collection.objects.link(l)
    w=bpy.data.worlds.new('w'); sc.world=w; w.use_nodes=True; w.node_tree.nodes['Background'].inputs[0].default_value=(0.35,0.37,0.4,1)
    sc.render.engine='CYCLES'; sc.cycles.samples=16; sc.render.resolution_x=640; sc.render.resolution_y=360
    return cam
def render(path,frame=None):
    sc=bpy.context.scene
    if frame is not None: sc.frame_set(frame)
    sc.render.filepath=path; bpy.ops.render.render(write_still=True)
def rel(arm,child,parent):
    b=arm.data.bones; return b[parent].matrix_local.inverted()@b[child].matrix_local
def hand_matrix(pos,fwd,up):
    # bone Y = fwd, bone Z = up (orthonormalised)
    y=Vector(fwd).normalized(); z=Vector(up); z=(z-z.dot(y)*y).normalized(); x=y.cross(z)
    m=Matrix((x,y,z)).transposed().to_4x4(); m.translation=Vector(pos); return m
def set_hand(arm,side,pos,fwd,up):
    H=hand_matrix(pos,fwd,up)
    C=H@rel(arm,f'hand.{side}',f'hand.{side}.control').inverted()
    pb=arm.pose.bones[f'hand.{side}.control']; pb.matrix=C
    bpy.context.view_layer.update()
def curl(arm,side,amt,thumb=0.5,index=None):
    for f in ('f_middle','f_ring','f_pinky','f_index'):
        a=amt if (f!='f_index' or index is None) else index
        pb=arm.pose.bones[f'{f}.01.{side}']; pb.rotation_mode='XYZ'; pb.rotation_euler=(a,0,0)
    pb=arm.pose.bones[f'thumb.02.{side}']; pb.rotation_mode='XYZ'; pb.rotation_euler=(thumb,0,0)
