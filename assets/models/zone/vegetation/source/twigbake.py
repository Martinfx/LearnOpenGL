# Bake twig cards (albedo+alpha, normal, AO) from scanned leaf sprites: one twig per call, Cycles, orthographic top view.
# Twig grows from the bottom centre of the image (card origin) toward the top, leaves face the camera (+Z).
import bpy,sys,json,math,random
from mathutils import Vector,Matrix,Quaternion
cfg=json.loads(sys.argv[-1]); R=random.Random(cfg['seed'])
bpy.ops.wm.read_factory_settings(use_empty=True)
lib=json.load(open(cfg['boxes'])); S=lib['size']; B={b['i']:b for b in lib['boxes']}
img=bpy.data.images.load(cfg['leaf_rgba']); nimg=bpy.data.images.load(cfg['leaf_nor']); nimg.colorspace_settings.name='Non-Color'
bimg=bpy.data.images.load(cfg['stem']);
MODE=cfg['mode']  # albedo | normal | ao
def material(name,im,alpha,nor=None):
    m=bpy.data.materials.new(name); m.use_nodes=True; nt=m.node_tree
    for n in list(nt.nodes): nt.nodes.remove(n)
    out=nt.nodes.new('ShaderNodeOutputMaterial'); t=nt.nodes.new('ShaderNodeTexImage'); t.image=im
    em=nt.nodes.new('ShaderNodeEmission'); tr=nt.nodes.new('ShaderNodeBsdfTransparent'); mx=nt.nodes.new('ShaderNodeMixShader')
    if MODE=='albedo':
        nt.links.new(t.outputs['Color'],em.inputs['Color'])
    elif MODE=='ao':
        ao=nt.nodes.new('ShaderNodeAmbientOcclusion'); ao.inputs['Distance'].default_value=cfg.get('ao_dist',0.06); ao.samples=16
        nt.links.new(ao.outputs['AO'],em.inputs['Color'])
    else:
        geo=nt.nodes.new('ShaderNodeNewGeometry'); nv=geo.outputs['Normal']
        if nor is not None:
            tn=nt.nodes.new('ShaderNodeTexImage'); tn.image=nor; nm=nt.nodes.new('ShaderNodeNormalMap'); nm.inputs['Strength'].default_value=1.0
            nt.links.new(tn.outputs['Color'],nm.inputs['Color']); nv=nm.outputs['Normal']
        # facing away from the camera: flip (the card is seen from +Z)
        fl=nt.nodes.new('ShaderNodeVectorMath'); fl.operation='SCALE'; nt.links.new(nv,fl.inputs[0])
        sgn=nt.nodes.new('ShaderNodeMath'); sgn.operation='MULTIPLY_ADD'; nt.links.new(geo.outputs['Backfacing'],sgn.inputs[0]); sgn.inputs[1].default_value=-2; sgn.inputs[2].default_value=1
        nt.links.new(sgn.outputs[0],fl.inputs['Scale'])
        sc=nt.nodes.new('ShaderNodeVectorMath'); sc.operation='MULTIPLY_ADD'; nt.links.new(fl.outputs[0],sc.inputs[0]); sc.inputs[1].default_value=(0.5,0.5,0.5); sc.inputs[2].default_value=(0.5,0.5,0.5)
        nt.links.new(sc.outputs[0],em.inputs['Color'])
    if alpha: nt.links.new(t.outputs['Alpha'],mx.inputs[0])
    else: mx.inputs[0].default_value=1.0
    nt.links.new(tr.outputs[0],mx.inputs[1]); nt.links.new(em.outputs[0],mx.inputs[2]); nt.links.new(mx.outputs[0],out.inputs['Surface'])
    return m
leafmat=material('leaf',img,True,nimg); stemmat=material('stem',bimg,False)
import bmesh
bm=bmesh.new(); uvl=bm.loops.layers.uv.new('UV'); mats=[]   # (face, matindex)
def leaf(base,d,roll,lid,length,fold,droop,twist=0.0):
    b=B[lid]; ori=cfg['orient']   # where the petiole is in the sprite: right | bottom
    x0,x1,y0,y1=b['x0'],b['x1'],b['y0'],b['y1']
    L=(x1-x0) if ori=='right' else (y1-y0); W=(y1-y0) if ori=='right' else (x1-x0); wid=length*W/L
    d=d.normalized(); up=Vector((0,0,1)); side=up.cross(d)
    if side.length<1e-3: side=Vector((1,0,0))
    side.normalize(); nrm=d.cross(side).normalized()
    q=Quaternion(d,roll); side=q@side; nrm=q@nrm
    NX,NY=7,4; vs=[]
    for i in range(NX+1):
        s=i/NX; row=[]
        tw=Quaternion(d,twist*s)
        for j in range(NY+1):
            w=j/NY-0.5
            p=base+d*(s*length)+(tw@side)*(w*wid)+(tw@nrm)*(abs(w)*2*fold*wid*0.5-droop*length*s*s)
            px=(x1-s*(x1-x0)) if ori=='right' else (x0+(w+0.5)*(x1-x0))
            py=(y0+(w+0.5)*(y1-y0)) if ori=='right' else (y1-s*(y1-y0))
            row.append((bm.verts.new(p),(px/S,1-py/S)))
        vs.append(row)
    for i in range(NX):
        for j in range(NY):
            q4=[vs[i][j],vs[i+1][j],vs[i+1][j+1],vs[i][j+1]]
            f=bm.faces.new([v for v,_ in q4]); f.material_index=0
            for lp,(_,uv) in zip(f.loops,q4): lp[uvl].uv=uv
def stem(pts,r0,r1,sides=5):
    n=len(pts); rings=[]
    for i,p in enumerate(pts):
        d=(pts[min(i+1,n-1)]-pts[max(i-1,0)]).normalized(); a=Vector((0,0,1)) if abs(d.z)<0.9 else Vector((1,0,0))
        u=d.cross(a).normalized(); v=d.cross(u); r=r0+(r1-r0)*i/(n-1)
        rings.append([bm.verts.new(p+(u*math.cos(6.283*k/sides)+v*math.sin(6.283*k/sides))*r) for k in range(sides)])
    for i in range(n-1):
        for k in range(sides):
            k2=(k+1)%sides; f=bm.faces.new((rings[i][k],rings[i][k2],rings[i+1][k2],rings[i+1][k])); f.material_index=1
            for lp,uv in zip(f.loops,[(k/sides*0.3,i/n),((k+1)/sides*0.3,i/n),((k+1)/sides*0.3,(i+1)/n),(k/sides*0.3,(i+1)/n)]): lp[uvl].uv=uv
def curve(p0,d,L,n,bend,wob=0.15):
    pts=[p0.copy()]; d=d.normalized()
    for i in range(n):
        d=(d+Vector((R.uniform(-wob,wob),R.uniform(-wob,wob),0))+bend/n).normalized(); pts.append(pts[-1]+d*L/n)
    return pts
def at(pts,t):
    f=t*(len(pts)-1); i=min(int(f),len(pts)-2); return pts[i].lerp(pts[i+1],f-i),(pts[i+1]-pts[i]).normalized()
P=cfg['twig']; ids=cfg['leaves']
def shoot(p0,d,L,nleaf,lsz,r0,depth=0):
    pts=curve(p0,d,L,8,Vector((R.uniform(-0.15,0.15),0,P.get('bendz',0.0))))
    stem(pts,r0,r0*0.3)
    side=1 if R.random()<0.5 else -1
    for k in range(nleaf):
        t=P.get('t0',0.12)+(1-P.get('t0',0.12))*(k+R.uniform(0,0.5))/nleaf; p,dd=at(pts,min(t,1.0))
        if P.get('opposite'): sides=(1,-1)
        else: side=-side; sides=(side,)
        for sd in sides:
            ang=math.radians(R.uniform(*P.get('angle',(40,70))))*sd
            ld=Quaternion(Vector((0,0,1)),ang)@dd
            ld.z+=R.uniform(-0.25,0.15)
            s=lsz*R.uniform(0.75,1.1)*(0.75+0.35*min(1,t*1.6))
            leaf(p,ld,R.uniform(-0.5,0.5),R.choice(ids),s,P.get('fold',0.12),R.uniform(*P.get('droop',(0.0,0.15))),R.uniform(-0.3,0.3))
    if P.get('terminal',True):
        p,dd=at(pts,1.0); leaf(p,dd,R.uniform(-0.3,0.3),R.choice(ids),lsz*R.uniform(0.9,1.1),P.get('fold',0.12),0.05)
    return pts
main=shoot(Vector((0,0.0,0)),Vector((R.uniform(-0.1,0.1),1,0)),P['length'],P['leaves'],P['leaf'],P.get('r',0.006))
for k in range(P.get('side_shoots',0)):
    t=0.25+0.6*k/max(1,P['side_shoots'])+R.uniform(-0.05,0.05); p,dd=at(main,t); sd=1 if k%2==0 else -1
    shoot(p,Quaternion(Vector((0,0,1)),math.radians(R.uniform(30,55))*sd)@dd,P['length']*R.uniform(0.35,0.55)*(1.2-t),max(2,P['leaves']//3),P['leaf']*0.9,P.get('r',0.006)*0.6,1)
me=bpy.data.meshes.new('twig'); bm.to_mesh(me); bm.free(); me.materials.append(leafmat); me.materials.append(stemmat)
o=bpy.data.objects.new('twig',me); bpy.context.scene.collection.objects.link(o)
sz=cfg['size']; cam=bpy.data.cameras.new('c'); cam.type='ORTHO'; cam.ortho_scale=sz; cam.clip_end=10
c=bpy.data.objects.new('c',cam); c.location=(0,sz/2,3); bpy.context.scene.collection.objects.link(c); bpy.context.scene.camera=c
sc=bpy.context.scene; sc.render.engine='CYCLES'; sc.cycles.samples=cfg.get('samples',24); sc.cycles.use_denoising=False
sc.render.resolution_x=sc.render.resolution_y=cfg.get('res',1024); sc.render.film_transparent=True
sc.view_settings.view_transform='Standard'; sc.render.image_settings.color_mode='RGBA'; sc.render.image_settings.color_depth='8'
if MODE!='albedo':
    sc.render.image_settings.file_format='OPEN_EXR'; sc.render.image_settings.color_depth='32'
sc.cycles.transparent_max_bounces=64; sc.cycles.max_bounces=4
import numpy as np
exr=cfg['out']+('.exr' if MODE!='albedo' else '')
sc.render.filepath=exr; bpy.ops.render.render(write_still=True)
if MODE!='albedo':
    im=bpy.data.images.load(exr); a=np.zeros(im.size[0]*im.size[1]*4,np.float32); im.pixels.foreach_get(a)
    np.save(cfg['out'],a.reshape(im.size[1],im.size[0],4)[::-1])
print('TWIG',cfg['out'],len(me.polygons))
