#!/usr/bin/env python3
"""Dead City street props (headless Blender as a Python module, CC0): 200 l drums, tyre piles, dumpsters, wheelie
bins, shipping containers, brick ruins and rubble, curbs and broken sidewalk, US traffic signals, cones, US road
signs, Jersey barriers, pipe railings and highway guardrail. Post-apocalyptic: faded paint, rust, dents, bent and
fallen variants.

    python3 -I textures.py /tmp/dc/tex          # builds the 1K texture set (Poly Haven CC0 scans + procedural)
    python3 -I dc_props.py /tmp/dc/tex [group ...]

Groups: drums tyres bins containers ruins street signals cones signs barriers railings (default: all).
Reuses the helpers of ../../clutter/source/clutter.py (materials from <tex>/<name>_{albedo,roughness,normal}.jpg;
glb export with the origin at the bottom centre). 1 unit = 1 m, +Y up after export, long side along +X before
export (= +X in the engine), front of signs and signal heads facing -Y (= +Z in the engine). Low poly so the
meshes can be used as collision; deadcity.json lists size, tris, physics kind, mass and collider per model.
"""
import os,sys,math,json
HERE=os.path.dirname(os.path.abspath(__file__)); OUT=os.path.normpath(os.path.join(HERE,'..'))
CL=os.path.join(HERE,'..','..','clutter','source','clutter.py')
src=open(CL).read().split('# ------------------------------------------------------------------ props')[0]
g={'__file__':CL,'__name__':'deadcity'}; exec(src,g)
g['OUT']=OUT; bpy=g['bpy']; box=g['box']; mat=g['mat']; reset=g['reset']; export=g['export']; cube_uv=g['cube_uv']
np=g['np']; MAN=g['MAN']; TEX=g['TEX']
import bmesh
from mathutils import Vector,Matrix,Euler
R=np.random.default_rng(7)
D=math.radians
ASSETS=os.path.normpath(os.path.join(OUT,'..','..','..'))

# ------------------------------------------------------------------ helpers
def obj_from_bm(bm,name,m=None,uv=None):
    me=bpy.data.meshes.new(name); bm.to_mesh(me); bm.free(); o=bpy.data.objects.new(name,me)
    bpy.context.scene.collection.objects.link(o)
    if m: me.materials.append(m)
    if uv: cube_uv(o,uv)
    return o
def cyl(r,h,loc=(0,0,0),rot=(0,0,0),m=None,uv=1.0,verts=16,r2=None,cap=True):
    if r2 is None: bpy.ops.mesh.primitive_cylinder_add(vertices=verts,radius=r,depth=h,location=loc,rotation=rot,end_fill_type='NGON' if cap else 'NOTHING')
    else: bpy.ops.mesh.primitive_cone_add(vertices=verts,radius1=r,radius2=r2,depth=h,location=loc,rotation=rot,end_fill_type='NGON' if cap else 'NOTHING')
    o=bpy.context.object; cube_uv(o,uv)
    if m: o.data.materials.append(m)
    return o
def tube(p0,p1,r,m=None,uv=1.0,verts=8):
    """cylinder from p0 to p1"""
    p0=Vector(p0); p1=Vector(p1); d=p1-p0; L=d.length
    o=cyl(r,L,loc=(p0+p1)/2,m=None,uv=uv,verts=verts)
    o.rotation_mode='QUATERNION'; o.rotation_quaternion=Vector((0,0,1)).rotation_difference(d.normalized())
    bpy.ops.object.transform_apply(rotation=True); cube_uv(o,uv)
    if m: o.data.materials.append(m)
    return o
def polyline(pts,r,m=None,uv=1.0,verts=8):
    return [tube(pts[i],pts[i+1],r,m,uv,verts) for i in range(len(pts)-1)]
def prism(profile,length,m=None,uv=1.0,axis='x'):
    """extrude a closed (y,z) profile along X from -length/2 to +length/2"""
    bm=bmesh.new(); vs=[bm.verts.new((-length/2,y,z)) for y,z in profile]; f=bm.faces.new(vs)
    bmesh.ops.recalc_face_normals(bm,faces=[f])
    r=bmesh.ops.extrude_face_region(bm,geom=[f]); nv=[e for e in r['geom'] if isinstance(e,bmesh.types.BMVert)]
    bmesh.ops.translate(bm,verts=nv,vec=(length,0,0)); bmesh.ops.recalc_face_normals(bm,faces=bm.faces)
    return obj_from_bm(bm,'prism',m,uv)
def plate(outline,thick,m=None,uv=1.0):
    """extrude a closed (x,z) outline along Y by thick (front at y=-thick/2)"""
    bm=bmesh.new(); vs=[bm.verts.new((x,-thick/2,z)) for x,z in outline]; f=bm.faces.new(vs)
    r=bmesh.ops.extrude_face_region(bm,geom=[f]); nv=[e for e in r['geom'] if isinstance(e,bmesh.types.BMVert)]
    bmesh.ops.translate(bm,verts=nv,vec=(0,thick,0)); bmesh.ops.recalc_face_normals(bm,faces=bm.faces)
    return obj_from_bm(bm,'plate',m,uv)
def sel(*objs):
    bpy.ops.object.select_all(action='DESELECT')
    for o in objs: o.select_set(True)
    bpy.context.view_layer.objects.active=objs[0]
def join(objs):
    sel(*objs)
    if len(objs)>1: bpy.ops.object.join()
    return bpy.context.object
def xform(objs,loc=(0,0,0),rot=(0,0,0),pivot=(0,0,0)):
    """rotate (XYZ euler, radians) objects about pivot, then move them; applied to the mesh data"""
    M=Matrix.Translation(Vector(loc)+Vector(pivot))@Euler(rot).to_matrix().to_4x4()@Matrix.Translation(-Vector(pivot))
    for o in objs:
        o.data.transform(M@o.matrix_world); o.matrix_world=Matrix.Identity(4)
def scene_meshes(): return [o for o in bpy.context.scene.objects if o.type=='MESH']
def dent(o,centers,radius,depth):
    """push vertices along -normal near the given points (car-bumper dents, crushed sheet metal)"""
    me=o.data; me.calc_normals_split() if hasattr(me,'calc_normals_split') else None
    for v in me.vertices:
        for c in centers:
            d=(v.co-Vector(c)).length
            if d<radius: v.co-=v.normal*depth*(1-d/radius)**2
def jit(o,amt): g['jitter'](o,amt)
def subdiv(o,cuts):
    sel(o); bpy.ops.object.mode_set(mode='EDIT'); bpy.ops.mesh.select_all(action='SELECT')
    bpy.ops.mesh.subdivide(number_cuts=cuts); bpy.ops.object.mode_set(mode='OBJECT')
def decimate(o,ratio):
    sel(o); md=o.modifiers.new('dec','DECIMATE'); md.ratio=ratio; bpy.ops.object.modifier_apply(modifier=md.name)
def bevel(o,w,seg=1):
    sel(o); md=o.modifiers.new('bev','BEVEL'); md.width=w; md.segments=seg; md.limit_method='ANGLE'; bpy.ops.object.modifier_apply(modifier=md.name)
def lod(name,ratio=0.4):
    """<name>_lod1.glb: decimated copy of the model just exported"""
    o=scene_meshes()[0]; decimate(o,ratio)
    bpy.ops.export_scene.gltf(filepath=os.path.join(OUT,name+'_lod1.glb'),export_format='GLB',export_yup=True,export_apply=True,export_image_format='JPEG',export_jpeg_quality=88)

# ------------------------------------------------------------------ drums
def drum(m,loc=(0,0,0),rot=(0,0,0),dents=0,lid=True,seed=0):
    """200 l / 55 US gal steel drum: r 0.292, h 0.88, two rolling hoops, rolled top and bottom chimes, bungs"""
    rr=np.random.default_rng(seed); parts=[]
    b=cyl(0.286,0.84,(0,0,0.44),m=m,uv=1.2,verts=24,cap=lid); parts.append(b)
    if not lid:  # open drum: bottom disc stays, rim is the chime
        bpy.ops.mesh.primitive_circle_add(vertices=24,radius=0.286,fill_type='NGON',location=(0,0,0.03)); c=bpy.context.object; c.data.materials.append(m); cube_uv(c,1.2); parts.append(c)
    for z in (0.012,0.868): parts.append(cyl(0.292,0.024,(0,0,z),m=m,uv=1.2,verts=24,cap=False))
    for z in (0.29,0.59): parts.append(cyl(0.294,0.022,(0,0,z),m=m,uv=1.2,verts=24,cap=False))
    if lid:
        parts.append(cyl(0.028,0.012,(0.17,0.05,0.866),m=m,uv=0.5,verts=8)); parts.append(cyl(0.02,0.012,(-0.18,-0.03,0.866),m=m,uv=0.5,verts=8))
    o=join(parts)
    if dents: dent(o,[(0.29*math.cos(a),0.29*math.sin(a),z) for a,z in zip(rr.uniform(0,6.28,dents),rr.uniform(0.15,0.8,dents))],0.16,0.05)
    xform([o],loc,rot,pivot=(0,0,0)); return o
def drums():
    for col in ('rust','blue','red','yellow','green'):
        reset(); drum(mat('drum_'+col),dents=1 if col!='rust' else 2,seed=hash(col)%99); export('drum_'+col,mass=18,collider='cylinder',kind='dynamic')
    reset(); drum(mat('drum_blue'),dents=4,seed=3); export('drum_blue_dented',mass=18,collider='cylinder',kind='dynamic')
    reset(); drum(mat('drum_rust'),lid=False,dents=2,seed=4); export('drum_rust_open',mass=16,collider='cylinder',kind='dynamic')
    reset(); drum(mat('drum_red'),rot=(D(90),0,D(10)),loc=(0,0,0.294),dents=2,seed=5); export('drum_red_lying',mass=18,collider='cylinder',kind='dynamic')
    reset()   # a cluster against a wall: three standing, one lying in front, all different paint
    drum(mat('drum_rust'),(0,0,0),(0,0,0.3),1,seed=6); drum(mat('drum_blue'),(0.6,0.05,0),(0,0,1.1),2,seed=7)
    drum(mat('drum_green'),(0.3,0.52,0),(0,0,2.0),1,seed=8); drum(mat('drum_yellow'),(0.25,-0.75,0.294),(D(90),0,D(70)),2,seed=9)
    export('drum_cluster',collider='mesh',kind='static')

# ------------------------------------------------------------------ tyres
def tyre_obj():
    bpy.ops.import_scene.gltf(filepath=os.path.join(ASSETS,'models','props','old_tyre','old_tyre.gltf'))
    o=[x for x in bpy.context.selected_objects if x.type=='MESH'][0]; sel(o); bpy.ops.object.transform_apply(location=True,rotation=True,scale=True)
    decimate(o,0.35); return o   # ~1000 tris; the tyre lies in XZ, axle along Y
def tyres():
    reset(); base=tyre_obj()
    def put(loc,rot):
        c=base.copy(); c.data=base.data.copy(); bpy.context.scene.collection.objects.link(c); xform([c],loc,rot); return c
    # stack of 4 lying flat
    for i in range(4): put((R.normal(0,0.02),R.normal(0,0.02),0.0825+i*0.165),(D(90),0,R.uniform(0,6.28)))
    bpy.data.objects.remove(base); export('tyre_stack',mass=40,collider='cylinder',kind='static')
    reset(); base=tyre_obj()
    lay=[((0,0,0.0825),(D(90),0,0)),((0.55,0.1,0.0825),(D(90),0,1)),((0.25,0.5,0.0825),(D(90),0,2)),((0.27,0.2,0.25),(D(80),D(8),0.4)),
         ((0.75,0.6,0.3),(0,0,D(30))),((-0.45,0.35,0.28),(D(20),0,D(-60)))]
    for l,r in lay: put(l,r)
    bpy.data.objects.remove(base); export('tyre_pile',collider='mesh',kind='static')
    reset(); base=tyre_obj(); put((0,0,0.3),(D(-12),0,0)); bpy.data.objects.remove(base); export('tyre_leaning',mass=10,collider='cylinder',kind='dynamic')

# ------------------------------------------------------------------ dumpsters, bins
def dumpster(m,lid_open=(0,0),dents=2,seed=0):
    """front-load dumpster 1.83 x 1.12 x 1.22 m: sloped front, two plastic lids, fork pockets, casters"""
    rr=np.random.default_rng(seed); W,Dp,H=1.83,1.12,1.22
    prof=[(-Dp/2,0.12),(Dp/2,0.12),(Dp/2,H),(-Dp/2+0.25,H),(-Dp/2,0.75)]   # (y,z), front (-y) sloped
    body=prism(prof,W,m,1.5); parts=[body]
    for x in (-0.55,0.55): parts.append(box(0.22,Dp+0.08,0.12,(x,0,0.75),m=m,uv=1.5))      # fork pockets on the sides
    for x in (-W/2-0.02,W/2+0.02): parts.append(box(0.04,Dp*0.9,0.05,(x,0,H-0.05),m=m,uv=1.5))
    for x,y in ((-0.75,-0.4),(0.75,-0.4),(-0.75,0.4),(0.75,0.4)):
        parts.append(cyl(0.06,0.05,(x,y,0.06),rot=(0,D(90),0),m=mat('rubber'),uv=0.5,verts=10)); parts.append(box(0.08,0.08,0.06,(x,y,0.11),m=m,uv=0.5))
    lm=mat('bin_green' if 'green' in m.name else 'bin_grey')
    for i,a in enumerate(lid_open):
        x=(-0.46 if i==0 else 0.46); L=box(0.9,0.84,0.03,(0,0,0),m=lm,uv=1.0)
        xform([L],(x,Dp/2-0.42,H+0.015),(D(-a),0,0),pivot=(0,0.42,0)); parts.append(L)
    o=join(parts)
    if dents: dent(o,[(rr.uniform(-0.8,0.8),-Dp/2+0.1,rr.uniform(0.3,0.9)) for _ in range(dents)],0.35,0.06)
    return o
def wheelie_bin(m,lid_open=0):
    """240 l wheelie bin 0.58 x 0.74 x 1.07 m, tapered body, lid on the back hinge, two wheels"""
    p=[(-0.37,0.0),(0.33,0.0),(0.37,1.0),(-0.37,1.0)]
    b=prism([(y,z) for y,z in p],0.58,m,0.8); xform([b],(0,0,0.03))
    xform([b],(0,0,0),(0,0,0)); parts=[b]
    sel(b); bpy.ops.transform.resize(value=(1,1,1))
    L=box(0.6,0.76,0.035,(0,0,0),m=m,uv=0.8); xform([L],(0,0,1.05),(D(-lid_open),0,0),pivot=(0,0.38,0)); parts.append(L)
    parts.append(box(0.63,0.78,0.06,(0,0.0,0.99),m=m,uv=0.8))                    # moulded rim under the lid
    for x in (-0.15,0.15): parts.append(box(0.06,0.02,0.6,(x,-0.355,0.55),rot=(D(-2.3),0,0),m=m,uv=0.8))   # front ribs
    parts.append(box(0.5,0.05,0.04,(0,0.38,0.98),m=m,uv=0.8))                   # handle bar
    for x in (-0.26,0.26): parts.append(cyl(0.1,0.05,(x,0.33,0.1),rot=(0,D(90),0),m=mat('rubber'),uv=0.5,verts=14))
    return join(parts)
def bins():
    reset(); dumpster(mat('dumpster_green'),(0,0),3,1); export('dumpster_green',mass=300,collider='box',kind='static'); lod('dumpster_green',0.5)
    reset(); dumpster(mat('dumpster_blue'),(110,15),2,2); export('dumpster_blue_open',mass=300,collider='box',kind='static'); lod('dumpster_blue_open',0.5)
    reset(); o=dumpster(mat('dumpster_green'),(160,0),4,3); xform([o],(0,0,0),(D(-90),0,D(4))); export('dumpster_green_tipped',mass=300,collider='box',kind='static'); lod('dumpster_green_tipped',0.5)
    reset(); wheelie_bin(mat('bin_green')); export('wheelie_bin_green',mass=12,collider='box',kind='dynamic')
    reset(); wheelie_bin(mat('bin_grey'),35); export('wheelie_bin_grey_open',mass=12,collider='box',kind='dynamic')
    reset(); o=wheelie_bin(mat('bin_green'),70); xform([o],(0,0,0),(D(90),0,D(15))); export('wheelie_bin_tipped',mass=12,collider='box',kind='dynamic')

# ------------------------------------------------------------------ shipping containers
def corrugated(length,height,m,uv,pitch=0.278,depth=0.036):
    """trapezoid-corrugated wall in the XZ plane (outer side -Y), length along X"""
    n=int(length/pitch); pts=[]
    for i in range(n):
        x0=-length/2+i*pitch; pts+= [(x0,0),(x0+pitch*0.18,-depth),(x0+pitch*0.5,-depth),(x0+pitch*0.68,0)]
    pts.append((length/2,0))
    bm=bmesh.new(); top=[];bot=[]
    for x,y in pts: bot.append(bm.verts.new((x,y,0))); top.append(bm.verts.new((x,y,height)))
    for i in range(len(pts)-1): bm.faces.new((bot[i],bot[i+1],top[i+1],top[i]))
    return obj_from_bm(bm,'corr',m,uv)
def container(m,doors=(0,0),dents=0,seed=0):
    """20 ft ISO container 6.06 x 2.44 x 2.59 m: corrugated sides, corner posts and castings, rails, doors with locking bars"""
    rr=np.random.default_rng(seed); L,W,H=6.058,2.438,2.591; parts=[]
    for s in (-1,1):
        c=corrugated(L-0.3,H-0.32,m,2.0); xform([c],(0,s*(W/2-0.04),0.16),(0,0,0 if s<0 else D(180))); parts.append(c)
    c=corrugated(W-0.3,H-0.32,m,2.0); xform([c],(L/2-0.04,0,0.16),(0,0,D(90))); parts.append(c)       # front end wall
    parts.append(box(L,W,0.05,(0,0,H-0.03),m=m,uv=2.0))                                         # roof
    parts.append(box(L,W-0.1,0.04,(0,0,0.14),m=m,uv=2.0))                                       # floor
    for s in (-1,1):
        parts.append(box(L,0.16,0.16,(0,s*(W/2-0.08),0.08),m=m,uv=2.0)); parts.append(box(L,0.1,0.12,(0,s*(W/2-0.05),H-0.06),m=m,uv=2.0))
    for x in (-L/2+0.08,L/2-0.08):
        for y in (-W/2+0.08,W/2-0.08):
            parts.append(box(0.16,0.16,H,(x,y,H/2),m=m,uv=2.0))
            for z in (0.06,H-0.06): parts.append(box(0.18,0.18,0.12,(x,y,z),m=mat('pole_dark'),uv=1.0))
    parts.append(box(0.14,W,0.2,(-L/2+0.07,0,0.1),m=m,uv=2.0)); parts.append(box(0.14,W,0.2,(-L/2+0.07,0,H-0.1),m=m,uv=2.0))
    # rear doors: two leaves hinged at the corner posts, four locking bars with handles
    dw=(W-0.32)/2; dh=H-0.4
    for i,(s,a) in enumerate(((-1,doors[0]),(1,doors[1]))):
        leaf=[corrugated(dw,dh,m,2.0,pitch=0.22,depth=0.025)]; xform(leaf,(0,0,0),(0,0,D(90)))
        for o in leaf: xform([o],(-L/2-0.01,s*dw/2,0.2))
        for k in (0.3,0.75):
            yb=s*dw*k-s*0.0 ; leaf.append(tube((-L/2-0.06,s*(dw*k),0.25),(-L/2-0.06,s*(dw*k),H-0.25),0.017,mat('pole_galv'),0.5,6))
            leaf.append(box(0.04,0.04,0.3,(-L/2-0.09,s*(dw*k)+s*0.05,1.1),m=mat('pole_galv'),uv=0.5))
        xform(leaf,(0,0,0),(0,0,-s*D(a)),pivot=(-L/2,s*(W/2-0.16),0)); parts+=leaf
    o=join(parts)
    if dents: dent(o,[(rr.uniform(-2.5,2.5),rr.choice([-1,1])*W/2,rr.uniform(0.5,2.2)) for _ in range(dents)],0.7,0.12)
    return o
def containers():
    reset(); container(mat('container_red'),dents=2,seed=1); export('container_red',mass=2200,collider='box',kind='static'); lod('container_red',0.5)
    reset(); container(mat('container_blue'),dents=1,seed=2); export('container_blue',mass=2200,collider='box',kind='static'); lod('container_blue',0.5)
    reset(); container(mat('container_rust'),doors=(100,35),dents=4,seed=3); export('container_rust_open',mass=2200,collider='mesh',kind='static'); lod('container_rust_open',0.5)

# ------------------------------------------------------------------ brick ruins, rubble
BW,BH=0.125,0.077   # half-brick column width and course height (65 mm brick + 12 mm joint)
def brick_wall(length,heights,thick=0.38,hole=None,m=None,plaster=None):
    """wall of half-brick columns along X with a stepped broken top; heights = callable(x)->m;
       hole=(x0,x1,z0,z1) window; plaster = material for a broken render layer on the -Y face"""
    m=m or mat('brick'); parts=[]; n=int(round(length/BW))
    for i in range(n):
        x=-length/2+(i+0.5)*BW; h=max(BH,round(heights(x)/BH)*BH)
        spans=[(0,h)]
        if hole and hole[0]<=x<=hole[1]: spans=[(0,min(h,hole[2])),(hole[3],h)] if h>hole[3] else [(0,min(h,hole[2]))]
        for z0,z1 in spans:
            if z1-z0>0.01: parts.append(box(BW,thick,z1-z0,(x,0,(z0+z1)/2),m=m,uv=1.0))
        if plaster and h>0.4:
            ph=h-R.uniform(0.15,0.6)
            for z0,z1 in spans:
                z1=min(z1,ph)
                if z1-z0>0.05: parts.append(box(BW,0.02,z1-z0,(x,-thick/2-0.01,(z0+z1)/2),m=plaster,uv=1.0))
    if hole:   # concrete lintel stub over the window
        parts.append(box(hole[1]-hole[0]+0.25,thick,0.15,((hole[0]+hole[1])/2,0,hole[3]+0.075),m=mat('concrete'),uv=1.0))
    for _ in range(int(length*3)):   # loose bricks fallen at the foot
        parts.append(box(0.25,0.12,0.065,(R.uniform(-length/2,length/2),R.choice([-1,1])*R.uniform(0.3,0.9),0.033),rot=(R.uniform(-0.3,0.3),R.uniform(-0.3,0.3),R.uniform(0,3)),m=m,uv=1.0))
    return join(parts)
def jagged(base,amp,seed):
    r=np.random.default_rng(seed); ph=r.uniform(0,6,4)
    return lambda x: base+amp*(0.5*math.sin(x*1.7+ph[0])+0.3*math.sin(x*4.1+ph[1])+0.2*math.sin(x*9+ph[2]))+r.normal(0,0.04)
def rubble_mound(rx,ry,h,m,seed,chunks=0,cm=None):
    """heap of rubble: displaced grid (edges at ground level, lumpy noisy top) plus loose chunks on it"""
    r=np.random.default_rng(seed); bpy.ops.mesh.primitive_grid_add(x_subdivisions=22,y_subdivisions=22,size=2); o=bpy.context.object
    ph=r.uniform(0,6.28,6); hgt=lambda x,y,d: h*max(0,1-d*d)**0.8*(1+0.18*math.sin(x*5+ph[0])*math.sin(y*4+ph[1])+0.12*math.sin(x*11+ph[2]+y*9))
    for v in o.data.vertices:
        x,y=v.co.x,v.co.y; ang=math.atan2(y,x); edge=1+0.15*math.sin(ang*3+ph[3])+0.08*math.sin(ang*7+ph[4])
        d0=math.hypot(x,y)/edge
        if d0>1: x,y=x/d0,y/d0; v.co.x,v.co.y=x,y      # outside verts snap onto the outline at ground level
        d=min(1,d0); v.co.x*=rx*0.9; v.co.y*=ry*0.9; v.co.z=hgt(x,y,d)+(r.normal(0,0.02*h) if d<0.95 else 0)
    sel(o); bpy.ops.object.mode_set(mode='EDIT'); bpy.ops.mesh.select_all(action='SELECT'); bpy.ops.mesh.remove_doubles(threshold=0.03)
    bpy.ops.mesh.dissolve_degenerate(threshold=0.005); bpy.ops.object.mode_set(mode='OBJECT')
    cube_uv(o,1.5); o.data.materials.append(m); parts=[o]
    for _ in range(chunks):
        a=r.uniform(0,6.28); d=r.uniform(0,0.85)**0.7; x,y=math.cos(a)*d,math.sin(a)*d
        z=hgt(x,y,d)*0.85; s=r.uniform(0.12,0.4)
        c=box(s,s*r.uniform(0.6,1.4),s*r.uniform(0.3,0.8),(x*rx*0.9,y*ry*0.9,z),rot=tuple(r.uniform(-0.6,0.6,3)),m=cm or m,uv=1.0); jit(c,s*0.08); parts.append(c)
    return join(parts)
def slab_rebar(length,width,thick,m,seed):
    """broken concrete floor slab: jagged outline, rebar sticking out of the broken edge"""
    r=np.random.default_rng(seed); n=14; pts=[]
    for i in range(n):
        a=i/n*2*math.pi; rad=1+0.25*r.normal()
        if 0.5<a<2.6: rad*=0.75   # broken side
        pts.append((math.cos(a)*length/2*min(rad,1.1),math.sin(a)*width/2*min(rad,1.1)))
    bm=bmesh.new(); vs=[bm.verts.new((x,y,0)) for x,y in pts]; f=bm.faces.new(vs)
    ex=bmesh.ops.extrude_face_region(bm,geom=[f]); nv=[e for e in ex['geom'] if isinstance(e,bmesh.types.BMVert)]
    bmesh.ops.translate(bm,verts=nv,vec=(0,0,thick)); bmesh.ops.recalc_face_normals(bm,faces=bm.faces)
    o=obj_from_bm(bm,'slab',m,1.5); parts=[o]; rm=mat('rust')
    for i in range(9):   # rebar out of the broken side, some bent down
        x=-length/2*0.6+i*length*0.6/8; y0=width/2*0.6
        bendz=r.uniform(-0.3,0.1); L=r.uniform(0.3,0.8)
        parts+=polyline([(x,y0-0.2,thick/2),(x,y0+L*0.5,thick/2),(x+r.normal(0,0.05),y0+L,thick/2+bendz)],0.008,rm,0.5,6)
    return join(parts)
def ruins():
    reset(); brick_wall(4.0,jagged(2.2,0.9,1),plaster=mat('brickplaster')); export('brick_wall_ruin_a',collider='mesh',kind='static'); lod('brick_wall_ruin_a',0.5)
    reset(); brick_wall(4.0,lambda x: 2.9-0.9*max(0,x-0.6)+0.15*math.sin(x*7),hole=(-1.1,0.1,0.9,2.3),m=mat('brickdark')); export('brick_wall_ruin_window',collider='mesh',kind='static'); lod('brick_wall_ruin_window',0.5)
    reset(); a=brick_wall(3.0,jagged(1.6,0.7,3),plaster=mat('brickplaster')); b=brick_wall(2.2,jagged(1.2,0.6,4))
    xform([b],(1.5+0.19,1.1-0.19,0),(0,0,D(90))); xform([a],(0,0,0)); export('brick_wall_ruin_corner',collider='mesh',kind='static'); lod('brick_wall_ruin_corner',0.5)
    reset(); brick_wall(3.0,jagged(0.7,0.35,5)); export('brick_wall_ruin_low',collider='mesh',kind='static')
    reset(); o=rubble_mound(1.6,1.2,0.75,mat('debris'),6,0); parts=[o]
    for _ in range(70):
        a=R.uniform(0,6.28); d=R.uniform(0,1)**0.6; x,y=math.cos(a)*d*1.4,math.sin(a)*d*1.05; z=0.7*max(0,1-d*d)**0.8
        parts.append(box(0.25,0.12,0.065,(x,y,z),rot=tuple(R.uniform(-0.8,0.8,3)),m=mat('brick'),uv=1.0))
    export('brick_pile_large',collider='mesh',kind='static'); lod('brick_pile_large',0.4)
    reset(); rubble_mound(1.3,1.0,0.55,mat('debris'),7,14,mat('concrete')); export('concrete_rubble_pile',collider='mesh',kind='static'); lod('concrete_rubble_pile',0.4)
    reset(); rubble_mound(2.6,1.8,1.1,mat('debris'),8,30,mat('concrete2')); export('rubble_mound_large',collider='mesh',kind='static'); lod('rubble_mound_large',0.4)
    reset(); o=slab_rebar(3.0,2.2,0.2,mat('concrete2'),9); xform([o],(0,0,0.3),(D(14),D(-6),0)); rubble_mound(0.9,0.7,0.3,mat('debris'),10,5,mat('concrete'))
    export('concrete_slab_rebar',collider='mesh',kind='static'); lod('concrete_slab_rebar',0.5)
    reset(); slab_rebar(2.0,1.4,0.18,mat('concrete2'),11); export('concrete_slab_rebar_flat',collider='mesh',kind='static')
    reset()
    for i in range(7):
        s=R.uniform(0.25,0.55); c=box(s*R.uniform(1,1.8),s,s*R.uniform(0.5,0.9),(R.normal(0,0.45),R.normal(0,0.35),s*0.3),rot=tuple(R.uniform(-0.4,0.4,3)),m=mat('concrete'),uv=1.0); jit(c,s*0.08)
    export('concrete_chunks',collider='mesh',kind='static')

# ------------------------------------------------------------------ curbs, sidewalk
KERB=[(-0.075,0),(0.075,0),(0.075,0.28),(0.045,0.31),(-0.075,0.31)]   # (y,z): 15 cm wide, rounded road-side arris
def street():
    reset(); prism(KERB,1.0,mat('concrete2'),1.0); export('curb_1m',mass=95,collider='box',kind='static')
    reset(); o=prism(KERB,2.0,mat('concrete2'),1.0); subdiv(o,3); jit(o,0.006); export('curb_2m',mass=190,collider='box',kind='static')
    reset()   # broken: two pieces, one tilted and sunk, a chipped gap between
    a=prism(KERB,0.9,mat('concrete2'),1.0); subdiv(a,2); jit(a,0.01); xform([a],(-0.5,0,0),(0,D(-4),0))
    b=prism(KERB,0.7,mat('concrete2'),1.0); subdiv(b,2); jit(b,0.012); xform([b],(0.45,0.03,-0.08),(D(9),D(6),D(4)))
    for _ in range(5): k=box(0.08,0.07,0.06,(R.uniform(-0.1,0.2),R.uniform(-0.25,0.2),0.03),rot=tuple(R.uniform(0,1,3)),m=mat('concrete2'),uv=1.0); jit(k,0.01)
    export('curb_broken',collider='mesh',kind='static')
    reset()   # quarter-circle corner, r 1.0 m to the road face
    bm=bmesh.new(); segs=8; rings=[]
    for i in range(segs+1):
        a=i/segs*math.pi/2; ring=[]
        for y,z in KERB:
            rr=1.0+y; ring.append(bm.verts.new((math.cos(a)*rr,math.sin(a)*rr,z)))
        rings.append(ring)
    k=len(KERB)
    for i in range(segs):
        for j in range(k): bm.faces.new((rings[i][j],rings[i][(j+1)%k],rings[i+1][(j+1)%k],rings[i+1][j]))
    bm.faces.new(rings[0][::-1]); bm.faces.new(rings[-1]); bmesh.ops.recalc_face_normals(bm,faces=bm.faces)
    obj_from_bm(bm,'corner',mat('concrete2'),1.0); export('curb_corner',collider='mesh',kind='static')
    # sidewalk: 2 x 2 m of 0.5 m slabs, heaved by roots, one missing, cracked ones
    for name,seed,heave in (('sidewalk_slabs_broken',1,1.0),('sidewalk_slabs_heaved',2,2.2)):
        reset(); r=np.random.default_rng(seed); miss={(r.integers(4),r.integers(4))}
        for i in range(4):
            for j in range(4):
                if (i,j) in miss: continue
                x,y=-0.75+i*0.5,-0.75+j*0.5; d=math.hypot(x-0.3,y+0.2)
                tz=heave*max(0,0.6-d)*0.12; rot=(r.normal(0,0.02)+heave*(y+0.2)*0.08*(d<0.6),r.normal(0,0.02)-heave*(x-0.3)*0.08*(d<0.6),r.normal(0,0.01))
                if r.random()<0.35:   # cracked: two halves slightly apart
                    for s in (-1,1):
                        p=box(0.24,0.49,0.06,(x+s*0.125,y,0.03+tz+r.uniform(0,0.02)),rot=(rot[0],rot[1]+s*0.03,rot[2]),m=mat('pavement'),uv=1.8); jit(p,0.004)
                else: box(0.49,0.49,0.06,(x,y,0.03+tz),rot=rot,m=mat('pavement'),uv=1.8)
        export(name,collider='mesh',kind='static')
    reset()   # loose broken slab fragments
    for _ in range(6):
        s=R.uniform(0.15,0.35); p=box(s,s*R.uniform(0.6,1),0.06,(R.normal(0,0.35),R.normal(0,0.35),0.03+R.uniform(0,0.05)),rot=(R.normal(0,0.2),R.normal(0,0.2),R.uniform(0,3)),m=mat('pavement'),uv=1.8); jit(p,0.01)
    export('sidewalk_slab_fragments',collider='mesh',kind='static')

# ------------------------------------------------------------------ US traffic signals
def signal_head(loc=(0,0,0),rot=(0,0,0),lights=('red','amber','green')):
    """3-section 12 in vehicle head: yellow housing, black backplate, lenses with tunnel visors, faces -Y"""
    y,m=mat('signal_yellow'),mat('pole_dark'); parts=[box(0.34,0.26,1.06,(0,0,0),m=y,uv=1.0)]
    parts.append(box(0.56,0.02,1.25,(0,0.14,0),m=m,uv=1.0))                         # backplate
    for i,c in enumerate(lights):
        z=0.35-i*0.35; parts.append(cyl(0.15,0.02,(0,-0.135,z),rot=(D(90),0,0),m=mat('lens_'+c),uv=0.5,verts=14))
        v=cyl(0.165,0.26,(0,-0.27,z),rot=(D(90),0,0),m=y,uv=1.0,verts=14,cap=False)   # visor: open tube, lower half removed
        sel(v); bpy.ops.object.mode_set(mode='EDIT'); bpy.ops.mesh.select_all(action='DESELECT'); bpy.ops.object.mode_set(mode='OBJECT')
        for vt in v.data.vertices: vt.select=vt.co.z<z-0.06
        bpy.ops.object.mode_set(mode='EDIT'); bpy.ops.mesh.delete(type='VERT'); bpy.ops.object.mode_set(mode='OBJECT'); parts.append(v)
    xform(parts,loc,rot); return parts
def signal_mast(arm_angle=0,heads_tilt=(0,0)):
    """mast-arm signal: 6.2 m pole on a base plate, 6.5 m arm at 5.6 m, two heads hanging over the lanes"""
    pm=mat('pole_dark'); parts=[box(0.5,0.5,0.05,(0,0,0.025),m=pm,uv=1.0)]
    parts.append(cyl(0.16,6.2,(0,0,3.15),m=pm,uv=1.5,verts=12,r2=0.12))
    parts.append(cyl(0.13,0.15,(0,0,6.3),m=pm,uv=1.0,verts=12))
    arm=[tube((0,0,5.6),(0.15,-6.5,6.1),0.09,pm,1.5,10),tube((0,0,6.1),(0.1,-2.2,5.75),0.04,pm,1.0,6)]
    for k,yy in enumerate((-3.0,-6.0)):
        z=6.0+0.5*yy/6.5*-0.0; arm+=[tube((0.07,yy,5.82-yy*0.075+0.0),(0.07,yy,5.55),0.02,pm,0.5,6)]
        arm+=signal_head((0.07,yy+0.0,4.95),(heads_tilt[k],0,D(-90)))
    xform(arm,(0,0,0),(D(arm_angle),0,0),pivot=(0,0,5.6)); parts+=arm
    parts.append(box(0.25,0.15,0.35,(0.2,0,1.4),m=pm,uv=1.0))    # pedestrian push-button / controller box
    return parts
def signals():
    reset(); signal_mast(); export('traffic_signal_mast',collider='mesh',kind='static'); lod('traffic_signal_mast',0.5)
    reset(); signal_mast(-8,(D(25),D(-10))); export('traffic_signal_mast_sagging',collider='mesh',kind='static'); lod('traffic_signal_mast_sagging',0.5)
    reset()   # fallen: snapped 0.5 m above the base, pole lying on the street, arm on the ground, a head torn off
    pm=mat('pole_dark'); stub=[box(0.5,0.5,0.05,(0,0,0.025),m=pm,uv=1.0),cyl(0.16,0.5,(0,0,0.27),m=pm,uv=1.5,verts=12,r2=0.155)]
    o=cyl(0.03,0.05,(0,0,0.55),m=pm); stub.append(o)
    pole=signal_mast(); pole=[p for p in pole];
    for p in pole[:1]: bpy.data.objects.remove(p)
    pole=pole[1:]
    xform(pole,(0.25,0,-0.34),(0,D(-90),0),pivot=(0,0,0.5))      # topple over: pole lies on the street, the arm flat on the ground
    signal_head((2.2,-1.6,0.17),(D(85),D(12),D(30)))
    export('traffic_signal_fallen',collider='mesh',kind='static'); lod('traffic_signal_fallen',0.5)
    reset()   # post-mounted (corner pedestal), leaning
    pm=mat('pole_dark'); parts=[box(0.4,0.4,0.05,(0,0,0.025),m=pm,uv=1.0),cyl(0.07,3.6,(0,0,1.8),m=pm,uv=1.0,verts=10)]
    parts+=signal_head((0,-0.2,3.0)); xform(parts,(0,0,0),(D(9),D(4),0)); export('traffic_signal_post_leaning',collider='mesh',kind='static')
    reset(); signal_head((0,0,0.17),(D(-90),0,D(20)),('red','amber','green')); export('traffic_signal_head_loose',mass=25,collider='box',kind='dynamic')

# ------------------------------------------------------------------ cones, channelizers
def cone(m=None):
    o=mat('cone_orange'); w=mat('reflective'); parts=[box(0.38,0.38,0.03,(0,0,0.015),m=o,uv=0.5)]
    parts.append(cyl(0.135,0.68,(0,0,0.37),m=o,uv=0.5,verts=16,r2=0.025))
    for z0,z1 in ((0.42,0.52),(0.56,0.62)):   # retroreflective collars
        ra=0.135+(0.025-0.135)*(z0-0.03)/0.68+0.003; rb=0.135+(0.025-0.135)*(z1-0.03)/0.68+0.003
        parts.append(cyl(ra,z1-z0,(0,0,(z0+z1)/2),m=w,uv=0.3,verts=16,r2=rb,cap=False))
    return join(parts)
def cones():
    reset(); cone(); export('traffic_cone',mass=3,collider='box',kind='dynamic')
    reset(); o=cone(); xform([o],(0,0,0),(D(-74),0,D(20)),pivot=(0,0,0)); export('traffic_cone_fallen',mass=3,collider='box',kind='dynamic')
    reset(); o=cone(); subdiv(o,1)
    for v in o.data.vertices: v.co.z*=0.45; v.co.x*=1+0.6*max(0,0.3-abs(v.co.z-0.15)); v.co.y*=1+0.3*max(0,0.3-abs(v.co.z-0.15))
    jit(o,0.01); export('traffic_cone_crushed',mass=3,collider='box',kind='dynamic')
    reset()   # channelizer drum: orange/white bands, black rubber base ring
    o,w=mat('cone_orange'),mat('reflective'); parts=[cyl(0.4,0.1,(0,0,0.05),m=mat('rubber'),uv=0.5,verts=18)]
    for i in range(5):
        z0=0.1+i*0.18; parts.append(cyl(0.29-i*0.012,0.18,(0,0,z0+0.09),m=(w if i in (1,3) else o),uv=0.5,verts=18,r2=0.29-(i+1)*0.012,cap=(i==4)))
    export('traffic_drum',mass=8,collider='cylinder',kind='dynamic')
    reset(); o,w=mat('cone_orange'),mat('reflective'); parts=[cyl(0.4,0.1,(0,0,0.05),m=mat('rubber'),uv=0.5,verts=18)]
    for i in range(5):
        z0=0.1+i*0.18; parts.append(cyl(0.29-i*0.012,0.18,(0,0,z0+0.09),m=(w if i in (1,3) else o),uv=0.5,verts=18,r2=0.29-(i+1)*0.012,cap=(i==4)))
    for p in parts[1:]: xform([p],(0,0,0),(D(-88),0,0),pivot=(0,0,0.29))
    xform(parts[1:],(0,0.15,0)); export('traffic_drum_fallen',mass=8,collider='cylinder',kind='dynamic')

# ------------------------------------------------------------------ US signs
SA=2048; CELL=512
def sign_panel(cell,outline_px,scale,thick=0.003):
    """flat aluminium panel: front face mapped to atlas cell (outline in cell pixels), back = galvanized cell 11"""
    cx,cy=(cell%4)*CELL,(cell//4)*CELL; bx,by=(11%4)*CELL,(11//4)*CELL
    xs=[p[0] for p in outline_px]; ys=[p[1] for p in outline_px]; mx,my=(min(xs)+max(xs))/2,(min(ys)+max(ys))/2
    bm=bmesh.new(); uvl=bm.loops.layers.uv.new('UVMap')
    fv=[bm.verts.new(((px-mx)*scale,-thick/2,(my-py)*scale)) for px,py in outline_px]
    bv=[bm.verts.new(((px-mx)*scale,thick/2,(my-py)*scale)) for px,py in outline_px]
    f=bm.faces.new(fv); b=bm.faces.new(bv[::-1])
    for face,ox,oy in ((f,cx,cy),(b,bx,by)):
        for l in face.loops:
            i=fv.index(l.vert) if l.vert in fv else bv.index(l.vert); px,py=outline_px[i]
            if face is b: px,py=px%CELL*0.8+51,py%CELL*0.8+51
            l[uvl].uv=((ox+px)/SA,1-(oy+py)/SA)
    n=len(fv)
    for i in range(n): bm.faces.new((fv[i],fv[(i+1)%n],bv[(i+1)%n],bv[i]))
    bmesh.ops.recalc_face_normals(bm,faces=bm.faces)
    o=obj_from_bm(bm,'sign',mat('signs')); return o
def oct_px(r=245.76): return [(256+r*math.cos(D(22.5+45*k)),256+r*math.sin(D(22.5+45*k))) for k in range(8)]
SIGNS={   # name: (cell, outline in cell px, metres per px, mounting height of the panel centre)
 'stop':(0,oct_px(),0.76/454,2.1),'yield':(1,[(8,40),(504,40),(256,492)],0.9/496,2.1),
 'speed35':(2,[(56,0),(456,0),(456,512),(56,512)],0.6/400,2.1),'roadwork':(3,[(256,4),(508,256),(256,508),(4,256)],1.07/504,2.1),
 'curve':(4,[(256,4),(508,256),(256,508),(4,256)],1.07/504,2.1),'oneway':(5,[(0,171),(512,171),(512,341),(0,341)],0.9/512,2.4),
 'noparking':(8,[(64,8),(448,8),(448,504),(64,504)],0.3/384,2.1),'donotenter':(9,[(0,0),(512,0),(512,512),(0,512)],0.76/512,2.1),
 'pedxing':(10,[(256,4),(508,256),(256,508),(4,256)],0.91/504,2.1)}
def ustpost(h,m):   # galvanized U-channel post with holes (as box), 2 lb/ft
    return box(0.07,0.035,h,(0,0.02,h/2-0.6),m=m,uv=1.0)
def sign(name,bend=0.0,lean=(0,0),panel_rot=0.0):
    cell,out,s,h=SIGNS[name]; gm=mat('pole_galv'); post=ustpost(h+0.35+0.6,gm); xform([post],(0,0,0))
    p=sign_panel(cell,out,s); xform([p],(0,-0.005,h))
    if panel_rot: xform([p],(0,0,0),(0,panel_rot,0),pivot=(0,0,h))
    up=[post,p]
    if bend:   # post kinked at 0.7 m (car hit): split the post and rotate the upper part
        post2=ustpost(h+0.35+0.6-0.7-0.6,gm); bpy.data.objects.remove(post); xform([post2],(0,0,0.7+0.6))
        low=box(0.07,0.035,1.3,(0,0.02,0.05),m=gm,uv=1.0); up=[post2,p]; xform(up,(0,0,0),(bend,0,D(8)),pivot=(0,0.02,0.7)); up.append(low)
    xform(up,(0,0,0),(lean[0],lean[1],0),pivot=(0,0,0))
    for o in up:   # nothing below ground except the buried 0.6 m of post
        pass
    return up
def street_name_sign():
    gm=mat('pole_galv'); parts=[ustpost(3.4,gm)]
    a=sign_panel(6,[(0,192),(512,192),(512,320),(0,320)],0.9/512); xform([a],(0,0,2.9))
    b=sign_panel(7,[(0,192),(512,192),(512,320),(0,320)],0.9/512); xform([b],(0,0,2.66),(0,0,D(90)))
    return parts+[a,b]
def finish_sign(name,**kw):
    """export with the buried part of the post cut away so the origin (bottom centre) is at ground level"""
    o=join(scene_meshes()); bm=bmesh.new(); bm.from_mesh(o.data)
    bmesh.ops.bisect_plane(bm,geom=bm.verts[:]+bm.edges[:]+bm.faces[:],plane_co=(0,0,0),plane_no=(0,0,1),clear_inner=True)
    bm.to_mesh(o.data); bm.free(); export(name,**kw)
def signs():
    for n in SIGNS:
        reset(); sign(n); finish_sign('sign_'+n,collider='box',kind='static')
    reset(); street_name_sign(); finish_sign('sign_street_name',collider='box',kind='static')
    reset(); sign('stop',bend=D(35)); finish_sign('sign_stop_bent',collider='mesh',kind='static')
    reset(); sign('speed35',lean=(D(-18),D(6)),panel_rot=D(-12)); finish_sign('sign_speed35_leaning',collider='box',kind='static')
    reset(); sign('yield',bend=D(48)); finish_sign('sign_yield_bent',collider='mesh',kind='static')
    reset(); sign('roadwork',lean=(D(-86),0)); o=join(scene_meshes()); xform([o],(0,0,0.04)); finish_sign('sign_roadwork_fallen',collider='box',kind='static')
    reset(); sign('oneway',lean=(0,D(-15)),panel_rot=D(25)); finish_sign('sign_oneway_leaning',collider='box',kind='static')
    reset(); o=sign_panel(0,oct_px(),0.76/454); xform([o],(0,0,0.02),(D(-89),0,D(30))); export('sign_stop_panel_loose',mass=3,collider='box',kind='dynamic')

# ------------------------------------------------------------------ Jersey barriers
JERSEY=[(-0.305,0),(0.305,0),(0.305,0.075),(0.23,0.33),(0.075,0.81),(-0.075,0.81),(-0.23,0.33),(-0.305,0.075)]   # US F/NJ shape, 32 in
def jersey(length,m,chip=None,seed=0):
    o=prism([(y,z) for y,z in JERSEY],length,m,1.0); subdiv(o,2); r=np.random.default_rng(seed)
    for v in o.data.vertices: v.co+=Vector(r.normal(0,0.004,3))
    parts=[o]
    for x in (-length/2+0.35,length/2-0.35): parts.append(box(0.3,0.62,0.06,(x,0,0.03),m=m,uv=1.0))   # forklift slots (as darker pads)
    if chip:   # broken end: cut the top corner away, rebar loops left sticking out
        side=1 if chip>0 else -1; bm=bmesh.new(); bm.from_mesh(o.data)
        bmesh.ops.bisect_plane(bm,geom=bm.verts[:]+bm.edges[:]+bm.faces[:],plane_co=(side*(length/2-0.45),0,0.4),plane_no=(side*0.7,0.1,0.7),clear_outer=True)
        bmesh.ops.contextual_create(bm,geom=[e for e in bm.edges if e.is_boundary]); bm.to_mesh(o.data); bm.free(); cube_uv(o,1.0)
        rm=mat('rust')
        for k in range(4):
            y=-0.15+k*0.1; x0=side*(length/2-0.7)
            parts+=polyline([(x0,y,0.45+k*0.05),(side*(length/2-0.15),y,0.6+r.uniform(-0.1,0.15)),(side*(length/2+0.05),y+r.normal(0,0.05),0.5)],0.008,rm,0.5,6)
        for _ in range(6): c=box(0.15,0.12,0.1,(side*(length/2+r.uniform(0,0.4)),r.normal(0,0.3),0.05),rot=tuple(r.uniform(0,1,3)),m=m,uv=1.0); jit(c,0.02); parts.append(c)
    return join(parts)
def barriers():
    c=lambda: mat('concrete')
    reset(); jersey(3.05,c(),seed=1); export('jersey_barrier',mass=1800,collider='mesh',kind='static')
    reset(); jersey(1.5,c(),seed=2); export('jersey_barrier_short',mass=900,collider='mesh',kind='static')
    reset(); jersey(3.05,c(),chip=1,seed=3); export('jersey_barrier_broken',mass=1600,collider='mesh',kind='static')
    reset(); o=jersey(3.05,c(),seed=4); xform([o],(0,0,0),(D(-90),0,D(7))); export('jersey_barrier_tipped',mass=1800,collider='mesh',kind='static')
    reset()   # a row of three, shoved out of line
    for i,(x,y,a) in enumerate(((-3.1,0,0.02),(0,0.25,-0.12),(3.0,0.1,0.2))): o=jersey(3.05,c(),seed=10+i); xform([o],(x,y,0),(0,0,a))
    export('jersey_barrier_row',collider='mesh',kind='static'); lod('jersey_barrier_row',0.5)

# ------------------------------------------------------------------ railings, guardrail
def pipe_railing(length=2.0,bent=0.0,broken=False,m=None,seed=0):
    """1.07 m (42 in) handrail: 48 mm posts every 1 m on base plates, top and mid rails"""
    m=m or mat('rust'); r=np.random.default_rng(seed); parts=[]; n=int(round(length))+1
    xs=[-length/2+i*length/(n-1) for i in range(n)]
    top=[];mid=[]
    for i,x in enumerate(xs):
        t=1.0-abs(x)/(length/2) if bent else 0
        dz=-bent*t; dy=bent*0.5*t
        top.append((x,dy,1.07+dz)); mid.append((x,dy*0.6,0.53+dz*0.4))
        lean=(dy*0.9,0)
        parts.append(tube((x,0,0),(x,lean[0],1.07+dz),0.024,m,0.6,8)); parts.append(box(0.12,0.12,0.01,(x,0,0.005),m=m,uv=0.5))
    if bent:   # a smooth sag between the posts
        def sag(pts):
            out=[]
            for i in range(len(pts)-1):
                a,b=Vector(pts[i]),Vector(pts[i+1])
                for k in range(4): t=k/4; p=a.lerp(b,t); out.append((p.x,p.y,p.z-bent*0.4*math.sin(t*math.pi)*0.3))
            return out+[pts[-1]]
        top,mid=sag(top),sag(mid)
    if broken:   # right half of the rails gone, one rail end hanging to the ground
        k=len(top)//2+1; top=top[:k]; mid=mid[:k]; e=Vector(top[-1]); parts+=polyline([tuple(e),(e.x+0.5,e.y+0.1,0.6),(e.x+0.8,e.y+0.25,0.02)],0.021,m,0.6,8)
        for p in parts[2*(len(xs)//2+1)+2:2*len(xs)]: pass
    parts+=polyline(top,0.021,m,0.6,8)+polyline(mid,0.018,m,0.6,8)
    return parts
def bar_railing(length=2.0,bent=0.0,m=None):
    """balcony / stair railing: flat top bar, 12 mm square balusters every 0.12 m"""
    m=m or mat('rust2'); parts=[box(length,0.05,0.008,(0,0,1.0),m=m,uv=0.6),box(length,0.04,0.008,(0,0,0.1),m=m,uv=0.6)]
    for x in (-length/2,length/2): parts.append(box(0.04,0.04,1.0,(x,0,0.5),m=m,uv=0.6))
    n=int(length/0.12)
    for i in range(1,n):
        x=-length/2+i*length/n; parts.append(box(0.012,0.012,0.9,(x,0,0.55),m=m,uv=0.6))
    if bent:
        for o in parts:
            for v in o.data.vertices:
                t=max(0,1-abs(v.co.x)/(length/2)); v.co.y+=bent*t*t*(v.co.z/1.0); v.co.z-=bent*0.3*t*t*(v.co.z/1.0)
    return parts
W_BEAM=[(0,0),(0.08,0.035),(0.155,0.0),(0.235,0.035),(0.31,0)]   # (z, depth) of the 12-gauge W-beam, 31 cm tall
def guardrail(length=3.81,crumple=0.0,m=None,seed=0):
    """highway W-beam guardrail on 150x100 mm I-beam posts every 1.905 m, rail centre at 0.6 m"""
    m=m or mat('pole_galv'); r=np.random.default_rng(seed); nx=int(length/0.25)+1
    bm=bmesh.new(); rows=[]
    for i in range(nx):
        x=-length/2+i*length/(nx-1); t=math.sin(math.pi*i/(nx-1))
        dy=crumple*t*(1+0.3*r.normal()); dz=-crumple*0.35*t
        row=[]
        for z,d in W_BEAM: row.append(bm.verts.new((x,-0.2-d+dy+(r.normal(0,0.012) if crumple else 0),0.45+z+dz)))
        rows.append(row)
    for i in range(nx-1):
        for j in range(len(W_BEAM)-1): bm.faces.new((rows[i][j],rows[i+1][j],rows[i+1][j+1],rows[i][j+1]))
    o=obj_from_bm(bm,'wbeam',m,1.5)
    sel(o); md=o.modifiers.new('s','SOLIDIFY'); md.thickness=0.004; bpy.ops.object.modifier_apply(modifier=md.name)
    parts=[o]; pm=mat('rust')
    for k,x in enumerate((-length/2+0.2,0,length/2-0.2)):
        t=math.sin(math.pi*(x+length/2)/length); lean=crumple*t*0.8
        p=box(0.1,0.15,1.0,(x,0,0.5),m=pm,uv=0.6); xform([p],(0,0,0),(-lean,0,0),pivot=(x,0,0)); parts.append(p)
        b=box(0.1,0.2,0.36,(x,-0.1,0.63),m=pm,uv=0.6); xform([b],(0,crumple*t,-crumple*0.35*t)); parts.append(b)   # blockout
    return parts
def railings():
    reset(); pipe_railing(2.0); export('railing_pipe_2m',collider='box',kind='static')
    reset(); pipe_railing(3.0,bent=0.35,seed=1); export('railing_pipe_bent',collider='mesh',kind='static')
    reset(); pipe_railing(3.0,broken=True,seed=2); export('railing_pipe_broken',collider='mesh',kind='static')
    reset(); bar_railing(2.0); export('railing_bars_2m',collider='box',kind='static')
    reset(); bar_railing(2.0,bent=0.3); export('railing_bars_bent',collider='mesh',kind='static')
    reset(); guardrail(3.81); export('guardrail_wbeam',collider='box',kind='static')
    reset(); guardrail(3.81,crumple=0.45,m=mat('rust'),seed=3); export('guardrail_wbeam_crashed',collider='mesh',kind='static')
    reset()   # end terminal: the rail flares down to the ground (buried end)
    parts=guardrail(3.81,m=mat('pole_galv')); o=parts[0]
    for v in o.data.vertices:
        t=max(0,(v.co.x-0.4)/1.5); v.co.z=max(0.005,v.co.z-min(t,1)*0.45); v.co.y+=min(t,1)*0.12
    bpy.data.objects.remove(parts[-2]); bpy.data.objects.remove(parts[-1])
    export('guardrail_wbeam_end',collider='mesh',kind='static')

GROUPS={'drums':drums,'tyres':tyres,'bins':bins,'containers':containers,'ruins':ruins,'street':street,'signals':signals,
        'cones':cones,'signs':signs,'barriers':barriers,'railings':railings}
if __name__=='__main__' or True:
    todo=sys.argv[2:] or list(GROUPS)
    mp=os.path.join(OUT,'deadcity.json'); old=json.load(open(mp)) if os.path.exists(mp) else {}
    for k in todo: GROUPS[k]()
    old.update(MAN); json.dump(dict(sorted(old.items())),open(mp,'w'),indent=1)
