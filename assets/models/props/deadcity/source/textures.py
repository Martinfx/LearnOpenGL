#!/usr/bin/env python3
"""Textures for the Dead City props, built from Poly Haven CC0 scans already in assets/textures/pbr (downsized to 1K)
plus procedural paint, plastic and sign faces (no real-world branding).

    python3 -I textures.py <tex_out_dir>

Writes <name>_{albedo,roughness,normal}.jpg as read by ../../clutter/source/clutter.py mat(); also the decals
(crack_*, pothole_*) into assets/textures/decals/.
"""
import os,sys
import numpy as np
from PIL import Image,ImageDraw,ImageFont,ImageFilter
T=sys.argv[1]; os.makedirs(T,exist_ok=True)
ROOT=os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)),'..','..','..','..'))
PBR=os.path.join(ROOT,'textures','pbr'); DEC=os.path.join(ROOT,'textures','decals')
FONT=os.path.join(ROOT,'fonts','DejaVuSans-Bold.ttf')
N=1024; rng=np.random.default_rng(5)
def load(set_,m,n=N): return np.asarray(Image.open(os.path.join(PBR,set_,m+'.jpg')).convert('RGB').resize((n,n),Image.LANCZOS)).astype(float)/255
def save(name,alb,rough=None,nrm=None):
    Image.fromarray((np.clip(alb,0,1)*255).astype(np.uint8)).save(os.path.join(T,name+'_albedo.jpg'),quality=90)
    if rough is not None:
        r=rough if rough.ndim==3 else np.repeat(rough[...,None],3,2)
        Image.fromarray((np.clip(r,0,1)*255).astype(np.uint8)).save(os.path.join(T,name+'_roughness.jpg'),quality=90)
    if nrm is not None: Image.fromarray((np.clip(nrm,0,1)*255).astype(np.uint8)).save(os.path.join(T,name+'_normal.jpg'),quality=90)
def fbm(seed,n=N,oct=6,base=4):
    r=np.random.default_rng(seed); acc=np.zeros((n,n)); a=1; t=0
    for o in range(oct):
        s=base*2**o; x=np.asarray(Image.fromarray((r.random((s,s))*255).astype(np.uint8)).resize((n,n),Image.BICUBIC)).astype(float)/255
        acc+=x*a; t+=a; a*=0.55
    return acc/t
RUST=load('rust_coarse_01','albedo'); RUSTR=load('rust_coarse_01','roughness'); RUSTN=load('rust_coarse_01','normal')
PAINTN=load('rusty_painted_metal','normal'); PAINTA=load('rusty_painted_metal','albedo')
def painted(name,color,rust_amt,seed,gloss=0.55,base_alb=None):
    """faded paint over metal, eaten by real rust where fractal noise + the scan's own dark streaks say so"""
    lum=(base_alb if base_alb is not None else PAINTA).mean(2)
    n=fbm(seed)*0.8+(1-lum)*0.35; thr=np.quantile(n,1-rust_amt); m=np.clip((n-thr)/0.05,0,1)
    m=np.maximum(m,np.clip((fbm(seed+7,base=16)-0.66)/0.04,0,1)*0.7)[...,None]
    paint=np.array(color)*(0.75+0.35*fbm(seed+3,base=8))[...,None]*(0.85+0.2*lum[...,None])
    alb=paint*(1-m)+RUST*m; rough=gloss*(1-m[...,0])+RUSTR.mean(2)*m[...,0]
    nrm=PAINTN*(1-m)+RUSTN*m; save(name,alb,rough,nrm)
painted('drum_blue',(0.12,0.22,0.42),0.45,1); painted('drum_red',(0.45,0.10,0.07),0.5,2); painted('drum_yellow',(0.62,0.48,0.08),0.4,3)
painted('drum_green',(0.16,0.25,0.15),0.55,4); painted('drum_rust',(0.3,0.2,0.12),0.92,5)
painted('dumpster_green',(0.10,0.22,0.14),0.5,6,0.6); painted('dumpster_blue',(0.10,0.18,0.32),0.45,7,0.6)
painted('container_red',(0.42,0.12,0.08),0.45,8,0.7); painted('container_blue',(0.12,0.24,0.38),0.4,9,0.7); painted('container_rust',(0.33,0.18,0.1),0.85,10,0.7)
painted('signal_yellow',(0.62,0.50,0.08),0.4,11,0.5); painted('pole_galv',(0.42,0.42,0.40),0.3,12,0.5); painted('pole_dark',(0.12,0.13,0.12),0.45,13,0.6)
# plastics
def plastic(name,color,dirt,seed,rough=0.6):
    d=fbm(seed); alb=np.array(color)*(0.9+0.15*fbm(seed+1,base=16))[...,None]
    dm=np.clip((d-0.45)/0.3,0,1)[...,None]*dirt; alb=alb*(1-dm)+np.array((0.22,0.19,0.15))*dm
    save(name,alb,np.full((N,N),rough)*(1-dm[...,0])+0.95*dm[...,0],None)
plastic('cone_orange',(0.85,0.28,0.05),0.6,20,0.55); plastic('bin_green',(0.10,0.17,0.12),0.5,21,0.6); plastic('bin_grey',(0.20,0.21,0.21),0.5,22,0.6)
plastic('reflective',(0.86,0.86,0.82),0.55,23,0.35); plastic('lens_dark',(0.06,0.06,0.05),0.2,24,0.15)
plastic('lens_red',(0.30,0.03,0.02),0.25,25,0.15); plastic('lens_amber',(0.38,0.20,0.02),0.25,26,0.15); plastic('lens_green',(0.04,0.22,0.12),0.25,27,0.15)
plastic('rubber',(0.05,0.05,0.05),0.4,28,0.9)
# rusty corrugated container sides: tint the corrugated scan
for name,col in (('corr_red',(0.42,0.12,0.08)),('corr_blue',(0.12,0.24,0.38)),('corr_rust',None)):
    a=load('rusty_corrugated_iron','albedo'); r=load('rusty_corrugated_iron','roughness'); n=load('rusty_corrugated_iron','normal')
    if col:
        m=np.clip((fbm(hash(name)%100)-0.5)/0.1,0,1)[...,None]*0.6
        tint=np.array(col)*(0.7+0.6*a.mean(2,keepdims=True)); a=tint*(1-m)+a*m
    save(name,a,r,n)
# sign faces: 4x4 atlas of 512 px cells (front faces), retroreflective sheet faded, dirt and rust spots
S=512; atlas=Image.new('RGB',(S*4,S*4),(200,200,195)); dr=ImageDraw.Draw(atlas)
def font(sz): return ImageFont.truetype(FONT,sz)
def cell(i): return ((i%4)*S,(i//4)*S)
def text(xy,s,sz,fill):
    f=font(sz); w=dr.textlength(s,font=f); dr.text((xy[0]-w/2,xy[1]-sz*0.55),s,font=f,fill=fill)
# 0 STOP (octagon)
x,y=cell(0); c=(x+S/2,y+S/2); r=S*0.48; import math
oct_=[(c[0]+r*math.cos(math.radians(22.5+45*k)),c[1]+r*math.sin(math.radians(22.5+45*k))) for k in range(8)]
dr.polygon(oct_,fill=(235,235,230)); oct2=[(c[0]+(r-14)*math.cos(math.radians(22.5+45*k)),c[1]+(r-14)*math.sin(math.radians(22.5+45*k))) for k in range(8)]
dr.polygon(oct2,fill=(170,24,24)); text(c,'STOP',150,(240,240,235))
# 1 YIELD (triangle, point down)
x,y=cell(1); dr.rectangle((x,y,x+S,y+S),fill=(235,235,230)); dr.polygon([(x+8,y+40),(x+S-8,y+40),(x+S/2,y+S-20)],fill=(175,30,30))
dr.polygon([(x+80,y+82),(x+S-80,y+82),(x+S/2,y+S-130)],fill=(235,235,230)); text((x+S/2,y+170),'YIELD',62,(175,30,30))
# 2 SPEED LIMIT 35 (portrait: cell drawn 400x512 centered)
x,y=cell(2); dr.rectangle((x+56,y,x+S-56,y+S),fill=(30,30,30)); dr.rectangle((x+64,y+8,x+S-64,y+S-8),fill=(232,232,226))
text((x+S/2,y+100),'SPEED',70,(25,25,25)); text((x+S/2,y+180),'LIMIT',70,(25,25,25)); text((x+S/2,y+340),'35',190,(25,25,25))
# 3 warning diamond: ROAD WORK (orange)
x,y=cell(3); c=(x+S/2,y+S/2); dr.polygon([(c[0],y+4),(x+S-4,c[1]),(c[0],y+S-4),(x+4,c[1])],fill=(30,30,30))
dr.polygon([(c[0],y+16),(x+S-16,c[1]),(c[0],y+S-16),(x+16,c[1])],fill=(222,120,20)); text((c[0],c[1]-45),'ROAD',80,(20,20,20)); text((c[0],c[1]+50),'WORK',80,(20,20,20))
# 4 warning diamond: curve arrow (yellow)
x,y=cell(4); c=(x+S/2,y+S/2); dr.polygon([(c[0],y+4),(x+S-4,c[1]),(c[0],y+S-4),(x+4,c[1])],fill=(30,30,30))
dr.polygon([(c[0],y+16),(x+S-16,c[1]),(c[0],y+S-16),(x+16,c[1])],fill=(225,190,25))
dr.arc((c[0]-150,c[1]-40,c[0]+90,c[1]+220),200,290,fill=(20,20,20),width=34); dr.line((c[0]-60,c[1]+120,c[0]-60,c[1]+200),fill=(20,20,20),width=34)
dr.polygon([(c[0]+40,c[1]-120),(c[0]+110,c[1]-30),(c[0]+10,c[1]-10)],fill=(20,20,20))
# 5 ONE WAY (landscape 3:1, lower third used)
x,y=cell(5); dr.rectangle((x,y+171,x+S,y+341),fill=(20,20,20)); dr.rectangle((x+10,y+181,x+S-10,y+331),fill=(20,20,20))
dr.polygon([(x+40,y+226),(x+360,y+226),(x+360,y+196),(x+470,y+256),(x+360,y+316),(x+360,y+286),(x+40,y+286)],fill=(235,235,230)); text((x+190,y+256),'ONE WAY',44,(20,20,20))
# 6,7 street name blades (green, 4:1 band in the middle)
for i,nm in ((6,'MAIN ST'),(7,'ELM AVE')):
    x,y=cell(i); dr.rectangle((x,y+192,x+S,y+320),fill=(20,95,55)); dr.rectangle((x+6,y+198,x+S-6,y+314),outline=(225,230,225),width=5); text((x+S/2,y+256),nm,72,(235,240,235))
# 8 NO PARKING (portrait)
x,y=cell(8); dr.rectangle((x+64,y+8,x+S-64,y+S-8),fill=(232,232,226)); text((x+S/2,y+90),'NO',80,(170,25,25)); text((x+S/2,y+185),'PARKING',60,(170,25,25))
text((x+S/2,y+300),'ANY TIME',44,(170,25,25)); dr.polygon([(x+180,y+400),(x+330,y+400),(x+330,y+380),(x+380,y+420),(x+330,y+460),(x+330,y+440),(x+180,y+440)],fill=(170,25,25))
# 9 DO NOT ENTER (red disc)
x,y=cell(9); c=(x+S/2,y+S/2); dr.rectangle((x,y,x+S,y+S),fill=(232,232,226)); dr.ellipse((x+20,y+20,x+S-20,y+S-20),fill=(170,25,25)); dr.rectangle((x+80,c[1]-35,x+S-80,c[1]+35),fill=(235,235,230))
text((c[0],y+125),'DO NOT',58,(235,235,230)); text((c[0],y+390),'ENTER',58,(235,235,230))
# 10 pedestrian crossing (yellow-green diamond)
x,y=cell(10); c=(x+S/2,y+S/2); dr.polygon([(c[0],y+4),(x+S-4,c[1]),(c[0],y+S-4),(x+4,c[1])],fill=(20,20,20))
dr.polygon([(c[0],y+16),(x+S-16,c[1]),(c[0],y+S-16),(x+16,c[1])],fill=(190,215,30)); dr.ellipse((c[0]-30,c[1]-150,c[0]+30,c[1]-90),fill=(20,20,20))
dr.line((c[0],c[1]-85,c[0]-10,c[1]+20),fill=(20,20,20),width=40); dr.line((c[0]-10,c[1]+20,c[0]-70,c[1]+140),fill=(20,20,20),width=30); dr.line((c[0]-10,c[1]+20,c[0]+50,c[1]+140),fill=(20,20,20),width=30)
dr.line((c[0]-5,c[1]-60,c[0]-80,c[1]+10),fill=(20,20,20),width=24); dr.line((c[0]-5,c[1]-60,c[0]+70,c[1]-5),fill=(20,20,20),width=24)
# 11 back of signs: galvanized aluminium
x,y=cell(11); dr.rectangle((x,y,x+S,y+S),fill=(150,152,150))
a=np.asarray(atlas).astype(float)/255
fade=0.75+0.25*fbm(40,n=S*4,base=8)[...,None]; dirt=np.clip((fbm(41,n=S*4,base=6)-0.55)/0.25,0,1)[...,None]*0.55
spots=np.clip((fbm(42,n=S*4,base=32)-0.7)/0.05,0,1)[...,None]
rust4=np.asarray(Image.fromarray((RUST*255).astype(np.uint8)).resize((S*4,S*4))).astype(float)/255
a=(a*fade+0.06)*(1-dirt)+np.array((0.25,0.22,0.17))*dirt; a=a*(1-spots*0.8)+rust4*spots*0.8
Image.fromarray((np.clip(a,0,1)*255).astype(np.uint8)).resize((2048,2048)).save(os.path.join(T,'signs_albedo.jpg'),quality=90)
Image.fromarray((np.clip(0.35+0.5*dirt[...,0]+0.4*spots[...,0],0,1)*255).astype(np.uint8)).save(os.path.join(T,'signs_roughness.jpg'),quality=90)
# decals (RGBA): cracks and potholes for asphalt and pavement, real asphalt colour from road_damaged
os.makedirs(DEC,exist_ok=True)
def crack_mask(seed,n=1024):
    """asphalt cracks: long nearly straight main crack, short side branches, a patch of alligator cracking"""
    r=np.random.default_rng(seed); im=Image.new('L',(n,n),0); d=ImageDraw.Draw(im)
    def walk(x,y,ang,steps,w,depth):
        for i in range(int(steps)):
            ang+=r.normal(0,0.10); nx=x+math.cos(ang)*8; ny=y+math.sin(ang)*8
            d.line((x,y,nx,ny),fill=255,width=max(1,int(round(w)))); x,y=nx,ny; w=max(1.0,w*0.993)
            if depth<2 and r.random()<0.03: walk(x,y,ang+r.choice([-1,1])*r.uniform(0.5,1.1),steps*0.3,w*0.6,depth+1)
            if not (24<x<n-24 and 24<y<n-24): break
    a0=r.uniform(0,3.14); walk(n/2,n/2,a0,80,r.uniform(5,8),0); walk(n/2,n/2,a0+3.14,80,r.uniform(4,7),0)
    # alligator patch: jittered Voronoi edges inside a soft blob
    cx,cy=n/2+r.normal(0,120),n/2+r.normal(0,120); pts=np.array([[cx,cy]])+r.normal(0,110,(28,2))
    yy,xx=np.mgrid[0:n,0:n]; dd=np.sort(np.stack([np.hypot(xx-px,yy-py) for px,py in pts]),0)
    edge=np.clip(1-(dd[1]-dd[0])/3.0,0,1)*(dd[0]>6)*np.clip(1-np.hypot(xx-cx,yy-cy)/220,0,1)**0.5
    eb=np.asarray(Image.fromarray((edge*255).astype(np.uint8)).filter(ImageFilter.GaussianBlur(5))).astype(float)/255; edge=edge*(eb<0.35)
    m=np.maximum(np.asarray(im).astype(float)/255,edge*(fbm(seed+50,n,base=16)>0.35))
    return np.asarray(Image.fromarray((m*255).astype(np.uint8)).filter(ImageFilter.GaussianBlur(1.0))).astype(float)/255
ASPH=load('road_damaged','albedo')
for i in range(3):
    m=crack_mask(100+i); halo=np.asarray(Image.fromarray((m*255).astype(np.uint8)).filter(ImageFilter.GaussianBlur(6))).astype(float)/255
    col=np.zeros((N,N,3))+np.array((0.04,0.035,0.03)); col=col*(m[...,None])+ASPH*0.55*(1-m[...,None])
    alpha=np.clip(m*1.3+halo*0.45,0,1)
    Image.fromarray(np.dstack([(np.clip(col,0,1)*255).astype(np.uint8),(alpha*255).astype(np.uint8)])).save(os.path.join(DEC,f'crack_{chr(97+i)}.png'))
GRAV=load('gravel_floor','albedo') if os.path.exists(os.path.join(PBR,'gravel_floor')) else load('rubble','albedo')
for i in range(2):   # pothole: irregular noisy rim, gravel and dirt bottom, broken asphalt chunks at the edge
    yy,xx=np.mgrid[0:N,0:N]/N-0.5; ang=np.arctan2(yy,xx); rad=np.hypot(xx,yy)
    warp=(fbm(210+i,base=3)-0.5)*0.22+(fbm(220+i,base=12)-0.5)*0.06
    sx,sy=(1.0,1.35) if i==0 else (1.3,0.9); rad=np.hypot(xx*sx,yy*sy)
    edge=0.27+warp; hole=np.clip((edge-rad)/0.012,0,1); rim=np.clip(1-np.abs(rad-edge)/0.03,0,1)
    depth=np.clip((edge-rad)/0.12,0,1)
    col=(GRAV*0.35+ASPH*0.25)*(0.9-0.45*depth[...,None])*hole[...,None]+ASPH*0.8*(1-hole[...,None]); col=col*(1-0.45*rim[...,None])
    alpha=np.clip(hole+rim*0.9+np.clip((edge+0.06-rad)/0.06,0,1)*0.35,0,1)
    Image.fromarray(np.dstack([(np.clip(col,0,1)*255).astype(np.uint8),(alpha*255).astype(np.uint8)])).save(os.path.join(DEC,f'pothole_{chr(97+i)}.png'))
    h=Image.fromarray((np.clip(1-depth*hole,0,1)*255).astype(np.uint8)).filter(ImageFilter.GaussianBlur(2)); h=np.asarray(h).astype(float)/255
    gy,gx=np.gradient(h*40); nz=np.dstack([-gx,gy,np.ones((N,N))]); nz/=np.linalg.norm(nz,axis=2,keepdims=True)
    Image.fromarray(((nz*0.5+0.5)*255).astype(np.uint8)).save(os.path.join(DEC,f'pothole_{chr(97+i)}_normal.png'))
print('textures done')
