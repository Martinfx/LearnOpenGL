# Procedural railway pieces + highway sign gantry (CC0, made for this project). Metres, Blender z-up -> glTF +Y up.
# Track runs along +X, centred on origin, base of ballast at y=0. Russian/Soviet 1520 mm gauge, timber sleepers.
import bpy,bmesh,math,sys,json
from mathutils import Vector,Matrix
mats={}
OUT=sys.argv[1]; import os; T=os.path.join(os.path.dirname(os.path.abspath(__file__)),'../../../../textures/pbr/')
def reset():
    bpy.ops.wm.read_factory_settings(use_empty=True); mats.clear()

def mat(name,tex,tile=2.0,rough=0.9,metal=0.0):
    if name in mats: return mats[name]
    m=bpy.data.materials.new(name); m.use_nodes=True; nt=m.node_tree; b=nt.nodes['Principled BSDF']
    t=nt.nodes.new('ShaderNodeTexImage'); t.image=bpy.data.images.load(T+tex+'/albedo.jpg'); nt.links.new(t.outputs['Color'],b.inputs['Base Color'])
    n=nt.nodes.new('ShaderNodeTexImage'); n.image=bpy.data.images.load(T+tex+'/normal.jpg'); n.image.colorspace_settings.name='Non-Color'
    nm=nt.nodes.new('ShaderNodeNormalMap'); nt.links.new(n.outputs['Color'],nm.inputs['Color']); nt.links.new(nm.outputs['Normal'],b.inputs['Normal'])
    b.inputs['Roughness'].default_value=rough; b.inputs['Metallic'].default_value=metal
    m['tile']=tile; mats[name]=m; return m
parts=[]  # (bmesh, material)
def add_box(bm,c,s,R=Matrix.Identity(3)):
    hx,hy,hz=s[0]/2,s[1]/2,s[2]/2
    vs=[bm.verts.new(Vector(c)+R@Vector((x,y,z))) for x in(-hx,hx) for y in(-hy,hy) for z in(-hz,hz)]
    for f in [(0,1,3,2),(4,6,7,5),(0,4,5,1),(2,3,7,6),(0,2,6,4),(1,5,7,3)]: bm.faces.new([vs[i] for i in f])
def add_prism(bm,pts2d,path,frames):
    # extrude 2D profile (y,z) along path points with given frames (tangent t, side s)
    rings=[]
    for p,(t,s) in zip(path,frames):
        up=t.cross(s).normalized() if False else Vector((0,0,1))
        rings.append([bm.verts.new(p+s*y+up*z) for y,z in pts2d])
    n=len(pts2d)
    for i in range(len(rings)-1):
        for k in range(n):
            k2=(k+1)%n; bm.faces.new((rings[i][k],rings[i][k2],rings[i+1][k2],rings[i+1][k]))
    bm.faces.new(list(reversed(rings[0]))); bm.faces.new(rings[-1])
def finish(bm,m,name):
    bmesh.ops.recalc_face_normals(bm,faces=bm.faces)
    uv=bm.loops.layers.uv.new('UV'); tile=m['tile']
    for f in bm.faces:
        nrm=f.normal; ax=max(range(3),key=lambda i:abs(nrm[i]))
        for l in f.loops:
            co=l.vert.co; u,v=[(co.y,co.z),(co.x,co.z),(co.x,co.y)][ax]; l[uv].uv=(u/tile,v/tile)
    me=bpy.data.meshes.new(name); bm.to_mesh(me); bm.free(); me.materials.append(m)
    o=bpy.data.objects.new(name,me); bpy.context.scene.collection.objects.link(o); return o
def export(path):
    objs=[o for o in bpy.data.objects if o.type=='MESH']
    for o in objs: o.select_set(True)
    bpy.context.view_layer.objects.active=objs[0]; bpy.ops.object.join()
    for im in bpy.data.images:
        if im.size[0]>512: im.scale(512,512)
    o=bpy.context.object; t=sum(len(p.vertices)-2 for p in o.data.polygons)
    bpy.ops.export_scene.gltf(filepath=path,export_format='GLB',use_selection=True,export_yup=True)
    print('PIECE',path.split('/')[-1],'tris',t,'dims',[round(x,2) for x in o.dimensions])
G=1.520+0.073  # rail centre distance (gauge + head width)
BALLAST_H=0.35; SLEEPER=(0.25,2.75,0.16); RAIL_H=0.18
RAIL=[(-0.075,0),(0.075,0),(0.075,0.012),(0.008,0.03),(0.008,0.14),(0.036,0.15),(0.036,0.18),(-0.036,0.18),(-0.036,0.15),(-0.008,0.14),(-0.008,0.03),(-0.075,0.012)]
def track(length,radius=None,name='rail'):
    reset()
    n=max(2,int(length/1.0)+1)
    def centre(s):  # s in [0,length] -> point, tangent, side
        if not radius: return Vector((s-length/2,0,0)),Vector((1,0,0)),Vector((0,1,0))
        a=(s-length/2)/radius; p=Vector((radius*math.sin(a),radius-radius*math.cos(a),0)); t=Vector((math.cos(a),math.sin(a),0))
        return p,t,Vector((-t.y,t.x,0))
    # ballast: trapezoid profile
    bm=bmesh.new(); prof=[(-2.3,0),(2.3,0),(1.75,BALLAST_H),(-1.75,BALLAST_H)]
    ss=[length*i/(n-1) for i in range(n)]; P=[centre(s) for s in ss]
    add_prism(bm,prof,[p for p,_,_ in P],[(t,sd) for _,t,sd in P]); finish(bm,mat('ballast','rocks_ground_02',2.0),'ballast')
    bm=bmesh.new()
    k=int(length/0.55)
    for i in range(k):
        s=(i+0.5)*length/k; p,t,sd=centre(s); R=Matrix((t,sd,Vector((0,0,1)))).transposed()
        add_box(bm,p+Vector((0,0,BALLAST_H-0.04+SLEEPER[2]/2)),SLEEPER,R)
    finish(bm,mat('sleeper','weathered_brown_planks',1.5),'sleepers')
    bm=bmesh.new(); top=BALLAST_H-0.04+SLEEPER[2]
    for side in (-1,1):
        path=[p+sd*side*G/2+Vector((0,0,top)) for p,_,sd in P]
        add_prism(bm,RAIL,path,[(t,sd) for _,t,sd in P])
    finish(bm,mat('rail','rusty_metal_02',1.0,0.6,0.6),'rails')
    export(f'{OUT}/{name}.glb')
track(12.5,None,'rail_straight_12m')
track(12.5,150.0,'rail_curve_r150_12m')
track(3.0,None,'rail_straight_3m')
# buffer stop at the end of a 3 m piece
reset(); bm=bmesh.new()
for y in(-G/2,G/2):
    add_box(bm,(0.0,y,0.75),(0.25,0.25,0.9)); add_box(bm,(-0.9,y,0.55),(2.0,0.18,0.18),Matrix.Rotation(math.radians(-20),3,'Y'))
add_box(bm,(0.12,0,1.05),(0.3,2.9,0.35))
finish(bm,mat('rail','rusty_metal_02',1.0,0.6,0.6),'stop')
bm=bmesh.new(); prof=[(-2.3,0),(2.3,0),(1.75,BALLAST_H),(-1.75,BALLAST_H)]
add_prism(bm,prof,[Vector((-1.5,0,0)),Vector((1.5,0,0))],[(Vector((1,0,0)),Vector((0,1,0)))]*2); finish(bm,mat('ballast','rocks_ground_02',2.0),'ballast')
export(f'{OUT}/rail_buffer_stop.glb')
# highway sign gantry: two lattice posts, box truss beam, two blank panels (span 18 m, clearance 5.5 m)
reset(); bm=bmesh.new(); span=18; Hc=5.5
for x in(-span/2,span/2):
    for dx in(-0.35,0.35):
        for dy in(-0.35,0.35): add_box(bm,(x+dx,dy,(Hc+1.2)/2),(0.08,0.08,Hc+1.2))
    for z in [0.6*i for i in range(1,int((Hc+1.2)/0.6))]:
        add_box(bm,(x,-0.35,z),(0.7,0.05,0.05)); add_box(bm,(x,0.35,z),(0.7,0.05,0.05)); add_box(bm,(x-0.35,0,z),(0.05,0.7,0.05)); add_box(bm,(x+0.35,0,z),(0.05,0.7,0.05))
for dy in(-0.35,0.35):
    for z in(Hc,Hc+1.1): add_box(bm,(0,dy,z),(span+0.8,0.08,0.08))
for i in range(int(span/0.9)+1):
    x=-span/2+i*0.9; add_box(bm,(x,-0.35,Hc+0.55),(0.05,0.05,1.1)); add_box(bm,(x,0.35,Hc+0.55),(0.05,0.05,1.1))
    d=Matrix.Rotation(math.radians(50),3,"Y")
    if x+0.9>span/2: continue
    add_box(bm,(x+0.45,-0.35,Hc+0.55),(0.04,0.04,1.4),d); add_box(bm,(x+0.45,0.35,Hc+0.55),(0.04,0.04,1.4),d)
finish(bm,mat('steel','green_metal_rust',1.5,0.7,0.5),'frame')
bm=bmesh.new()
for x,w in((-4.0,5.0),(3.5,4.0)): add_box(bm,(x,-0.5,Hc+0.6),(w,0.06,2.2))
finish(bm,mat('panel','blue_metal_plate',2.5,0.6,0.3),'panels')
export(f'{OUT}/sign_gantry_18m.glb')
