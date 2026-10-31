# Measure every glb under assets/ (static bounds of all mesh vertices at rest, node transforms applied)
import json,struct,sys,os,numpy as np
def load(p):
    if p.endswith('.gltf'): return json.load(open(p)),None
    b=open(p,'rb').read(); n=struct.unpack('<I',b[12:16])[0]; g=json.loads(b[20:20+n]); bin_=b[20+n+8:]
    return g,bin_
def mat(n):
    if 'matrix' in n: return np.array(n['matrix']).reshape(4,4).T
    T=np.eye(4); T[:3,3]=n.get('translation',[0,0,0])
    x,y,z,w=n.get('rotation',[0,0,0,1])
    R=np.array([[1-2*(y*y+z*z),2*(x*y-z*w),2*(x*z+y*w)],[2*(x*y+z*w),1-2*(x*x+z*z),2*(y*z-x*w)],[2*(x*z-y*w),2*(y*z+x*w),1-2*(x*x+y*y)]])
    S=np.diag(n.get('scale',[1,1,1])); M=np.eye(4); M[:3,:3]=R@S; return T@M
def bounds(p):
    g,b=load(p); mn=np.full(3,1e9); mx=-mn
    def walk(i,P):
        n=g['nodes'][i]; M=P@mat(n)
        if 'mesh' in n:
            for pr in g['meshes'][n['mesh']]['primitives']:
                a=g['accessors'][pr['attributes']['POSITION']]
                lo=np.array(a['min']);hi=np.array(a['max'])
                corners=np.array([[x,y,z,1] for x in (lo[0],hi[0]) for y in (lo[1],hi[1]) for z in (lo[2],hi[2])])
                M2=np.eye(4) if 'skin' in n else M
                c=(M2@corners.T).T[:,:3]; mn[:]=np.minimum(mn,c.min(0)); mx[:]=np.maximum(mx,c.max(0))
        for c in n.get('children',[]): walk(c,M)
    for r in g['scenes'][g.get('scene',0)]['nodes']: walk(r,np.eye(4))
    return mn,mx
root=sys.argv[1]
for d,_,fs in sorted(os.walk(root)):
    for f in sorted(fs):
        if f.endswith(('.glb','.gltf')) and '_lod' not in f:
            p=os.path.join(d,f)
            try:
                mn,mx=bounds(p); s=mx-mn
                print(f"{os.path.relpath(p,root):60s} W {s[0]:7.2f} H {s[1]:7.2f} D {s[2]:7.2f}  minY {mn[1]:6.2f}")
            except Exception as e: print(p,'ERR',e)
