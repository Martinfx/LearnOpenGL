#!/usr/bin/env python3
"""Post-apocalyptic clutter that Poly Haven doesn't have: pallets, cinder blocks, bricks, rubble, paper, rags,
cardboard. Headless Blender as a Python module (pip install bpy); writes assets/models/props/clutter/*.glb.

    python3 -I assets/models/props/clutter/source/clutter.py /tmp/clutter/tex

Real-world sizes, 1 unit = 1 m, +Y up after glTF export, origin at the bottom centre. Low poly so the meshes
can be used directly as collision; clutter.json lists a box collider and a mass for physics props.
Textures: Poly Haven CC0 scans (weathered_planks, red_brick, concrete_wall_007, concrete_debris, rusty_metal_02,
hessian_230 (rags), rough_wood (pallets), rubble) downsized to 1K; cardboard and newspaper are procedural (no real print).
"""
import os,sys,math,json
import numpy as np
import bpy,bmesh
TEX=sys.argv[1] if len(sys.argv)>1 else '/tmp/clutter/tex'
OUT=os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)),'..'))
rng=np.random.default_rng(42); MAN={}

def reset():
    bpy.ops.wm.read_factory_settings(use_empty=True)
def mat(name,rough=None):
    m=bpy.data.materials.get(name)
    if m: return m
    m=bpy.data.materials.new(name); m.use_nodes=True; nt=m.node_tree; b=nt.nodes['Principled BSDF']
    def img(k,cs):
        p=os.path.join(TEX,f'{name}_{k}.jpg')
        if not os.path.exists(p): return None
        t=nt.nodes.new('ShaderNodeTexImage'); t.image=bpy.data.images.load(p); t.image.colorspace_settings.name=cs; return t
    a=img('albedo','sRGB'); nt.links.new(a.outputs[0],b.inputs['Base Color'])
    r=img('roughness','Non-Color')
    if r: nt.links.new(r.outputs[0],b.inputs['Roughness'])
    else: b.inputs['Roughness'].default_value=rough or 0.85
    n=img('normal','Non-Color')
    if n:
        nm=nt.nodes.new('ShaderNodeNormalMap'); nt.links.new(n.outputs[0],nm.inputs['Color']); nt.links.new(nm.outputs[0],b.inputs['Normal'])
    return m
def box(sx,sy,sz,loc=(0,0,0),rot=(0,0,0),m=None,uv=1.0):
    bpy.ops.mesh.primitive_cube_add(size=1,location=loc,rotation=rot); o=bpy.context.object; o.scale=(sx,sy,sz)
    bpy.ops.object.transform_apply(scale=True); cube_uv(o,uv)
    if m: o.data.materials.append(m)
    return o
def cube_uv(o,scale=1.0):
    bpy.context.view_layer.objects.active=o; bpy.ops.object.mode_set(mode='EDIT'); bpy.ops.mesh.select_all(action='SELECT')
    bpy.ops.uv.cube_project(cube_size=scale); bpy.ops.object.mode_set(mode='OBJECT')
def jitter(o,amt,seed_axis=None):
    for v in o.data.vertices: v.co+=__import__('mathutils').Vector(rng.uniform(-amt,amt,3))
def export(name,mass=None,collider=None,kind='static'):
    objs=[o for o in bpy.context.scene.objects if o.type=='MESH']
    for o in objs: o.select_set(True)
    bpy.context.view_layer.objects.active=objs[0]
    if len(objs)>1: bpy.ops.object.join()
    o=bpy.context.object; bpy.ops.object.transform_apply(location=True,rotation=True,scale=True)
    mn=np.min([list(v.co) for v in o.data.vertices],0); mx=np.max([list(v.co) for v in o.data.vertices],0)
    o.data.transform(__import__('mathutils').Matrix.Translation((-(mn[0]+mx[0])/2,-(mn[1]+mx[1])/2,-mn[2])))
    size=(mx-mn); o.name=name; o.data.name=name
    bpy.ops.object.shade_smooth_by_angle(angle=math.radians(35)) if hasattr(bpy.ops.object,'shade_smooth_by_angle') else None
    p=os.path.join(OUT,name+'.glb')
    bpy.ops.export_scene.gltf(filepath=p,export_format='GLB',export_yup=True,export_apply=True,export_image_format='JPEG',export_jpeg_quality=88)
    tris=sum(len(f.vertices)-2 for f in o.data.polygons)
    MAN[name]={'size_m':[round(float(size[0]),3),round(float(size[2]),3),round(float(size[1]),3)],'tris':tris,
               'physics':kind,'mass_kg':mass,'collider':collider or 'box'}
    print('wrote',name,'size(x,y,z m)',MAN[name]['size_m'],'tris',tris)

# ------------------------------------------------------------------ props
def pallet():
    reset(); m=mat('planks')
    for i in range(5): box(1.2,0.10 if i%4==0 else 0.145,0.022,(0,-0.4+0.0725+i*0.164,0.144-0.011),m=m)   # top deck boards
    for j in (-1,0,1):
        for i in (-1,0,1): box(0.145 if j==0 else 0.145,0.10,0.078,(j*0.5275,i*0.3275,0.022+0.039),m=m)   # blocks
        box(0.145,0.8,0.022,(j*0.5275,0,0.122-0.011),m=m)                                              # stringer boards
    for i in (-1,0,1): box(1.2,0.10,0.022,(0,i*0.35,0.011),m=m)                                        # bottom boards
    export('wooden_pallet',mass=22,collider='box',kind='dynamic')
def pallet_broken():
    reset(); m=mat('planks')
    for i,keep in enumerate([1,0,1,1,0]):
        if keep: box(1.2 if i!=3 else 0.7,0.13,0.022,((0 if i!=3 else -0.25),-0.4+0.0725+i*0.164,0.133),rot=(0,math.radians(rng.uniform(-2,2)),0),m=m)
    for j in (-1,0,1):
        for i in (-1,0,1):
            if (j,i)!=(1,1): box(0.145,0.10,0.078,(j*0.5275,i*0.3275,0.061),m=m)
        box(0.145,0.8,0.022,(j*0.5275,0,0.111),m=m)
    box(0.9,0.13,0.022,(0.2,0.75,0.011),rot=(0,0,math.radians(25)),m=m)   # torn-off board lying beside
    export('wooden_pallet_broken',mass=18,collider='mesh',kind='static')
def cinder(broken=False):
    reset(); m=mat('concrete')
    o=box(0.39,0.19,0.19,(0,0,0.095),m=m,uv=0.4)
    for x in (-0.095,0.095):   # two hollow cores
        c=box(0.13,0.11,0.25,(x,0,0.095)); md=o.modifiers.new('core','BOOLEAN'); md.object=c; md.operation='DIFFERENCE'
        bpy.context.view_layer.objects.active=o; bpy.ops.object.modifier_apply(modifier=md.name); bpy.data.objects.remove(c)
    if broken:
        c=box(0.3,0.3,0.3,(0.27,0.05,0.2),rot=(0.3,0.5,0.7)); md=o.modifiers.new('break','BOOLEAN'); md.object=c; md.operation='DIFFERENCE'
        bpy.context.view_layer.objects.active=o; bpy.ops.object.modifier_apply(modifier=md.name); bpy.data.objects.remove(c)
    cube_uv(o,0.4)
    export('cinder_block_broken' if broken else 'cinder_block',mass=11 if broken else 15,collider='box',kind='dynamic')
def brick(): reset(); box(0.215,0.1025,0.065,(0,0,0.0325),m=mat('brick'),uv=0.3); export('brick',mass=2.3,collider='box',kind='dynamic')
def brick_pile():
    reset(); m=mat('brick'); z=0
    for k in range(26):
        r=math.sqrt(rng.random())*0.45; a=rng.uniform(0,2*math.pi); h=0.0325+max(0,0.25-r*0.5)*rng.random()
        box(0.215,0.1025,0.065,(r*math.cos(a),r*math.sin(a),h),rot=tuple(rng.uniform(-0.5,0.5,2))+(rng.uniform(0,6.3),),m=m,uv=0.3)
    export('brick_pile',collider='mesh')
def rubble(name,n,radius,height):
    reset(); m=mat('debris'); m2=mat('rust')
    bpy.ops.mesh.primitive_uv_sphere_add(segments=20,ring_count=8,radius=1)   # the mound of fines under the chunks
    o=bpy.context.object; o.scale=(radius,radius*0.9,height*0.75); bpy.ops.object.transform_apply(scale=True)
    bm=bmesh.new(); bm.from_mesh(o.data); bmesh.ops.delete(bm,geom=[v for v in bm.verts if v.co.z< -1e-4],context='VERTS'); bm.to_mesh(o.data); bm.free()
    for v in o.data.vertices:   # lumpy, flattened top
        v.co.z=min(v.co.z,height*0.55)*(1+rng.uniform(-0.25,0.25)); v.co.x*=1+rng.uniform(-0.12,0.12); v.co.y*=1+rng.uniform(-0.12,0.12)
    cube_uv(o,0.8); o.data.materials.append(mat('rubble'))
    for k in range(n):
        r=math.sqrt(rng.random())*radius*0.9; a=rng.uniform(0,2*math.pi); s=rng.uniform(0.03,0.12)*(1+radius)/1.5
        bpy.ops.mesh.primitive_ico_sphere_add(subdivisions=1,radius=s,location=(r*math.cos(a),r*math.sin(a),max(0,height*(1-r/radius))*rng.random()))
        o=bpy.context.object; o.scale=(1,rng.uniform(0.6,1),rng.uniform(0.35,0.7)); bpy.ops.object.transform_apply(scale=True)
        jitter(o,s*0.25); o.rotation_euler=tuple(rng.uniform(0,6.3,3)); bpy.ops.object.transform_apply(location=False,rotation=True,scale=False)
        base=max(0.0,height*(1-r/radius))*rng.random()*0.7; zmin=min(v.co.z for v in o.data.vertices)
        o.location=(r*math.cos(a),r*math.sin(a),base-zmin)   # rest on the pile, not float
        cube_uv(o,0.5); o.data.materials.append(m)
    for k in range(3):   # bent rebar sticking out
        bpy.ops.mesh.primitive_cylinder_add(vertices=6,radius=0.006,depth=rng.uniform(0.4,0.9)*radius,
            location=tuple(rng.uniform(-0.4,0.4,2)*radius)+(height*0.3,),rotation=(rng.uniform(1.25,1.5),0,rng.uniform(0,6.3)))
        o=bpy.context.object; cube_uv(o,0.2); o.data.materials.append(m2)
    export(name,collider='mesh')
def cloth(name,mname,sx,sy,crumple,mass):
    reset(); bpy.ops.mesh.primitive_grid_add(x_subdivisions=18,y_subdivisions=18,size=1); o=bpy.context.object
    o.scale=(sx,sy,1); bpy.ops.object.transform_apply(scale=True)
    ph=rng.uniform(0,6,4)
    for v in o.data.vertices:
        x,y=v.co.x/sx,v.co.y/sy
        v.co.z=crumple*(0.5+0.5*math.sin(7*x+ph[0])*math.cos(6*y+ph[1])+0.4*math.sin(13*x+11*y+ph[2]))*max(0,1-(abs(x)+abs(y))*0.6)
        v.co.x*=1-0.15*rng.random(); v.co.y*=1-0.15*rng.random()
    bpy.context.view_layer.objects.active=o; bpy.ops.object.mode_set(mode='EDIT'); bpy.ops.mesh.select_all(action='SELECT')
    bpy.ops.uv.unwrap(method='ANGLE_BASED'); bpy.ops.object.mode_set(mode='OBJECT')
    sol=o.modifiers.new('t','SOLIDIFY'); sol.thickness=0.002 if mname=='paper' else 0.004; bpy.ops.object.modifier_apply(modifier='t')
    o.data.materials.append(mat(mname)); export(name,mass=mass,collider='box',kind='dynamic' if mass else 'static')
def newspaper_stack():
    reset(); m=mat('paper')
    for i in range(14):
        box(0.40,0.29,0.004,(rng.uniform(-0.01,0.01),rng.uniform(-0.01,0.01),0.002+i*0.0042),rot=(0,0,rng.uniform(-0.05,0.05)),m=m,uv=0.42)
    box(0.012,0.30,0.002,(0.1,0,0.06),m=mat('rag'),uv=0.2); box(0.012,0.30,0.002,(-0.1,0,0.06),m=mat('rag'),uv=0.2)  # string
    export('newspaper_stack',mass=3,collider='box',kind='dynamic')
def cardboard(name,sx,sy,sz,crush=0.0,mass=0.6,open_=False):
    reset(); o=box(sx,sy,sz,(0,0,sz/2),m=mat('cardboard'),uv=max(sx,sy,sz))
    if crush:
        for v in o.data.vertices:
            if v.co.z>sz*0.9: v.co.z-=crush*sz*rng.random(); v.co.x+=rng.uniform(-0.02,0.02)
    if open_:   # flaps sticking up
        for side in (-1,1): box(sx,0.004,sy*0.45,(0,side*(sy/2+0.0),sz+sy*0.2),rot=(side*math.radians(rng.uniform(15,40)),0,0),m=mat('cardboard'),uv=sx)
    export(name,mass=mass,collider='box',kind='dynamic')
def cardboard_flat(): reset(); box(1.1,0.75,0.006,(0,0,0.003),rot=(0,0,0.1),m=mat('cardboard'),uv=1.1); export('cardboard_flattened',mass=0.4,collider='box',kind='dynamic')
def planks_scrap():
    reset(); m=mat('planks')
    for k in range(5): box(rng.uniform(0.6,1.6),0.12,0.022,(rng.uniform(-0.3,0.3),rng.uniform(-0.3,0.3),0.011+k*0.02),rot=(0,0,rng.uniform(-0.6,0.6)),m=m)
    export('planks_scrap_pile',collider='mesh')

for f in (pallet,pallet_broken,lambda:cinder(False),lambda:cinder(True),brick,brick_pile,
          lambda:rubble('rubble_pile_small',30,0.5,0.25),lambda:rubble('rubble_pile_large',110,1.4,0.6),
          lambda:cloth('rag_a','rag',0.55,0.45,0.04,None),lambda:cloth('rag_b','rag',0.8,0.5,0.06,None),
          lambda:cloth('newspaper_sheet_a','paper',0.58,0.40,0.015,None),lambda:cloth('newspaper_sheet_b','paper',0.40,0.29,0.03,None),
          newspaper_stack,lambda:cardboard('cardboard_box_small',0.40,0.30,0.25),lambda:cardboard('cardboard_box_large',0.60,0.40,0.40,mass=1.2),
          lambda:cardboard('cardboard_box_crushed',0.50,0.40,0.32,crush=0.35,mass=0.8),lambda:cardboard('cardboard_box_open',0.45,0.35,0.30,mass=0.7,open_=True),
          cardboard_flat,planks_scrap):
    f()
json.dump(MAN,open(os.path.join(OUT,'clutter.json'),'w'),indent=1)
