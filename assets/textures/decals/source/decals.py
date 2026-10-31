# Procedural bullet-hole decal atlas (made for this project, CC0). 4x4 cells of 256 px:
# row 0 concrete, row 1 metal, row 2 wood, row 3 generic/soft (plaster, dirt).
import numpy as np, sys
from PIL import Image
rng=np.random.default_rng(7); C=256; N=4
def noise(n,oct=5,seed=0):
    r=np.random.default_rng(seed); out=np.zeros((n,n))
    for o in range(oct):
        s=2**(o+2); g=r.standard_normal((s+1,s+1))
        im=np.array(Image.fromarray(g.astype(np.float32)).resize((n,n),Image.BICUBIC))
        out+=im/(2**o)
    return out/np.abs(out).max()
yy,xx=np.mgrid[0:C,0:C]; cx=cy=C/2
def cell(kind,seed):
    r=np.random.default_rng(seed)
    ang=np.arctan2(yy-cy,xx-cx); d=np.hypot(xx-cx,yy-cy)/(C/2)
    nz=noise(C,5,seed); nzA=noise(C,3,seed+99)
    nzP=noise(C,6,seed+7)
    jag=1+0.06*np.sin(ang*r.integers(3,7)+r.random()*6)+0.45*nzA+0.15*nzP
    if kind=='concrete':
        core=0.06; chip=0.26*jag
        h=np.where(d<core,-1.0,np.where(d<chip,-0.55*np.clip(1-(d-core)/(chip-core),0,1)**0.7,0))
        h+=0.15*nz*(d<chip)
        a=np.clip((chip*1.15-d)/0.08,0,1)
        dust=np.clip(1-d/0.55,0,1)**2*0.5
        base=np.array([0.42,0.41,0.39])
        col=np.ones((C,C,3))*base; col*=(0.8+0.2*nz)[...,None]; col[d<core*1.6]=[0.05,0.05,0.05]
        col=col*(1-0.4*(-h)[...,None]); a=np.maximum(a,dust*0.6); col=np.where((d>chip)[...,None],np.array([0.55,0.54,0.52]),col)
        rough=0.95
    elif kind=='metal':
        core=0.06; dent=0.2; ring=0.26
        h=np.where(d<core,-1.0,np.where(d<dent,-0.6*(1-(d-core)/(dent-core)),np.where(d<ring,0.25*np.sin((d-dent)/(ring-dent)*np.pi),0)))
        a=np.clip((ring*1.6-d)/0.1,0,1)
        col=np.ones((C,C,3))*np.array([0.62,0.62,0.64]); col*=(0.9+0.1*nz)[...,None]
        col[d<core*1.4]=[0.03,0.03,0.03]
        burn=np.clip(1-(d-ring)/0.25,0,1)*(d>ring); col=col*(1-0.6*burn[...,None])
        col=np.where(((d>dent)&(d<ring))[...,None],np.array([0.68,0.68,0.7]),col)
        rough=0.35
    elif kind=='wood':
        core=0.07
        stretch=np.hypot((xx-cx)/1.0,(yy-cy)/2.2)/(C/2)
        spl=0.17*(1+0.6*nzA+0.2*nzP)
        h=np.where(d<core,-1.0,np.where(stretch<spl,-0.5*(1-stretch/spl),0))
        a=np.clip((spl*1.1-stretch)/0.06,0,1)
        col=np.ones((C,C,3))*np.array([0.72,0.55,0.36])  # fresh splintered wood
        col*= (0.88+0.12*np.sin(xx/1.7+nz*6))[...,None]
        col[d<core*1.5]=[0.06,0.04,0.03]
        rough=0.8
    else:
        core=0.06; c=0.22*jag
        h=np.where(d<core,-1.0,np.where(d<c,-0.4*(1-(d-core)/(c-core)),0))
        a=np.clip((c*1.5-d)/0.12,0,1)
        col=np.ones((C,C,3))*np.array([0.3,0.28,0.26]); col[d<core*1.5]=[0.04,0.04,0.04]
        col*=(1-0.4*np.clip(1-d/0.6,0,1))[...,None]
        rough=0.9
    a[d<core*1.3]=1
    return np.clip(col,0,1),np.clip(a,0,1),h
alb=np.zeros((C*N,C*N,4)); hgt=np.zeros((C*N,C*N))
kinds=['concrete','metal','wood','soft']
for row,k in enumerate(kinds):
    for c in range(N):
        col,a,h=cell(k,row*10+c)
        sl=(slice(row*C,(row+1)*C),slice(c*C,(c+1)*C))
        alb[sl[0],sl[1],:3]=col; alb[sl[0],sl[1],3]=a; hgt[sl]=h*a
# normal map (OpenGL convention, +Y up) from height
gy,gx=np.gradient(hgt*6.0)
n=np.dstack([-gx,gy,np.ones_like(hgt)]); n/=np.linalg.norm(n,axis=2,keepdims=True)
out=sys.argv[1]
Image.fromarray((alb*255).astype(np.uint8),'RGBA').save(out+'/bullet_holes_albedo.png')
Image.fromarray(((n*0.5+0.5)*255).astype(np.uint8)).save(out+'/bullet_holes_normal.png')
print('ok')
