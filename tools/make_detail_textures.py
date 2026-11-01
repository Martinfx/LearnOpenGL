#!/usr/bin/env python3
# Tileable grey grain detail map and its normal map (assets/textures/detail_grain*.png). Run from the repo root.
import numpy as np
from PIL import Image
rng=np.random.default_rng(7)
N=256
def band(lo,hi,amp):
    f=np.fft.fft2(rng.standard_normal((N,N)))
    k=np.sqrt(np.add.outer(np.fft.fftfreq(N)**2,np.fft.fftfreq(N)**2))*N
    m=((k>=lo)&(k<hi)).astype(float)
    r=np.real(np.fft.ifft2(f*m)); return amp*r/r.std()
h=band(2,8,0.5)+band(8,32,0.35)+band(32,128,0.25)
h=h/h.std()
grey=np.clip(0.5+h*0.09,0,1)
Image.fromarray((grey*255+0.5).astype(np.uint8),'L').save('assets/textures/detail_grain.png',optimize=True)
s=2.5
dx=(np.roll(h,-1,1)-np.roll(h,1,1))*0.5*s/8; dy=(np.roll(h,-1,0)-np.roll(h,1,0))*0.5*s/8
n=np.dstack([-dx,dy,np.ones_like(h)]); n/=np.linalg.norm(n,axis=2,keepdims=True)
Image.fromarray(((n*0.5+0.5)*255+0.5).astype(np.uint8),'RGB').save('assets/textures/detail_grain_normal.png',optimize=True)
