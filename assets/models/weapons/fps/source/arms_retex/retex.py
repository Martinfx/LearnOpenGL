import sys,io,numpy as np
from PIL import Image
from scipy import ndimage
sys.path.insert(0,'/tmp/arms'); from glbtex import read,image_bytes
FPS='/home/user/LearnOpenGL/assets/models/weapons/fps/'
def img(glb,name,mode='RGB'):
    j,b=read(FPS+glb); return np.asarray(Image.open(io.BytesIO(image_bytes(j,b,name))).convert(mode)).astype(np.float64)/255
def lin(c): return np.where(c<=0.04045,c/12.92,((c+0.055)/1.055)**2.4)
def srgb(c): c=np.clip(c,0,1); return np.where(c<=0.0031308,12.92*c,1.055*c**(1/2.4)-0.055)
M=np.array([[0.4124,0.3576,0.1805],[0.2126,0.7152,0.0722],[0.0193,0.1192,0.9505]]); W=np.array([0.9505,1.0,1.089])
def f(t): return np.where(t>0.008856,np.cbrt(t),7.787*t+16/116)
def finv(t): return np.where(t>0.2069,t**3,(t-16/116)/7.787)
def to_lab(rgb):
    xyz=lin(rgb)@M.T/W; fx,fy,fz=f(xyz[...,0]),f(xyz[...,1]),f(xyz[...,2])
    return np.stack([116*fy-16,500*(fx-fy),200*(fy-fz)],-1)
def from_lab(lab):
    fy=(lab[...,0]+16)/116; fx=fy+lab[...,1]/500; fz=fy-lab[...,2]/200
    xyz=np.stack([finv(fx),finv(fy),finv(fz)],-1)*W; return srgb(xyz@np.linalg.inv(M).T)
def classes(tag):
    m=np.asarray(Image.open(f'/tmp/arms/mask_{tag}.png')); cls=np.where(m>200,2,np.where(m>64,1,0))  # 2 glove, 1 sleeve
    idx=ndimage.distance_transform_edt(cls==0,return_distances=False,return_indices=True)
    return cls[idx[0],idx[1]],cls
def match(t,s,tc,sc,k,lscale=1.0):
    a=t[tc==k]; b=s[sc==k]; mu_t,sd_t=a.mean(0),a.std(0)+1e-6; mu_s,sd_s=b.mean(0),b.std(0)
    sd=sd_s.copy(); sd[0]*=lscale
    return (t-mu_t)/sd_t*sd+mu_s
src_cls,_=classes('ak74'); S=to_lab(img('fp_ak74.glb','arms_baseColor')); SMR=img('fp_ak74.glb','arms_metallicRoughness')
def build(tag,base_glb,prefix):
    tc,raw=classes(tag); T=to_lab(img(base_glb,prefix+'_baseColor')); TMR=img(base_glb,prefix+'_metallicRoughness')
    skin=(T[...,1]>8)&(T[...,2]>12)&(T[...,0]>25)          # bare skin (hands, wrist band) -> glove
    skin=ndimage.binary_dilation(ndimage.binary_opening(skin,iterations=2),iterations=3)
    tc=tc.copy(); tc[skin]=3
    out=np.zeros_like(T); mr=TMR.copy()
    src3=np.where(src_cls==2,3,src_cls)
    for k in (1,2,3):
        sc=src3 if k==3 else src_cls
        if not (tc==k).any(): continue
        out[tc==k]=match(T,S,tc,sc,k,lscale=0.5 if k==3 else 1.0)[tc==k]
        for ch in (1,2): mr[...,ch][tc==k]=match(TMR[...,ch:ch+1],SMR[...,ch:ch+1],tc,sc,k)[...,0][tc==k]
    # the AK sleeve's wear (scuffs and pale streaks): high-pass of a patch of its sleeve, mirrored-tiled over ours
    patch=S[720:1100,140:1250]; det=patch-ndimage.gaussian_filter(patch,(24,24,0))
    reps=(int(np.ceil(T.shape[0]/det.shape[0]))+1,int(np.ceil(T.shape[1]/det.shape[1]))+1)
    tile=np.block([[det,det[:,::-1]],[det[::-1],det[::-1,::-1]]]) if False else None
    big=np.concatenate([np.concatenate([det if (i+jj)%2==0 else det[:,::-1] for jj in range(reps[1])],1) for i in range(reps[0])],0)
    big=big[:T.shape[0],:T.shape[1]]
    own=out-ndimage.gaussian_filter(out,(24,24,0))
    sl=(tc==1)[...,None]
    out=np.where(sl,out-own*0.4+big*0.6,out)
    rgb=from_lab(out)
    # soften the class seam (glove cuff into sleeve) by a few texels
    soft=ndimage.gaussian_filter((tc==2).astype(float),3)
    g=from_lab(np.where((tc==2)[...,None],out,out)); rgb=rgb
    return rgb,np.clip(mr,0,1)
if __name__=='__main__':
    for tag,glb,pre in (('pistol','fp_pistol.glb','material'),('knife','fp_knife.glb','arms'),('heavy_pistol','fp_knife.glb','arms')):
        rgb,mr=build(tag,glb,pre)
        Image.fromarray((rgb*255+0.5).astype(np.uint8)).save(f'/tmp/arms/new_{tag}_baseColor.jpg',quality=92)
        Image.fromarray((mr*255+0.5).astype(np.uint8)).save(f'/tmp/arms/new_{tag}_metallicRoughness.jpg',quality=92)
        print('built',tag)
