# Procedural low-poly trees with alpha-tested leaf/needle cards (CC0, made for this project).
# Trunks and limbs are tapered tubes with tiled bark; foliage are cards textured from a 2x2 atlas
# baked from Poly Haven (CC0) scanned plants. Blender z-up, exported as glTF +Y up, metres, base at origin.
import bpy,bmesh,math,random,sys,json
from mathutils import Vector,Matrix,Quaternion
cfg=json.loads(sys.argv[1]); R=random.Random(cfg.get('seed',1)); KR=random.Random(cfg.get('seed',1)+99); LOD=cfg.get('lod',0)
H=cfg['height']; sp=cfg['species']; A=cfg['assets']
bpy.ops.wm.read_factory_settings(use_empty=True)
tubes=[]; cards=[]
def perp(d):
    a=Vector((0,0,1)) if abs(d.z)<0.9 else Vector((1,0,0)); return d.cross(a).normalized()
def path(p0,d,L,n,gravity=0.0,noise=0.08,up=0.0):
    pts=[p0.copy()]; d=d.normalized(); p=p0.copy(); seg=L/n
    for i in range(n):
        d=(d+Vector((R.uniform(-1,1),R.uniform(-1,1),R.uniform(-1,1)))*noise+Vector((0,0,up-gravity))*(1.0/n)).normalized()
        p=p+d*seg; pts.append(p.copy())
    return pts
def tube(pts,r0,r1,sides,mat):
    if LOD: sides=max(3,sides-2)
    tubes.append((pts,r0,r1,sides,mat))
def card(c,u,v,w,h,cell,ox=0.5,oy=0.5,nrm_c=None,mat='leaves'):
    if LOD and KR.random()>0.6: return
    cards.append((c,u.normalized(),v.normalized(),w,h,cell,ox,oy,nrm_c,mat))
OUT=cfg.get('crown')=='out'   # street atlas (leafcards.py): leaf rosettes facing out of the crown
def out_cluster(c,s,ctr,mat='leaves',extra=True):
    # SpeedTree-like: the main card faces away from the crown centre (rolled at random), a crossing card gives it
    # depth from the side, low outer clusters let a spray hang down (ragged underside instead of a flat bottom)
    nr=c-ctr
    nr=nr.normalized() if nr.length>1e-3 else Vector((0,0,1))
    nr=(nr+Vector((R.uniform(-1,1),R.uniform(-1,1),R.uniform(-1,1)))*0.35).normalized()
    u=perp(nr); u=Quaternion(nr,R.uniform(0,6.28))@u; v=nr.cross(u).normalized()
    card(c,u,v,s*R.uniform(0.9,1.1),s*R.uniform(0.9,1.1),R.choice([0,1,2,0,1]),nrm_c=ctr,mat=mat)
    if extra:
        card(c,u,nr,s*0.8,s*0.8,2,nrm_c=ctr,mat=mat)
    if nr.z<-0.1 and R.random()<0.45:
        a=R.uniform(0,6.28); hu=Vector((math.cos(a),math.sin(a),0))
        card(c+Vector((0,0,s*0.15)),hu,Vector((0,0,1)),s*0.9,s*1.1,3,0.5,1.0,nrm_c=ctr,mat=mat)
def cross_cluster(c,s,cells,ctr,nh=True):
    if OUT:
        out_cluster(c,s*0.85,ctr,extra=nh); return
    ang=R.uniform(0,math.pi)
    for k in range(2):
        a=ang+k*math.pi/2; u=Vector((math.cos(a),math.sin(a),0))
        v=Vector((0,0,1)); rot=Quaternion(u.cross(v),R.uniform(-0.3,0.3)); v=rot@v
        card(c,u,v,s*R.uniform(0.85,1.15),s*R.uniform(0.85,1.15),R.choice(cells),nrm_c=ctr)
    if nh:
        a=R.uniform(0,6.28); u=Vector((math.cos(a),math.sin(a),0)); v=Vector((-math.sin(a),math.cos(a),R.uniform(-0.25,0.25)))
        card(c+Vector((0,0,s*0.1)),u,v,s,s,R.choice(cells),nrm_c=ctr)
def keep(): return True
def csz(s): return s*(1.2 if LOD else 1.0)
def at(pts,t):
    f=t*(len(pts)-1); i=min(int(f),len(pts)-2); return pts[i].lerp(pts[i+1],f-i)
def dirat(pts,t):
    f=t*(len(pts)-1); i=min(int(f),len(pts)-2); return (pts[i+1]-pts[i]).normalized()
S=H/15.0
if sp in('birch','broadleaf','dead','snag'):
    lean=Vector((R.uniform(-0.04,0.04),R.uniform(-0.04,0.04),1))
    th=H*(0.62 if sp=='snag' else 1.0)
    trunk=path(Vector((0,0,0)),lean,th,10 if not LOD else 6,noise=0.03 if sp!='dead' else 0.06)
    r0={'birch':0.16,'broadleaf':0.28,'dead':0.22,'snag':0.24}[sp]*max(S,0.35)
    tube(trunk,r0,r0*(0.45 if sp=='snag' else 0.08),8,'bark')
    ctr=Vector((0,0,H*0.68))
    if sp=='birch':
        n=int(10+12*S); 
        for i in range(n):
            t=0.32+0.62*i/n; p=at(trunk,t); a=i*2.4+R.uniform(-0.3,0.3)
            out=Vector((math.cos(a),math.sin(a),0)); el=math.radians(35+35*t)
            d=(out*math.cos(el)+Vector((0,0,math.sin(el)))); L=(0.34*H*(1-t)**0.6+0.6)*R.uniform(0.8,1.1)
            lp=path(p,d,L,4,gravity=0.9 if t<0.8 else 0.3,noise=0.12)
            tube(lp,r0*0.32*(1.1-t),0.01,5,'bark')
            for tt in (0.45,0.75,1.0):
                if keep(): cross_cluster(at(lp,tt)+Vector((0,0,-0.2)),csz(R.uniform(1.3,1.9)*max(S,0.45)),[0,1,2,3],ctr,nh=tt==1.0)
        for tt in (0.9,1.0): cross_cluster(at(trunk,tt),csz(1.6*max(S,0.45)),[0,1,2,3],ctr)
    elif sp=='broadleaf':
        split=0.42; base=at(trunk,split)
        trunk[:]=trunk[:int(len(trunk)*split)+2]; tubes[-1]=(trunk,r0,r0*0.55,8 if not LOD else 6,'bark')
        nm=R.choice([3,4])
        for m in range(nm):
            a=m*6.28/nm+R.uniform(-0.4,0.4); d=Vector((math.cos(a)*0.55,math.sin(a)*0.55,1))
            lp=path(base,d,H*0.5,5,noise=0.12,gravity=0.3); tube(lp,r0*0.55,0.04,6,'bark')
            for j in range(4):
                t=0.3+0.17*j; p=at(lp,t); b=a+R.uniform(-1.2,1.2); dd=Vector((math.cos(b),math.sin(b),R.uniform(0.2,0.8)))
                sl=path(p,dd,H*0.22*(1.2-t),3,noise=0.15,gravity=0.4); tube(sl,r0*0.18,0.01,4,'bark')
                if keep(): cross_cluster(at(sl,1.0),csz(R.uniform(2.0,2.8)*max(S,0.5)),[0,1,2,3],ctr)
                if keep(): cross_cluster(at(sl,0.5),csz(R.uniform(1.6,2.2)*max(S,0.5)),[0,1,2,3],ctr,nh=False)
            cross_cluster(at(lp,1.0),csz(2.6*max(S,0.5)),[0,1,2,3],ctr)
            for tt in (0.55,0.8): cross_cluster(at(lp,tt),csz(2.4*max(S,0.5)),[0,1,2,3],ctr,nh=False)
        for k in range(4):
            a=R.uniform(0,6.28); r=R.uniform(0.3,1.5)*max(S,0.5)
            cross_cluster(Vector((math.cos(a)*r,math.sin(a)*r,H*R.uniform(0.62,0.85))),csz(2.8*max(S,0.5)),[0,1,2,3],ctr)
    else:  # dead / snag: bare limbs, broken ends
        n=int((7 if sp=='dead' else 3)+4*S)
        for i in range(n):
            t=(0.35 if sp=='dead' else 0.3)+0.6*R.random(); p=at(trunk,t); a=R.uniform(0,6.28)
            d=Vector((math.cos(a),math.sin(a),R.uniform(0.1,0.9))); L=(H*0.32*(1.05-t)+0.5)*(0.35 if sp=='snag' else 1)*R.uniform(0.6,1.1)
            lp=path(p,d,L,4,noise=0.25,gravity=0.1); tube(lp,r0*0.3,0.02,5,'bark')
            if sp=='dead':
                for j in range(2):
                    q=at(lp,R.uniform(0.3,0.8)); b=a+R.uniform(-1,1); sl=path(q,Vector((math.cos(b),math.sin(b),R.uniform(0,1))),L*0.45,3,noise=0.3)
                    tube(sl,r0*0.1,0.01,4,'bark')
elif sp=='sapling':   # self-seeded young broadleaf (maple / ash / locust) growing out of cracks: 2-4 slender stems, leafy top
    ctr=Vector((0,0,H*0.62)); ns=R.choice([2,3,3,4])
    for m in range(ns):
        a=m*6.28/ns+R.uniform(-0.5,0.5); sl=R.uniform(0.08,0.28)
        st=path(Vector((math.cos(a)*0.05,math.sin(a)*0.05,0)),Vector((math.cos(a)*sl,math.sin(a)*sl,1)),H*R.uniform(0.75,1.0),7 if not LOD else 4,noise=0.06,gravity=0.05)
        tube(st,0.035*max(H/5,0.6),0.008,6,'bark')
        nb=int(4+H)
        for j in range(nb):
            t=0.35+0.6*j/nb; p=at(st,t); b=R.uniform(0,6.28); d=Vector((math.cos(b),math.sin(b),R.uniform(0.3,1.0)))
            L=H*0.25*(1.15-t)*R.uniform(0.7,1.1); lp=path(p,d,L,3,noise=0.15,gravity=0.35); tube(lp,0.012,0.004,4,'bark')
            if keep(): cross_cluster(at(lp,1.0),csz(R.uniform(0.9,1.4)*max(H/5,0.6)),[0,1,2,3],ctr,nh=j%2==0)
            if keep() and t>0.5: cross_cluster(at(lp,0.5),csz(R.uniform(0.7,1.1)*max(H/5,0.6)),[0,1,2,3],ctr,nh=False)
        cross_cluster(at(st,1.0),csz(1.3*max(H/5,0.6)),[0,1,2,3],ctr)
elif sp in('maple','ash'):   # middle-aged broadleaf with a full crown: short trunk, scaffold limbs, clusters filling an ellipsoid
    lean=Vector((R.uniform(-0.03,0.03),R.uniform(-0.03,0.03),1)); r0=0.021*H
    tb=0.32 if sp=='maple' else 0.4
    trunk=path(Vector((0,0,0)),lean,H*tb,6 if not LOD else 4,noise=0.03); tube(trunk,r0,r0*0.7,8,'bark')
    cz=H*(0.6 if sp=='maple' else 0.64); rx=H*(0.34 if sp=='maple' else 0.25); rz=H*(0.36 if sp=='maple' else 0.38)
    ctr=Vector((0,0,cz)); top=trunk[-1]; limbs=[]
    nl=R.choice([5,6]) if sp=='maple' else R.choice([4,5])
    for m in range(nl):
        a=m*6.28/nl+R.uniform(-0.3,0.3); out=Vector((math.cos(a),math.sin(a),0))
        tgt=ctr+out*rx*R.uniform(0.6,0.85)+Vector((0,0,rz*R.uniform(-0.1,0.7)))
        lp=path(top,(tgt-top).normalized(),(tgt-top).length,5,noise=0.08); tube(lp,r0*0.6,r0*0.12,6,'bark'); limbs.append(lp)
        for j in range(3):   # secondary branches
            q=at(lp,0.35+0.2*j); b=a+R.uniform(-1.0,1.0); d=Vector((math.cos(b),math.sin(b),R.uniform(0.1,0.8)))
            sl=path(q,d,rx*R.uniform(0.35,0.6),3,noise=0.15,gravity=0.15); tube(sl,r0*0.18,0.01,4,'bark'); limbs.append(sl)
    pts=[p_ for l in limbs for p_ in l[1:]]
    nc=int((60 if sp=='maple' else 50)*(H/10)**1.2); cs=2.0*(H/10)**0.6
    if OUT: nc=int(nc*3.6); cs*=0.85
    # a lumpy crown: clusters gather in clumps around the limb ends, with gaps between them
    clumps=[l[-1] for l in limbs[::4]]+[ctr+Vector((0,0,rz*0.55))]
    for i in range(nc):
        while True:
            v=Vector((R.uniform(-1,1),R.uniform(-1,1),R.uniform(-1,1)))
            if 0.3<v.length<=1.0: break
        c=ctr+Vector((v.x*rx,v.y*rx,v.z*rz)); rr=v.length
        if OUT:
            k=min(clumps,key=lambda q:(q-c).length)
            if (k-c).length>rx*0.8 and R.random()<0.35: continue
            c=c.lerp(k,0.25); q=c-ctr; rr=Vector((q.x/rx,q.y/rx,q.z/rz)).length   # shade by where it ended up
        near=min(pts,key=lambda q:(q-c).length)
        if (near-c).length>0.4: tube([near,near.lerp(c,0.5),c],0.025,0.006,3,'bark')
        cmat='leaves' if rr>0.72 else 'leaves_dark'
        sz=csz(cs*R.uniform(0.85,1.2)*(1.0 if rr>0.72 else 0.9))
        if OUT:
            if keep(): out_cluster(c,sz,ctr,'leaves',extra=rr>0.72 or i%2==0)   # the shadow map darkens the inside
            continue
        ang=R.uniform(0,math.pi)
        for k in range(2):
            a=ang+k*math.pi/2; u=Vector((math.cos(a),math.sin(a),0)); vv=Quaternion(u.cross(Vector((0,0,1))),R.uniform(-0.3,0.3))@Vector((0,0,1))
            card(c,u,vv,sz,sz,R.choice([0,1,2,3]),nrm_c=ctr,mat=cmat)
        if rr>0.72 or i%2==0:
            a=R.uniform(0,6.28); u=Vector((math.cos(a),math.sin(a),0)); vv=Vector((-math.sin(a),math.cos(a),R.uniform(-0.25,0.25)))
            card(c+Vector((0,0,sz*0.1)),u,vv,sz,sz,R.choice([0,1,2,3]),nrm_c=ctr,mat=cmat)
elif sp=='bush':   # dense self-seeded shrub (elder, hazel, lilac gone wild): many stems from the ground, a mound of leaves
    W=cfg.get('width',H*1.3); ctr=Vector((0,0,H*0.45))
    for m in range(int(6+4*H)):
        a=R.uniform(0,6.28); sl=R.uniform(0.2,0.7)
        st=path(Vector((math.cos(a)*0.08,math.sin(a)*0.08,0)),Vector((math.cos(a)*sl,math.sin(a)*sl,1)),H*R.uniform(0.6,1.0),4,noise=0.12,gravity=0.15)
        tube(st,0.025,0.006,4,'bark')
    n=int(70*(W*H/3.0)**0.8)
    for i in range(n):
        while True:
            v=Vector((R.uniform(-1,1),R.uniform(-1,1),R.uniform(-1,1)))
            if 0.25<v.length<=1.0: break
        c=ctr+Vector((v.x*W/2,v.y*W/2,v.z*H*0.55))
        if c.z<0.15: c.z=0.15+R.uniform(0,0.2)
        rr=v.length
        if keep(): out_cluster(c,csz(R.uniform(0.75,1.05)*min(1.3,0.55+0.25*H)),ctr,'leaves',extra=rr>0.65)
elif sp in('pine','spruce'):
    trunk=path(Vector((0,0,0)),Vector((R.uniform(-0.02,0.02),R.uniform(-0.02,0.02),1)),H,10 if not LOD else 6,noise=0.02 if sp=='spruce' else 0.035)
    r0=(0.22 if sp=='pine' else 0.2)*max(S,0.3); tube(trunk,r0,0.02,8,'bark'); ctr=Vector((0,0,H*(0.8 if sp=='pine' else 0.5)))
    if sp=='spruce':
        nw=int(H/0.55); 
        for w in range(nw):
            t=0.07+0.93*w/nw; p=at(trunk,t); L=(0.27*H*(1-t)**0.9+0.25)*R.uniform(0.85,1.1); k=5 if H>4 else 4
            for b in range(k):
                if not keep(): continue
                a=w*0.7+b*6.28/k+R.uniform(-0.2,0.2); out=Vector((math.cos(a),math.sin(a),0))
                d=(out+Vector((0,0,-0.25+0.4*t))).normalized()
                side=perp(d); 
                if t<0.75: tube([p,p+d*L*0.85],r0*0.12,0.01,3,'bark')
                card(p+d*L*0.5,side,d,L*0.8,L*1.05,3,nrm_c=ctr)
                up=d.cross(side).normalized(); card(p+d*L*0.5+up*0.05,(side*0.5+up).normalized(),d,L*0.55,L*1.0,R.choice([1,3]),nrm_c=ctr)
        tip=at(trunk,1.0); cross_cluster(tip-Vector((0,0,0.5*max(S,0.3))),1.2*max(S,0.3),[3],ctr,nh=False)
    else:
        n=int(10+8*S)
        for i in range(n):
            t=0.6+0.38*i/n; p=at(trunk,t); a=i*2.4; out=Vector((math.cos(a),math.sin(a),0))
            d=(out+Vector((0,0,0.2+0.7*(t-0.6)))).normalized(); L=(0.15*H*(1.05-t)/0.4+0.5)*R.uniform(0.7,1.0)
            lp=path(p,d,L,3,noise=0.1,up=0.6); tube(lp,r0*0.28,0.03,5,'bark')
            for tt in (0.35,0.7,1.0):
                c=at(lp,tt); s_=csz(R.uniform(2.0,2.8)*max(S,0.45))
                a2=R.uniform(0,6.28); u=Vector((math.cos(a2),math.sin(a2),R.uniform(-0.25,0.25))); v=Vector((-math.sin(a2),math.cos(a2),R.uniform(-0.25,0.25)))
                card(c,u,v,s_,s_,R.choice([0,1]),nrm_c=ctr)
                card(c+Vector((0,0,-0.1)),u.cross(Vector((0,0,1))).normalized(),Vector((0,0,1)),s_*0.85,s_*0.7,R.choice([0,1]),nrm_c=ctr)
        for k in range(3):
            cross_cluster(at(trunk,0.82+0.09*k),csz(2.4*max(S,0.45)),[0,1],ctr)
        # dead stubs on the bare lower trunk
        for i in range(int(6*S)+2):
            t=R.uniform(0.25,0.55); p=at(trunk,t); a=R.uniform(0,6.28)
            tube(path(p,Vector((math.cos(a),math.sin(a),-0.1)),R.uniform(0.3,0.8),2,noise=0.2),r0*0.12,0.01,3,'bark')
# ---- build meshes
def build_tubes():
    bm=bmesh.new(); uvl=bm.loops.layers.uv.new('UV')
    for pts,r0,r1,sides,_ in tubes:
        n=len(pts); L=[0.0]
        for i in range(1,n): L.append(L[-1]+(pts[i]-pts[i-1]).length)
        tot=L[-1] or 1; d0=(pts[1]-pts[0]).normalized(); nrm=perp(d0); rings=[]
        urep=max(1,round(2*math.pi*r0/0.7))
        for i in range(n):
            d=(pts[min(i+1,n-1)]-pts[max(i-1,0)]).normalized()
            nrm=(nrm-d*nrm.dot(d)).normalized(); bn=d.cross(nrm)
            r=r0+(r1-r0)*(L[i]/tot)**0.8
            if i==0 and r0>0.1: r*=1.25  # root flare
            ring=[bm.verts.new(pts[i]+(nrm*math.cos(2*math.pi*k/sides)+bn*math.sin(2*math.pi*k/sides))*r) for k in range(sides)]
            rings.append(ring)
        for i in range(n-1):
            for k in range(sides):
                k2=(k+1)%sides; f=bm.faces.new((rings[i][k],rings[i][k2],rings[i+1][k2],rings[i+1][k]))
                us=[k/sides*urep,(k+1)/sides*urep,(k+1)/sides*urep,k/sides*urep]; vs=[L[i]/1.6,L[i]/1.6,L[i+1]/1.6,L[i+1]/1.6]
                for lp,u,v in zip(f.loops,us,vs): lp[uvl].uv=(u,v)
    me=bpy.data.meshes.new('bark'); bm.to_mesh(me); bm.free(); return me
def build_cards(which='leaves'):
    bm=bmesh.new(); uvl=bm.loops.layers.uv.new('UV'); nrms=[]
    for c,u,v,w,h,cell,ox,oy,ctr,mm in cards:
        if mm!=which: continue
        col=cell%2; row=cell//2; u0=col*0.5; v0=1-(row+1)*0.5
        cs=[(-ox,-oy),(1-ox,-oy),(1-ox,1-oy),(-ox,1-oy)]
        vs=[bm.verts.new(c+u*(a*w)+v*(b*h)) for a,b in cs]
        f=bm.faces.new(vs)
        for lp,(a,b) in zip(f.loops,cs): lp[uvl].uv=(u0+(a+ox)*0.5*0.996+0.001,v0+(b+oy)*0.5*0.996+0.001)
        for vv in vs:
            nn=(vv.co-ctr) if ctr else Vector((0,0,1)); nn.z=nn.z*0.6+abs(nn.length)*0.4; nrms.append(nn.normalized())
    me=bpy.data.meshes.new('leaves'); bm.to_mesh(me); bm.free()
    if len(me.vertices): me.normals_split_custom_set_from_vertices(nrms)
    return me
def mat(name,diff,nor=None,alpha=False):
    m=bpy.data.materials.new(name); m.use_nodes=True; nt=m.node_tree; b=nt.nodes['Principled BSDF']
    t=nt.nodes.new('ShaderNodeTexImage'); t.image=bpy.data.images.load(diff); nt.links.new(t.outputs['Color'],b.inputs['Base Color'])
    if alpha: nt.links.new(t.outputs['Alpha'],b.inputs['Alpha'])
    if nor:
        tn=nt.nodes.new('ShaderNodeTexImage'); tn.image=bpy.data.images.load(nor); tn.image.colorspace_settings.name='Non-Color'
        nm=nt.nodes.new('ShaderNodeNormalMap'); nt.links.new(tn.outputs['Color'],nm.inputs['Color']); nt.links.new(nm.outputs['Normal'],b.inputs['Normal'])
    b.inputs['Roughness'].default_value=0.9 if not alpha else 1.0
    if alpha and 'Specular IOR Level' in b.inputs: b.inputs['Specular IOR Level'].default_value=0.15; b.inputs['Metallic'].default_value=0
    return m
objs=[]
me=build_tubes(); o=bpy.data.objects.new(cfg['name']+'_bark',me); bpy.context.scene.collection.objects.link(o)
me.materials.append(mat(cfg['name']+'_bark',A['bark'],A.get('bark_nor'))); objs.append(o)
for which,key in (('leaves','cards'),('leaves_dark','cards_dark')):
    if not any(c[9]==which for c in cards): continue
    me2=build_cards(which); o2=bpy.data.objects.new(cfg['name']+'_'+which,me2); bpy.context.scene.collection.objects.link(o2)
    me2.materials.append(mat(cfg['name']+'_'+which,A.get(key,A['cards']),None,True)); objs.append(o2)
for x in objs: x.select_set(True)
bpy.context.view_layer.objects.active=objs[0]; bpy.ops.object.join()
o=bpy.context.object; o.name=cfg['name']
for im in bpy.data.images:
    mx=(1024 if im.filepath in (A['cards'],A.get('cards_dark')) else 512)//(2 if LOD else 1)
    if im.size[0]>mx: im.scale(mx,max(1,im.size[1]*mx//im.size[0]))
tris=sum(len(p.vertices)-2 for p in o.data.polygons)
bpy.ops.export_scene.gltf(filepath=cfg['out'],export_format='GLB',use_selection=True,export_yup=True,export_image_format='AUTO')
print('TREE',cfg['name'],'lod',LOD,'tris',tris,'dims',[round(x,2) for x in o.dimensions])
