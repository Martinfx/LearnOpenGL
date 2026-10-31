import numpy as np
from PIL import Image,ImageFilter
def load(f): return np.array(Image.open(f).convert('RGBA')).astype(np.float32)/255
def best_windows(img,S,n,mind):
    a=img[...,3]; H,W=a.shape; ii=a.cumsum(0).cumsum(1)
    def s(y,x): return ii[y+S-1,x+S-1]-(ii[y-1,x+S-1] if y else 0)-(ii[y+S-1,x-1] if x else 0)+((ii[y-1,x-1]) if x and y else 0)
    cand=[(s(y,x),y,x) for y in range(0,H-S,32) for x in range(0,W-S,32)]; cand.sort(reverse=True); out=[]
    for v,y,x in cand:
        if all(abs(y-y2)>mind or abs(x-x2)>mind for _,y2,x2 in out): out.append((v,y,x))
        if len(out)==n: break
    return [(y,x) for _,y,x in out]
def soft(c,edge=0.18):
    h,w=c.shape[:2]; yy,xx=np.mgrid[0:h,0:w]; r=np.sqrt(((yy-h/2)/(h/2))**2+((xx-w/2)/(w/2))**2)
    rng=np.random.default_rng(h); nz=rng.random((h//32+1,w//32+1)).astype(np.float32)
    nz=np.array(Image.fromarray((nz*255).astype(np.uint8)).resize((w,h),Image.BICUBIC)).astype(np.float32)/255
    m=np.clip((1.0-r*0.95+(nz-0.5)*0.45)/edge,0,1); c=c.copy(); c[...,3]*=m; return c
def pad(c):
    # bleed colour into transparent area to avoid halos
    rgb=c[...,:3].copy(); a=(c[...,3]>0.05).astype(np.float32)
    for k in (2,4,8,16,32,64):
        im=Image.fromarray((np.dstack([rgb*a[...,None],a])*255).astype(np.uint8),'RGBA').filter(ImageFilter.BoxBlur(k))
        b=np.array(im).astype(np.float32)/255; w=b[...,3:4]; fill=np.where(w>1e-3,b[...,:3]/np.maximum(w,1e-3),rgb)
        rgb=np.where(a[...,None]>0,rgb,fill); a=np.maximum(a,(w[...,0]>1e-3).astype(np.float32))
    return np.dstack([rgb,c[...,3]])
def cell(c,size=512): return np.array(Image.fromarray((c*255).astype(np.uint8),'RGBA').resize((size,size),Image.LANCZOS)).astype(np.float32)/255
def build(cells,out,soft_edges):
    A=np.zeros((1024,1024,4),np.float32)
    for i,c in enumerate(cells):
        c=soft(c) if soft_edges[i] else c
        A[(i//2)*512:(i//2+1)*512,(i%2)*512:(i%2+1)*512]=cell(c)
    A=pad(A); Image.fromarray((np.clip(A,0,1)*255).astype(np.uint8),'RGBA').save(out)
V='/tmp/veg/'
f=load(V+'crown_ts_front.png'); s=load(V+'crown_ts_side.png')
cf=[f[y:y+760,x:x+760] for y,x in best_windows(f,760,2,500)]
cs=[s[y:y+760,x:x+760] for y,x in best_windows(s,760,2,500)]
build(cf+cs,V+'atlas/leaves_birch.png',[1,1,1,1])
t=load(V+'crown_it_top.png')
ct=[t[y:y+700,x:x+700] for y,x in best_windows(t,700,4,450)]
build(ct,V+'atlas/leaves_broadleaf.png',[1,1,1,1])
# conifer: whole sapling renders, no soft edge (they're complete)
cells=[load(V+'card_pine_top.png'),load(V+'card_fir_top.png'),load(V+'card_pine_test.png'),load(V+'card_fir_test.png')]
build(cells,V+'atlas/needles_conifer.png',[0,0,0,0])
for n in ['leaves_birch','leaves_broadleaf','needles_conifer']:
    im=Image.open(V+'atlas/'+n+'.png'); bg=Image.new('RGBA',im.size,(255,0,255,255)); bg.alpha_composite(im); bg.convert('RGB').resize((512,512)).save(V+'atlas/chk_'+n+'.png')
