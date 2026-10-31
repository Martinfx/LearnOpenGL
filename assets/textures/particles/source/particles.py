#!/usr/bin/env python3
"""Render the particle textures in assets/textures/particles/ (headless Blender as a Python module).

    python3 -I assets/textures/particles/source/particles.py [only]      # pip install bpy numpy

Everything here is original work rendered for this project (CC0). The debris chips use the Poly Haven
CC0 scans already in assets/textures/pbr/ (concrete_wall_007, rough_wood).

Flipbooks are cols x rows tiles (the numbers in the file name), frame 0 top-left, row-major, left to right.
All PNGs are straight (not premultiplied) RGBA. Volumes are rendered in Cycles as real 3D smoke and fire
(noise-shaped density, sun + sky light, blackbody emission). For the emissive sheets (fire, explosion,
muzzle flash, spark) alpha = coverage of the light, so they also work additively.
"""
import os,sys,math
import numpy as np
import bpy
HERE=os.path.dirname(os.path.abspath(__file__)); OUT=os.path.normpath(os.path.join(HERE,'..'))
PBR=os.path.normpath(os.path.join(OUT,'..','pbr'))
ONLY=sys.argv[1:]

def reset(res):
    bpy.ops.wm.read_factory_settings(use_empty=True)
    s=bpy.context.scene; s.render.engine='CYCLES'; s.cycles.device='CPU'
    s.render.resolution_x=s.render.resolution_y=res; s.render.film_transparent=True
    s.cycles.samples=48; s.cycles.use_denoising=True; s.cycles.volume_step_rate=1.0; s.cycles.volume_max_steps=256
    s.view_settings.view_transform='Standard'; s.view_settings.look='None'
    s.render.image_settings.file_format='OPEN_EXR'; s.render.image_settings.color_depth='32'
    w=bpy.data.worlds.new('w'); s.world=w; w.use_nodes=True
    w.node_tree.nodes['Background'].inputs[0].default_value=(0.55,0.6,0.68,1); w.node_tree.nodes['Background'].inputs[1].default_value=1.0
    cam=bpy.data.objects.new('cam',bpy.data.cameras.new('cam')); s.collection.objects.link(cam); s.camera=cam
    cam.data.type='ORTHO'; cam.data.ortho_scale=2.0; cam.location=(0,-6,0); cam.rotation_euler=(math.pi/2,0,0)
    sun=bpy.data.objects.new('sun',bpy.data.lights.new('sun','SUN')); s.collection.objects.link(sun)
    sun.data.energy=6.0; sun.rotation_euler=(math.radians(50),math.radians(-35),math.radians(-20))
    return s
def node(nt,kind,**kw):
    n=nt.nodes.new(kind)
    for k,v in kw.items():
        if k in n.inputs: n.inputs[k].default_value=v
        else: setattr(n,k,v)
    return n
def domain(s,size=1.0):
    bpy.ops.mesh.primitive_cube_add(size=2*size); o=bpy.context.object
    m=bpy.data.materials.new('vol'); m.use_nodes=True; o.data.materials.append(m)
    nt=m.node_tree; nt.nodes.clear(); out=node(nt,'ShaderNodeOutputMaterial'); return o,nt,out
def render_px(s):
    p='/tmp/_particle_frame.exr'; s.render.filepath=p; bpy.ops.render.render(write_still=True)
    im=bpy.data.images.load(p); a=np.array(im.pixels[:],np.float32).reshape(im.size[1],im.size[0],4)[::-1]
    bpy.data.images.remove(im); return a          # premultiplied linear RGBA, top row first
def to_srgb(c): c=np.clip(c,0,1); return np.where(c<=0.0031308,12.92*c,1.055*np.power(c,1/2.4)-0.055)
def straight(pm,emissive=False,gain=1.0):
    """Premultiplied linear -> straight sRGB RGBA. Emissive: alpha also covers light with no absorption."""
    rgb=pm[...,:3]; a=pm[...,3]
    if emissive: a=np.maximum(a,np.clip(rgb.max(-1)*gain,0,1))
    col=rgb/np.maximum(a,1e-4)[...,None]
    return np.dstack([to_srgb(col),np.clip(a,0,1)])
def save(name,img):
    h,w,_=img.shape; im=bpy.data.images.new(name,w,h,alpha=True)
    im.pixels[:]=np.ascontiguousarray(img[::-1]).ravel(); im.filepath_raw=os.path.join(OUT,name); im.file_format='PNG'
    im.save(); bpy.data.images.remove(im); print('wrote',name,w,'x',h)
def sheet(frames,cols):
    rows=[np.hstack(frames[r*cols:(r+1)*cols]) for r in range(len(frames)//cols)]; return np.vstack(rows)

# ---------------------------------------------------------------- volumes: shared density graph
def puff_graph(nt,out,albedo,emit=False):
    """density = noisy sphere of radius R; inputs set per frame: R, W (noise time), dens, rise, temp."""
    tc=node(nt,'ShaderNodeTexCoord'); rise=node(nt,'ShaderNodeVectorMath',operation='SUBTRACT')
    nt.links.new(tc.outputs['Object'],rise.inputs[0]); rise.inputs[1].default_value=(0,0,0)
    n1=node(nt,'ShaderNodeTexNoise',noise_dimensions='4D'); n1.inputs['Scale'].default_value=1.6
    n1.inputs['Detail'].default_value=8; n1.inputs['Roughness'].default_value=0.62; n1.inputs['Distortion'].default_value=0.35
    nt.links.new(rise.outputs[0],n1.inputs['Vector'])
    ln=node(nt,'ShaderNodeVectorMath',operation='LENGTH'); nt.links.new(rise.outputs[0],ln.inputs[0])
    # d = |p| + (noise-0.5)*amp ; mask = smoothstep(R, R*0.55, d)
    sub=node(nt,'ShaderNodeMath',operation='SUBTRACT'); nt.links.new(n1.outputs['Fac'],sub.inputs[0]); sub.inputs[1].default_value=0.5
    amp=node(nt,'ShaderNodeMath',operation='MULTIPLY'); nt.links.new(sub.outputs[0],amp.inputs[0]); amp.inputs[1].default_value=0.9
    d=node(nt,'ShaderNodeMath',operation='ADD'); nt.links.new(ln.outputs['Value'],d.inputs[0]); nt.links.new(amp.outputs[0],d.inputs[1])
    mr=node(nt,'ShaderNodeMapRange',interpolation_type='SMOOTHSTEP'); nt.links.new(d.outputs[0],mr.inputs['Value'])
    mr.inputs['To Min'].default_value=0.0; mr.inputs['To Max'].default_value=1.0   # d>=R -> 0, d<=0.55R -> 1
    dens=node(nt,'ShaderNodeMath',operation='MULTIPLY'); nt.links.new(mr.outputs['Result'],dens.inputs[0])
    pv=node(nt,'ShaderNodeVolumePrincipled'); pv.inputs['Color'].default_value=(*albedo,1)
    nt.links.new(dens.outputs[0],pv.inputs['Density']); nt.links.new(pv.outputs[0],out.inputs['Volume'])
    if emit:   # temperature rises toward the hot core: temp = Tmin + (Tmax-Tmin)*mask^2
        sq=node(nt,'ShaderNodeMath',operation='POWER'); nt.links.new(mr.outputs['Result'],sq.inputs[0]); sq.inputs[1].default_value=2.0
        tmp=node(nt,'ShaderNodeMapRange'); nt.links.new(sq.outputs[0],tmp.inputs['Value'])
        nt.links.new(tmp.outputs['Result'],pv.inputs['Temperature'])
        bi=node(nt,'ShaderNodeMath',operation='MULTIPLY'); nt.links.new(mr.outputs['Result'],bi.inputs[0])
        nt.links.new(bi.outputs[0],pv.inputs['Blackbody Intensity'])
    else: tmp=bi=None
    return dict(rise=rise,n1=n1,amp=amp,mr=mr,dens=dens,pv=pv,tmp=tmp,bi=bi)
def set_frame(g,R,W,dens,rise=0.0,amp=0.9,temp=None,glow=0.0):
    g['rise'].inputs[1].default_value=(0,0,rise); g['n1'].inputs['W'].default_value=W
    g['amp'].inputs[1].default_value=amp; g['mr'].inputs['From Min'].default_value=R; g['mr'].inputs['From Max'].default_value=R*0.82
    g['dens'].inputs[1].default_value=dens
    if temp: g['tmp'].inputs['To Min'].default_value=temp[0]; g['tmp'].inputs['To Max'].default_value=temp[1]
    if g['bi']: g['bi'].inputs[1].default_value=glow

def smoke(res=256,n=64):
    s=reset(res); o,nt,out=domain(s); g=puff_graph(nt,out,(0.92,0.91,0.89)); fr=[]
    for i in range(n):
        t=i/(n-1); R=0.5+0.32*t**0.6
        set_frame(g,R,W=1.3*t,dens=30*(1-t)**1.8*(0.42/R)**2+0.03,rise=0.12*t,amp=1.1+0.4*t)
        fr.append(straight(render_px(s)))
    save('smoke_8x8.png',sheet(fr,8))
def dust(res=512):
    s=reset(res); o,nt,out=domain(s); g=puff_graph(nt,out,(0.55,0.47,0.36))
    set_frame(g,0.8,W=3.7,dens=2.2,amp=1.1); s.cycles.samples=256; img=straight(render_px(s)); save('dust.png',img)
def explosion(res=256,n=64):
    s=reset(res); o,nt,out=domain(s); g=puff_graph(nt,out,(0.12,0.11,0.1),emit=True); fr=[]
    for i in range(n):
        t=i/(n-1); R=0.22+0.7*t**0.45
        glow=30*max(0.0,1-t/0.5)**2           # fireball burns out over the first half, smoke stays
        set_frame(g,R,W=1.6*t,dens=(1.5+30*t)*(1-t)**1.5+0.02,rise=0.22*t,amp=0.7+0.4*t,temp=(900,2400-900*t),glow=glow)
        fr.append(straight(render_px(s),emissive=True,gain=1.5))
    save('explosion_8x8.png',sheet(fr,8))

def fire(res=256,n=64):
    """Looping flame: two noise fields scrolled upward, crossfaded over the loop so frame 63 -> 0 is seamless."""
    s=reset(res); s.world.node_tree.nodes['Background'].inputs[1].default_value=0.0
    o,nt,out=domain(s); tc=node(nt,'ShaderNodeTexCoord'); sep=node(nt,'ShaderNodeSeparateXYZ'); nt.links.new(tc.outputs['Object'],sep.inputs[0])
    def field(off):
        add=node(nt,'ShaderNodeVectorMath',operation='ADD'); nt.links.new(tc.outputs['Object'],add.inputs[0])
        nz=node(nt,'ShaderNodeTexNoise',noise_dimensions='4D'); nz.inputs['Scale'].default_value=2.6; nz.inputs['Detail'].default_value=5
        nz.inputs['Roughness'].default_value=0.55; nt.links.new(add.outputs[0],nz.inputs['Vector']); return add,nz
    a1,n1=field(0); a2,n2=field(1)
    mix=node(nt,'ShaderNodeMix',data_type='FLOAT'); nt.links.new(n1.outputs['Fac'],mix.inputs[2]); nt.links.new(n2.outputs['Fac'],mix.inputs[3])
    # flame body: radial distance in xy scaled by a width that narrows with height z in [-1,1]
    xy=node(nt,'ShaderNodeCombineXYZ'); nt.links.new(sep.outputs['X'],xy.inputs['X']); nt.links.new(sep.outputs['Y'],xy.inputs['Y'])
    r=node(nt,'ShaderNodeVectorMath',operation='LENGTH'); nt.links.new(xy.outputs[0],r.inputs[0])
    h=node(nt,'ShaderNodeMapRange'); nt.links.new(sep.outputs['Z'],h.inputs['Value']); h.inputs['From Min'].default_value=-0.95
    h.inputs['From Max'].default_value=0.95; h.inputs['To Min'].default_value=0.55; h.inputs['To Max'].default_value=0.02
    q=node(nt,'ShaderNodeMath',operation='DIVIDE'); nt.links.new(r.outputs['Value'],q.inputs[0]); nt.links.new(h.outputs['Result'],q.inputs[1])
    sub=node(nt,'ShaderNodeMath',operation='SUBTRACT'); nt.links.new(mix.outputs[0],sub.inputs[0]); sub.inputs[1].default_value=0.5
    amp=node(nt,'ShaderNodeMath',operation='MULTIPLY',); nt.links.new(sub.outputs[0],amp.inputs[0]); amp.inputs[1].default_value=2.4
    d=node(nt,'ShaderNodeMath',operation='ADD'); nt.links.new(q.outputs[0],d.inputs[0]); nt.links.new(amp.outputs[0],d.inputs[1])
    m=node(nt,'ShaderNodeMapRange',interpolation_type='SMOOTHSTEP'); nt.links.new(d.outputs[0],m.inputs['Value'])
    m.inputs['From Min'].default_value=1.0; m.inputs['From Max'].default_value=0.3; m.inputs['To Min'].default_value=1; m.inputs['To Max'].default_value=0
    m.inputs['To Min'].default_value=0; m.inputs['To Max'].default_value=1
    tp=node(nt,'ShaderNodeMapRange'); nt.links.new(m.outputs['Result'],tp.inputs['Value']); tp.inputs['To Min'].default_value=1000; tp.inputs['To Max'].default_value=2100
    pv=node(nt,'ShaderNodeVolumePrincipled'); pv.inputs['Density'].default_value=0.0
    base=node(nt,'ShaderNodeMapRange',interpolation_type='SMOOTHSTEP'); nt.links.new(sep.outputs['Z'],base.inputs['Value'])
    base.inputs['From Min'].default_value=-0.98; base.inputs['From Max'].default_value=-0.6   # rounded base, not cut by the domain
    mb=node(nt,'ShaderNodeMath',operation='MULTIPLY'); nt.links.new(m.outputs['Result'],mb.inputs[0]); nt.links.new(base.outputs['Result'],mb.inputs[1])
    bi=node(nt,'ShaderNodeMath',operation='MULTIPLY'); nt.links.new(mb.outputs[0],bi.inputs[0]); bi.inputs[1].default_value=9
    nt.links.new(bi.outputs[0],pv.inputs['Blackbody Intensity']); nt.links.new(tp.outputs['Result'],pv.inputs['Temperature'])
    nt.links.new(pv.outputs[0],out.inputs['Volume'])
    fr=[]; speed=1.6
    for i in range(n):
        t=i/n
        a1.inputs[1].default_value=(0,0,-speed*t); n1.inputs['W'].default_value=0.8*t
        a2.inputs[1].default_value=(0,0,-speed*(t-1)); n2.inputs['W'].default_value=0.8*(t-1)
        mix.inputs['Factor'].default_value=t
        fr.append(straight(render_px(s),emissive=True,gain=1.2))
    save('fire_8x8.png',sheet(fr,8))

# ---------------------------------------------------------------- 2D emissive sprites (numpy)
def blackbody(x):   # 0..1 heat -> linear colour, white core through yellow to deep orange
    x=np.clip(x,0,1)[...,None]
    return np.clip(np.concatenate([np.ones_like(x)*1.0, 0.25+0.75*x**0.8, 0.02+0.98*x**2.5],-1),0,1)
def emissive_png(name,heat,alpha):
    rgb=blackbody(heat); save(name,np.dstack([to_srgb(rgb),np.clip(alpha,0,1)]))
def fbm(x,y,rng,octaves=5):
    """Cheap value-noise fBm on a grid (bilinear lattice noise), roughly -1..1."""
    out=np.zeros_like(x); amp=1.0; f=3.0; tot=0
    for _ in range(octaves):
        g=rng.uniform(-1,1,(int(f)+3,int(f)+3)); u=(x+1)/2*f; v=(y+1)/2*f; i=np.minimum(u.astype(int),int(f)); j=np.minimum(v.astype(int),int(f)); fu=u-i; fv=v-j
        fu=fu*fu*(3-2*fu); fv=fv*fv*(3-2*fv)
        n=(g[j,i]*(1-fu)+g[j,i+1]*fu)*(1-fv)+(g[j+1,i]*(1-fu)+g[j+1,i+1]*fu)*fv
        out+=n*amp; tot+=amp; amp*=0.55; f*=2.1
    return out/tot
def muzzle_flash(res=512):
    """Front view of a rifle muzzle blast: hot white core, 3-5 broad turbulent flame lobes, orange fringe."""
    rng=np.random.default_rng(5); y,x=np.mgrid[-1:1:res*1j,-1:1:res*1j]; r=np.hypot(x,y)*0.7; th=np.arctan2(y,x); tiles=[]
    for v in range(4):
        k=int(rng.integers(3,6)); ph=rng.uniform(0,2*np.pi); lobes=np.zeros_like(r)
        n=fbm(x,y,rng); n2=fbm(x*1.7,y*1.7,rng)
        for j in range(k):
            a=ph+2*np.pi*j/k+rng.uniform(-0.3,0.3); L=rng.uniform(0.55,0.9); wdt=rng.uniform(0.28,0.5)
            da=np.angle(np.exp(1j*(th-a+0.25*n2)))
            lobes=np.maximum(lobes,np.exp(-(da/wdt)**2)*np.clip(1-r*np.clip(1+0.45*n,0.6,1.5)/L,0,1)**0.9)
        core=np.exp(-(r*(1+0.3*n)/0.2)**2.0); halo=np.exp(-(r/0.5)**2)*0.25
        cov=np.clip(core+lobes*(0.85+0.4*n)+halo,0,1)
        heat=np.clip(core*1.1+lobes*(0.45+0.3*n2)+halo*0.3,0,1)
        win=np.clip(1-r/0.66,0,1)**1.2; cov=np.clip((cov*win-0.04)*1.25,0,1)**1.3
        tiles.append(np.dstack([to_srgb(blackbody(heat*win)),cov]))
    save('muzzle_flash_2x2.png',sheet(tiles,2))
def spark(w=64,h=512):
    y,x=np.mgrid[-1:1:h*1j,-1:1:w*1j]; along=np.clip(1-np.abs(y),0,1)**0.6*np.where(y<0,1,np.clip(1-y*1.4,0,1))
    core=np.exp(-(x/0.12)**2)*along; halo=np.exp(-(x/0.45)**2)*along*0.35
    emissive_png('spark.png',np.clip(core*1.1,0,1)*0.9+0.1*halo,np.clip(core+halo,0,1))
def raindrop(w=32,h=512):
    y,x=np.mgrid[-1:1:h*1j,-1:1:w*1j]; along=np.clip(1-np.abs(y)**2,0,1)
    a=np.exp(-(x/0.28)**2)*along*0.55; col=np.ones((h,w,3))*np.array([0.82,0.86,0.9])
    save('raindrop.png',np.dstack([to_srgb(col),a]))

# ---------------------------------------------------------------- debris chips (Cycles, Poly Haven scans)
def debris(tile=256):
    s=reset(tile); s.cycles.samples=64; s.camera.data.ortho_scale=2.2
    rng=np.random.default_rng(11); mats=[]
    for name,scale in (('concrete_wall_007',1.0),('rough_wood',1.0)):
        m=bpy.data.materials.new(name); m.use_nodes=True; nt=m.node_tree; b=nt.nodes['Principled BSDF']
        tc=node(nt,'ShaderNodeTexCoord'); mp=node(nt,'ShaderNodeMapping'); nt.links.new(tc.outputs['Object'],mp.inputs['Vector'])
        mp.inputs['Scale'].default_value=(0.35,0.35,0.35)
        for ch,inp,cs in (('albedo.jpg','Base Color','sRGB'),('roughness.jpg','Roughness','Non-Color')):
            t=node(nt,'ShaderNodeTexImage'); t.image=bpy.data.images.load(os.path.join(PBR,name,ch)); t.image.colorspace_settings.name=cs
            t.projection='BOX'; t.projection_blend=0.3; nt.links.new(mp.outputs[0],t.inputs['Vector']); nt.links.new(t.outputs['Color'],b.inputs[inp])
        mats.append(m)
    tiles=[]
    for i in range(16):
        for o in list(s.objects):
            if o.type=='MESH': bpy.data.objects.remove(o)
        wood=i%4==3   # 12 concrete chips, 4 wood splinters
        if wood:
            bpy.ops.mesh.primitive_cube_add(size=1); o=bpy.context.object; o.scale=(rng.uniform(1.4,1.9),rng.uniform(0.18,0.3),rng.uniform(0.12,0.22))
        else:
            bpy.ops.mesh.primitive_ico_sphere_add(subdivisions=2,radius=0.7); o=bpy.context.object
            o.scale=(rng.uniform(0.7,1.2),rng.uniform(0.5,0.9),rng.uniform(0.45,0.8))
        bpy.ops.object.transform_apply(scale=True)
        for v in o.data.vertices:   # break the shape into a jagged chip
            v.co*=1+rng.uniform(-0.22,0.22) if not wood else 1+rng.uniform(-0.05,0.05)
            if not wood and v.co.z>0.25: v.co.z=0.25+0.3*(v.co.z-0.25)   # a flat fracture face
        o.data.materials.append(mats[1 if wood else 0])
        o.rotation_euler=tuple(rng.uniform(0,2*np.pi,3)); o.location=(0,0,0)
        tiles.append(straight(render_px(s)))
    save('debris_4x4.png',sheet(tiles,4))

JOBS={'smoke':smoke,'fire':fire,'explosion':explosion,'dust':dust,'muzzle':muzzle_flash,'spark':spark,'rain':raindrop,'debris':debris}
for k,f in JOBS.items():
    if not ONLY or k in ONLY: f()
