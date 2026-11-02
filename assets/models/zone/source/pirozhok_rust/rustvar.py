#!/usr/bin/env python3
"""Rusted variants of the Pirozhok van scan: repaint the white body in a faded colour and eat it with real rust.

    python3 -I rustvar.py car_pirozhok.glb rust_albedo.jpg out_dir

Only the base colour image inside the .glb changes (mesh, UVs, normal and roughness maps are the scan's).
Rust: Poly Haven rust_coarse_01 (CC0), laid in UV space and revealed by fractal noise plus the scan's own grime.
"""
import json,struct,sys,io,os
import numpy as np
from PIL import Image,ImageFilter
src,rust_p,out=sys.argv[1:4]
def read(p):
    b=open(p,'rb').read(); n=struct.unpack('<I',b[12:16])[0]; j=json.loads(b[20:20+n]); return j,b[20+n+8:]
def put(p,j,binb,repl,dst):
    views=j['bufferViews']; new={}
    for I in j['images']:
        if I.get('name') in repl: new[I['bufferView']]=repl[I['name']]
    o=bytearray()
    for i,v in enumerate(views):
        d=new.get(i,binb[v.get('byteOffset',0):v.get('byteOffset',0)+v['byteLength']])
        while len(o)%4: o+=b'\0'
        v['byteOffset']=len(o); v['byteLength']=len(d); o+=d
    while len(o)%4: o+=b'\0'
    j['buffers'][0]['byteLength']=len(o)
    js=json.dumps(j,separators=(',',':')).encode(); js+=b' '*((4-len(js)%4)%4)
    data=b'glTF'+struct.pack('<II',2,12+8+len(js)+8+len(o))+struct.pack('<I',len(js))+b'JSON'+js+struct.pack('<I',len(o))+b'BIN\0'+bytes(o)
    open(dst,'wb').write(data)
j,binb=read(src)
img={I['name']:I for I in j['images']}
bv=j['bufferViews'][img['Material_23_baseColor']['bufferView']]
base=np.asarray(Image.open(io.BytesIO(binb[bv.get('byteOffset',0):bv.get('byteOffset',0)+bv['byteLength']])).convert('RGB')).astype(float)/255
N=base.shape[0]
rust=np.asarray(Image.open(rust_p).convert('RGB').resize((N//2,N//2))).astype(float)/255; rust=np.tile(rust,(2,2,1))
lum=base.mean(2); sat=(base.max(2)-base.min(2))/(base.max(2)+1e-4)
paint=np.clip((lum-0.28)/0.15,0,1)*np.clip((0.25-sat)/0.1,0,1)
paint=np.asarray(Image.fromarray((paint*255).astype(np.uint8)).filter(ImageFilter.GaussianBlur(1.2))).astype(float)/255
def fbm(seed,oct=5):
    r=np.random.default_rng(seed); acc=np.zeros((N,N)); amp=1; tot=0
    for o in range(oct):
        s=max(4,N//(2**(7-o)) if o<7 else N); s=4*2**o
        n=np.asarray(Image.fromarray((r.random((s,s))*255).astype(np.uint8)).resize((N,N),Image.BICUBIC)).astype(float)/255
        acc+=n*amp; tot+=amp; amp*=0.55
    return acc/tot
grime=1-np.asarray(Image.fromarray((lum*255).astype(np.uint8)).filter(ImageFilter.GaussianBlur(6))).astype(float)/255
VARS={'car_pirozhok_rust_green':((0.33,0.42,0.30),0.68,11),'car_pirozhok_rust_blue':((0.30,0.40,0.50),0.58,23),
      'car_pirozhok_wreck':((0.16,0.14,0.12),0.9,37)}
for name,(tint,amount,seed) in VARS.items():
    n=fbm(seed)*0.75+grime*0.45
    thr=np.quantile(n,1-amount); m=np.clip((n-thr)/0.04,0,1)**0.7
    m=np.maximum(m,np.clip((fbm(seed+1,6)-0.62)/0.05,0,1)*0.8)   # small rust spots everywhere
    col=np.array(tint)*(0.55+0.65*lum[...,None])            # faded paint keeps the scan's dents and dirt
    out_=base*(1-paint[...,None])+col*paint[...,None]
    if name.endswith('wreck'): out_=out_*0.55+np.array(tint)*0.45   # burnt: everything charred
    rm=(m*np.maximum(paint,0.5 if name.endswith('wreck') else 0.25))[...,None]
    out_=out_*(1-rm)+rust*(0.8+0.3*lum[...,None])*rm
    buf=io.BytesIO(); Image.fromarray((np.clip(out_,0,1)*255).astype(np.uint8)).save(buf,'JPEG',quality=90)
    j2=json.loads(json.dumps(j)); put(src,j2,binb,{'Material_23_baseColor':buf.getvalue()},os.path.join(out,name+'.glb'))
    Image.fromarray((np.clip(out_,0,1)*255).astype(np.uint8)).resize((512,512)).save(os.path.join('/tmp',name+'_alb.png'))
    print('wrote',name)
