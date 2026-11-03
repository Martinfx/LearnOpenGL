# 2x2 twig atlases: albedo (RGB * soft AO, alpha) with colour padding, OpenGL normal map (RGB, padded with flat).
import json,numpy as np,sys,os
from PIL import Image
from scipy import ndimage
out=sys.argv[1]; os.makedirs(out,exist_ok=True)
for name in ('birch','broad','pinnate','lance'):
    A=np.zeros((2048,2048,4),np.float32); N=np.zeros((2048,2048,3),np.float32)
    for ci in range(4):
        al=np.asarray(Image.open(f'cells/{name}_{ci}_albedo.png')).astype(np.float32)/255
        nr=np.load(f'cells/{name}_{ci}_normal.npy'); ao=np.load(f'cells/{name}_{ci}_ao.npy')
        a=al[...,3]
        w=np.maximum(nr[...,3],1e-4)[...,None]
        n=nr[...,:3]/w; n=n*2-1; n/=np.maximum(np.linalg.norm(n,axis=2,keepdims=True),1e-4)
        o=np.clip(ao[...,0]/np.maximum(ao[...,3],1e-4),0,1)
        rgb=al[...,:3]*(0.55+0.45*o)[...,None]
        # pad: nearest opaque colour into the transparent texels (no dark fringes in mips)
        solid=a>0.5
        idx=ndimage.distance_transform_edt(~solid,return_distances=False,return_indices=True)
        rgb=rgb[idx[0],idx[1]]; n=np.where(solid[...,None],n,n[idx[0],idx[1]]*0.5+np.array([0,0,0.5]))
        n/=np.maximum(np.linalg.norm(n,axis=2,keepdims=True),1e-4)
        y,x=(ci//2)*1024,(ci%2)*1024
        A[y:y+1024,x:x+1024,:3]=rgb; A[y:y+1024,x:x+1024,3]=a; N[y:y+1024,x:x+1024]=n*0.5+0.5
    Image.fromarray((np.clip(A,0,1)*255+0.5).astype(np.uint8),'RGBA').save(f'{out}/twigs_{name}.png',optimize=True)
    Image.fromarray((np.clip(N,0,1)*255+0.5).astype(np.uint8),'RGB').save(f'{out}/twigs_{name}_nor.png',optimize=True)
    print(name)
