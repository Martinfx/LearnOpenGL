import json,numpy as np,sys
src,dst,mi,k=sys.argv[1],sys.argv[2],int(sys.argv[3]),float(sys.argv[4])
import os as _o; NEG=_o.environ.get('NEG','1')=='1'
import os
d=os.path.dirname(src)+'/'
g=json.load(open(src))
bufs=[open(d+b['uri'],'rb').read() for b in g['buffers']]
def acc(i):
    a=g['accessors'][i];bv=g['bufferViews'][a['bufferView']]
    n={'SCALAR':1,'VEC2':2,'VEC3':3,'VEC4':4}[a['type']]
    dt={5126:np.float32,5123:np.uint16,5125:np.uint32,5121:np.uint8}[a['componentType']]
    off=bv.get('byteOffset',0)+a.get('byteOffset',0)
    assert bv.get('byteStride',0) in (0,n*np.dtype(dt).itemsize)
    return np.frombuffer(bufs[bv['buffer']],dt,count=a['count']*n,offset=off).reshape(a['count'],n).copy()
extra=bytearray()
def add(arr,like,target=None):
    global extra
    while len(extra)%4: extra+=b'\0'
    off=len(extra); extra+=arr.tobytes()
    bv={'buffer':len(g['buffers']),'byteOffset':off,'byteLength':arr.nbytes}
    if target: bv['target']=target
    g['bufferViews'].append(bv)
    a=dict(g['accessors'][like]); a['bufferView']=len(g['bufferViews'])-1; a.pop('byteOffset',None)
    if 'min' in a and arr.ndim==2 and arr.shape[1]==3 and a['type']=='VEC3' and 'POS' in str(like_kind[0]):
        a['min']=arr.min(0).tolist(); a['max']=arr.max(0).tolist()
    g['accessors'].append(a); return len(g['accessors'])-1
like_kind=['']
for p in g['meshes'][mi]['primitives']:
    at=p['attributes']
    P=acc(at['POSITION'])*k; like_kind[0]='POS'
    at['POSITION']=add(P.astype(np.float32),at['POSITION'],34962); like_kind[0]=''
    if k<0:
        if 'NORMAL' in at and NEG: at['NORMAL']=add((-acc(at['NORMAL'])).astype(np.float32),at['NORMAL'],34962)
        if 'TANGENT' in at:
            T=acc(at['TANGENT']); T[:,:3]*=-1; T[:,3]*=-1; at['TANGENT']=add(T.astype(np.float32),at['TANGENT'],34962)
        I=acc(p['indices']).reshape(-1,3)[:,::-1].copy(); p['indices']=add(I,p['indices'],34963)
g['buffers'].append({'uri':'fix.bin','byteLength':len(extra)})
open(os.path.dirname(dst)+'/fix.bin','wb').write(extra)
json.dump(g,open(dst,'w'))
