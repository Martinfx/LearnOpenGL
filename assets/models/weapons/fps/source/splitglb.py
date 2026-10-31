# split one sampled glTF animation into several clips by time ranges (seconds)
import json,struct,sys,numpy as np
src,dst,spec=sys.argv[1],sys.argv[2],json.loads(sys.argv[3])
b=open(src,'rb').read()
jl=struct.unpack('<I',b[12:16])[0]; g=json.loads(b[20:20+jl]); bin_=bytearray(b[20+jl+8:])
def acc(i):
    a=g['accessors'][i];bv=g['bufferViews'][a['bufferView']]
    n={'SCALAR':1,'VEC3':3,'VEC4':4}[a['type']]
    return np.frombuffer(bytes(bin_),np.float32,count=a['count']*n,offset=bv.get('byteOffset',0)+a.get('byteOffset',0)).reshape(a['count'],n)
def add(arr,typ):
    global bin_
    while len(bin_)%4: bin_+=b'\0'
    off=len(bin_); bin_+=arr.astype(np.float32).tobytes()
    g['bufferViews'].append({'buffer':0,'byteOffset':off,'byteLength':arr.nbytes})
    a={'bufferView':len(g['bufferViews'])-1,'componentType':5126,'count':len(arr),'type':typ}
    if typ=='SCALAR': a['min']=[float(arr.min())];a['max']=[float(arr.max())]
    g['accessors'].append(a); return len(g['accessors'])-1
src_anim=g['animations'][0]; new=[]
for name,(t0,t1) in spec.items():
    an={'name':name,'channels':[],'samplers':[]}
    for ch in src_anim['channels']:
        sm=src_anim['samplers'][ch['sampler']]
        T=acc(sm['input'])[:,0]; V=acc(sm['output'])
        m=(T>=t0-1e-4)&(T<=t1+1e-4)
        if m.sum()==0: continue
        typ=g['accessors'][sm['output']]['type']
        an['samplers'].append({'input':add((T[m]-T[m][0]).reshape(-1,1)[:,0],'SCALAR'),'output':add(V[m],typ),'interpolation':sm.get('interpolation','LINEAR')})
        an['channels'].append({'sampler':len(an['samplers'])-1,'target':ch['target']})
    new.append(an)
g['animations']=new
g['buffers'][0]['byteLength']=len(bin_)
js=json.dumps(g).encode(); js+=b' '*((4-len(js)%4)%4)
while len(bin_)%4: bin_+=b'\0'
out=struct.pack('<III',0x46546C67,2,12+8+len(js)+8+len(bin_))+struct.pack('<II',len(js),0x4E4F534A)+js+struct.pack('<II',len(bin_),0x004E4942)+bytes(bin_)
open(dst,'wb').write(out)
print('clips',[(a['name'],len(a['channels'])) for a in new])
