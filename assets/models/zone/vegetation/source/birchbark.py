# Procedural birch bark (CC0, made for this project): 512x1024, tiles in U and V.
import numpy as np
from PIL import Image,ImageFilter
W,H=512,1024; rng=np.random.default_rng(7)
def tnoise(sx,sy,seed):
    r=np.random.default_rng(seed).random((sy,sx)).astype(np.float32)
    r=np.tile(r,(3,3)); im=Image.fromarray((r*255).astype(np.uint8)).resize((W*3,H*3),Image.BICUBIC)
    return np.array(im).astype(np.float32)[H:2*H,W:2*W]/255
base=0.80+0.10*tnoise(8,16,1)+0.05*tnoise(64,128,2)
col=np.dstack([base*0.93,base*0.92,base*0.88])
# lenticels: short horizontal dark dashes
img=Image.fromarray((col*255).astype(np.uint8)); px=np.array(img).astype(np.float32)
for _ in range(320):
    x=rng.integers(0,W); y=rng.integers(0,H); L=rng.integers(8,60); t=rng.integers(1,4); d=rng.uniform(0.25,0.6)
    for k in range(L):
        xx=(x+k)%W
        for j in range(t): px[(y+j)%H,xx]*=d+rng.uniform(0,0.15)
# black patches (older bark)
patch=tnoise(6,10,3); patch2=tnoise(24,40,4)
m=np.clip((patch*0.7+patch2*0.3-0.62)*8,0,1)
px=px*(1-m[...,None])+np.array([38,34,30],np.float32)*m[...,None]
# slight grime
px*=(0.92+0.08*tnoise(32,64,5))[...,None]
Image.fromarray(np.clip(px,0,255).astype(np.uint8)).filter(ImageFilter.GaussianBlur(0.6)).save('/tmp/veg/atlas/bark_birch.png')
# simple normal from luminance
lum=px.mean(2)/255; gx=np.roll(lum,-1,1)-np.roll(lum,1,1); gy=np.roll(lum,-1,0)-np.roll(lum,1,0)
n=np.dstack([-gx*3,gy*3,np.ones_like(lum)]); n/=np.linalg.norm(n,axis=2,keepdims=True)
Image.fromarray(((n*0.5+0.5)*255).astype(np.uint8)).save('/tmp/veg/atlas/bark_birch_nor.png')
