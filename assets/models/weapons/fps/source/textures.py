from PIL import Image, ImageFilter, ImageChops
import random, numpy as np
rng=np.random.default_rng(7)
# dirty hands
im=Image.open('/tmp/dl/a_fps_20arms_7z/new_diff.png').convert('RGB'); W,H=im.size
a=np.asarray(im).astype(np.float32)/255
def noise(scale,seed):
    r=np.random.default_rng(seed); n=r.random((max(2,H//scale),max(2,W//scale))).astype(np.float32)
    return np.asarray(Image.fromarray((n*255).astype(np.uint8)).resize((W,H),Image.BICUBIC)).astype(np.float32)/255
blot=np.clip((noise(24,1)*0.6+noise(6,2)*0.4-0.45)*2.2,0,1)
fine=noise(2,3)
dirt=np.array([0.23,0.19,0.15],np.float32)
k=(0.22+0.38*blot)[...,None]*(0.8+0.2*fine[...,None])
out=a*(1-k)+dirt*k
out*=0.9
Image.fromarray(np.clip(out*255,0,255).astype(np.uint8)).save('/tmp/out/hands_dirty.jpg',quality=92)
# knit sleeve 1024
S=1024; y,x=np.mgrid[0:S,0:S].astype(np.float32)
rib=0.5+0.5*np.cos(x/S*2*np.pi*96)          # vertical ribs
stitch=0.5+0.5*np.cos((y/S*2*np.pi*140)+np.sin(x/S*2*np.pi*96)*1.2)
n=np.asarray(Image.fromarray((rng.random((S//4,S//4))*255).astype(np.uint8)).resize((S,S),Image.BICUBIC)).astype(np.float32)/255
g=0.55+0.25*rib*stitch+0.2*n
base=np.array([0.075,0.095,0.06],np.float32)
col=base*g[...,None]*1.5
Image.fromarray(np.clip(col*255,0,255).astype(np.uint8)).save('/tmp/out/sleeve_knit.jpg',quality=92)
# normal from height
h=rib*stitch*0.7+n*0.3; gy,gx=np.gradient(h*6)
nrm=np.dstack([-gx,-gy,np.ones_like(h)]); nrm/=np.linalg.norm(nrm,axis=2,keepdims=True)
Image.fromarray(((nrm*0.5+0.5)*255).astype(np.uint8)).save('/tmp/out/sleeve_knit_normal.png')
print('ok',W,H)
