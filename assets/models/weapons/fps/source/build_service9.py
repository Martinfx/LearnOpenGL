# Build fp_service9.glb: the classic 9 mm service pistol (Poly Haven "service_pistol", CC0)
# held by the DJMaesen heavy pistol arms rig (fp_heavy_pistol.glb, CC-BY 4.0).
#
# The heavy pistol rig has its weapon parts rigidly skinned to W_* bones, so the gun is swapped
# without touching the arms or the animations: every Talon part is replaced by the matching
# service pistol part, moved into the Talon's bind pose and skinned 100 % to the same W_* bone.
#
#   python3 build_service9.py <fp_heavy_pistol.glb> <service_pistol.gltf> <fp_out.glb> <world_out.gltf> [variant a|b]
#
# variant a = wooden grips (default), b = grips wrapped in blue tape.
# world_out is the same gun alone (magazine in, no arms) for pickups and traders: it lies on its
# right side, origin under its centre, so it rests on the ground at y = 0.
import json,struct,sys,os,numpy as np

src,sp_path,dst,world_dst=sys.argv[1],sys.argv[2],sys.argv[3],sys.argv[4]
variant=sys.argv[5] if len(sys.argv)>5 else 'a'

# ---- placement (glTF axes; the service pistol model points +X, up +Y; the rig points -Z) ----
# rotate +90 deg about Y: (x,y,z) -> (z,y,-x)
ROT=np.array([[0,0,1],[0,1,0],[-1,0,0]],np.float32)
# translation chosen so the trigger and the back strap sit where the Talon's were (see README)
GUN_T=np.array([0.0400,-0.0890,-0.3160],np.float32)
# the magazine is modelled beside the gun; this moves it into the grip (model axes)
MAG_IN=np.array([0.0935,0.0,0.0],np.float32)
# cartridges: model stands upright (+Y = tip); rig wants them lying along -Z: (x,y,z)->(x,z,-y)
ROT_BULLET=np.array([[1,0,0],[0,0,1],[0,-1,0]],np.float32)

def read_glb(p):
    b=open(p,'rb').read(); jl=struct.unpack('<I',b[12:16])[0]
    g=json.loads(b[20:20+jl]); return g,bytes(b[20+jl+8:])
def read_gltf(p):
    g=json.load(open(p)); d=os.path.dirname(p)
    return g,open(os.path.join(d,g['buffers'][0]['uri']),'rb').read(),d

g,bin0=read_glb(src)
sg,sbin,sdir=read_gltf(sp_path)

COMP={5126:np.float32,5123:np.uint16,5125:np.uint32,5121:np.uint8}
NC={'SCALAR':1,'VEC2':2,'VEC3':3,'VEC4':4,'MAT4':16}
def acc(gg,bb,i):
    a=gg['accessors'][i]; bv=gg['bufferViews'][a['bufferView']]; n=NC[a['type']]; dt=COMP[a['componentType']]
    stride=bv.get('byteStride',0); off=bv.get('byteOffset',0)+a.get('byteOffset',0)
    if stride and stride!=n*np.dtype(dt).itemsize:
        rows=[np.frombuffer(bb,dt,count=n,offset=off+k*stride) for k in range(a['count'])]
        return np.array(rows)
    return np.frombuffer(bb,dt,count=a['count']*n,offset=off).reshape(a['count'],n).copy()

# ---- rebuild the binary buffer, keeping only what is still referenced ----
out=bytearray(); views=[]; accs=[]
def put(raw,target=None):
    while len(out)%4: out.append(0)
    o=len(out); out.extend(raw); v={'buffer':0,'byteOffset':o,'byteLength':len(raw)}
    if target: v['target']=target
    views.append(v); return len(views)-1
def put_acc(arr,typ,ctype=5126,target=None,minmax=False):
    v=put(np.ascontiguousarray(arr).tobytes(),target)
    a={'bufferView':v,'componentType':ctype,'count':int(len(arr)),'type':typ}
    if minmax: a['min']=[float(x) for x in arr.min(0)]; a['max']=[float(x) for x in arr.max(0)]
    accs.append(a); return len(accs)-1
def copy_acc(i):
    a=dict(g['accessors'][i]); bv=g['bufferViews'][a['bufferView']]
    raw=bin0[bv.get('byteOffset',0):bv.get('byteOffset',0)+bv['byteLength']]
    nv=dict(bv); nv['byteOffset']=0
    while len(out)%4: out.append(0)
    nv['byteOffset']=len(out); out.extend(raw); views.append(nv)
    a['bufferView']=len(views)-1; accs.append(a); return len(accs)-1

skin=g['skins'][0]
joint_of={g['nodes'][j]['name']:k for k,j in enumerate(skin['joints'])}

# service pistol parts by name
sp_nodes={n['name']:n for n in sg['nodes']}
def sp_part(name,rot,trans,pre=np.zeros(3,np.float32)):
    n=sp_nodes[name]; pr=sg['meshes'][n['mesh']]['primitives'][0]
    P=acc(sg,sbin,pr['attributes']['POSITION'])+np.array(n.get('translation',[0,0,0]),np.float32)+pre
    N=acc(sg,sbin,pr['attributes']['NORMAL'])
    UV=acc(sg,sbin,pr['attributes']['TEXCOORD_0'])
    I=acc(sg,sbin,pr['indices'])[:,0].astype(np.uint32)
    return (P@rot.T+trans).astype(np.float32),(N@rot.T).astype(np.float32),UV,I

v=variant
bul_c=lambda m:(np.array(g['accessors'][g['meshes'][m]['primitives'][0]['attributes']['POSITION']]['min'])+
                np.array(g['accessors'][g['meshes'][m]['primitives'][0]['attributes']['POSITION']]['max']))/2
mesh_of_node={g['nodes'][i]['name']:g['nodes'][i]['mesh'] for i in range(len(g['nodes'])) if 'mesh' in g['nodes'][i]}
def bullet_at(talon_mesh):
    P,N,UV,I=sp_part('service_pistol_bullet',ROT_BULLET,np.zeros(3,np.float32))
    c=(P.min(0)+P.max(0))/2
    return P+(bul_c(talon_mesh)-c).astype(np.float32),N,UV,I
parts={  # Talon node -> (W bone, geometry)
    'base_talon_0':('W_base_talon_0',sp_part('service_pistol_pistol_'+v,ROT,GUN_T)),
    'slide_talon_0':('W_slide_talon_0',sp_part('service_pistol_slide_'+v,ROT,GUN_T)),
    'hammer_talon_0':('W_hammer_talon_0',sp_part('service_pistol_hammer_'+v,ROT,GUN_T)),
    'trigger_talon_0':('W_trigger_talon_0',sp_part('service_pistol_trigger_'+v,ROT,GUN_T)),
    'clip_talon_0':('W_clip_talon_0',sp_part('service_pistol_magazine_loaded',ROT,GUN_T,MAG_IN)),
    'bullet_talon_0':('W_bullet_talon_0',bullet_at(mesh_of_node['bullet_talon_0'])),
    'bullet2_talon_0':('W_bullet2_talon_0',bullet_at(mesh_of_node['bullet2_talon_0'])),
}
DROP={'slidecatch_talon_0'}  # the P38-style pistol has its slide stop on the frame

# ---- images / textures / materials ----
imgs=[];texs=[];mats=[];samplers=g.get('samplers',[{}])
def add_image(raw,name,mime='image/jpeg'):
    imgs.append({'name':name,'mimeType':mime,'bufferView':put(raw)}); texs.append({'source':len(imgs)-1,'sampler':0})
    return len(texs)-1
# arms material copied as is
arms_mat=[m for m in g['materials'] if m['name']=='arms'][0]
remap={}
for key in ('normalTexture','occlusionTexture'):
    if key in arms_mat:
        t=arms_mat[key]['index']; img=g['images'][g['textures'][t]['source']]; bv=g['bufferViews'][img['bufferView']]
        remap[t]=remap.get(t) or add_image(bin0[bv['byteOffset']:bv['byteOffset']+bv['byteLength']],img['name'],img['mimeType'])
for key in ('baseColorTexture','metallicRoughnessTexture'):
    t=arms_mat['pbrMetallicRoughness'][key]['index']
    if t not in remap:
        img=g['images'][g['textures'][t]['source']]; bv=g['bufferViews'][img['bufferView']]
        remap[t]=add_image(bin0[bv['byteOffset']:bv['byteOffset']+bv['byteLength']],img['name'],img['mimeType'])
am=json.loads(json.dumps(arms_mat))
for key in ('normalTexture','occlusionTexture'):
    if key in am: am[key]['index']=remap[am[key]['index']]
for key in ('baseColorTexture','metallicRoughnessTexture'):
    am['pbrMetallicRoughness'][key]['index']=remap[am['pbrMetallicRoughness'][key]['index']]
mats.append(am); ARMS=0
tex=lambda f:open(os.path.join(sdir,'textures',f),'rb').read()
t_n=add_image(tex('service_pistol_nor_gl_2k.jpg'),'service9_normal')
t_c=add_image(tex('service_pistol_diff_2k.jpg'),'service9_baseColor')
t_a=add_image(tex('service_pistol_arm_2k.jpg'),'service9_occlusionRoughnessMetallic')
mats.append({'name':'service9','normalTexture':{'index':t_n},'occlusionTexture':{'index':t_a},
             'pbrMetallicRoughness':{'baseColorTexture':{'index':t_c},'metallicRoughnessTexture':{'index':t_a}}})
GUN=1

# ---- meshes ----
meshes=[]
for ni,n in enumerate(g['nodes']):
    if 'mesh' not in n: continue
    name=n['name']
    if name in DROP:
        del n['mesh']; n.pop('skin',None); continue
    if name in parts:
        bone,(P,N,UV,I)=parts[name]
        J=np.zeros((len(P),4),np.uint8); J[:,0]=joint_of[bone]
        W=np.zeros((len(P),4),np.float32); W[:,0]=1
        attrs={'POSITION':put_acc(P,'VEC3',target=34962,minmax=True),'NORMAL':put_acc(N,'VEC3',target=34962),
               'TEXCOORD_0':put_acc(UV,'VEC2',target=34962),'JOINTS_0':put_acc(J,'VEC4',5121,34962),
               'WEIGHTS_0':put_acc(W,'VEC4',target=34962)}
        meshes.append({'name':name.replace('talon','service9'),'primitives':[{'attributes':attrs,
               'indices':put_acc(I.reshape(-1,1),'SCALAR',5125,34963),'material':GUN}]})
        n['name']=name.replace('talon','service9')
    else:  # arms
        m=g['meshes'][n['mesh']]; prs=[]
        for pr in m['primitives']:
            p2={'attributes':{k:copy_acc(a) for k,a in pr['attributes'].items()},'material':ARMS}
            if 'indices' in pr: p2['indices']=copy_acc(pr['indices'])
            prs.append(p2)
        meshes.append({'name':m['name'],'primitives':prs})
    n['mesh']=len(meshes)-1
for nn in g['nodes']:
    if nn.get('name','').startswith('W_') and 'talon' in nn['name']: nn['name']=nn['name'].replace('talon','service9')

skin['inverseBindMatrices']=copy_acc(skin['inverseBindMatrices'])
# The source `reload` throws the slide back at 0.3 s with no hand on it and keeps it there until the
# support hand racks it at the end. Keep the slide forward until the hand takes it (frame 43): all W_*
# bones share one parent and rest pose, so "slide forward" means the slide follows the frame (W_base).
SLIDE_HOLD={'reload':43/24}
node_id=lambda prefix:[i for i,n in enumerate(g['nodes']) if n.get('name','').startswith(prefix)][0]
slide_node,base_node=node_id('W_slide'),node_id('W_base')
for an in g['animations']:
    fix={}
    if an.get('name') in SLIDE_HOLD:
        base_ch={ch['target']['path']:dict(an['samplers'][ch['sampler']]) for ch in an['channels'] if ch['target']['node']==base_node}
        for ch in an['channels']:
            if ch['target']['node']==slide_node: fix[ch['sampler']]=(SLIDE_HOLD[an['name']],base_ch.get(ch['target']['path']),ch['target']['path'])
    for si,sm in enumerate(an['samplers']):
        if si in fix:
            cut,bsm,path=fix[si]
            T=acc(g,bin0,sm['input'])[:,0]; V=acc(g,bin0,sm['output'])
            if bsm is not None:
                BT=acc(g,bin0,bsm['input'])[:,0]; BV=acc(g,bin0,bsm['output'])
                BI=np.stack([np.interp(T,BT,BV[:,k]) for k in range(BV.shape[1])],1)
            else:  # the frame is not animated on this path: its rest value
                BI=np.tile(np.array(g['nodes'][base_node].get(path,{'scale':[1,1,1],'translation':[0,0,0],'rotation':[0,0,0,1]}[path]),np.float32),(len(T),1))
            if path=='rotation': BI/=np.linalg.norm(BI,axis=1,keepdims=True)
            m=T<cut-1e-4; V[m]=BI[m]
            typ=g['accessors'][sm['output']]['type']
            sm['input']=copy_acc(sm['input']); sm['output']=put_acc(V.astype(np.float32),typ)
        else:
            sm['input']=copy_acc(sm['input']); sm['output']=copy_acc(sm['output'])

g['accessors']=accs; g['bufferViews']=views; g['images']=imgs; g['textures']=texs; g['materials']=mats; g['meshes']=meshes
g['samplers']=samplers or [{}]
g['asset']={'version':'2.0','generator':'build_service9.py (FPS engine)'}
while len(out)%4: out.append(0)
g['buffers']=[{'byteLength':len(out)}]
js=json.dumps(g,separators=(',',':')).encode(); js+=b' '*((4-len(js)%4)%4)
data=struct.pack('<III',0x46546C67,2,12+8+len(js)+8+len(out))+struct.pack('<II',len(js),0x4E4F534A)+js+struct.pack('<II',len(out),0x004E4942)+bytes(out)
open(dst,'wb').write(data)
print('wrote',dst,len(data)//1024,'KiB; meshes',[m['name'] for m in meshes])

# ---- world model: the gun alone, lying on its right side ----
# model axes: +X muzzle, +Y up, +Z left side. Lying on the right side = model +Z becomes world +Y;
# the muzzle keeps pointing +X: (x,y,z) -> (x,-z,y)
ROT_W=np.array([[1,0,0],[0,0,-1],[0,1,0]],np.float32)
wparts=[sp_part('service_pistol_'+k+'_'+v,ROT_W,np.zeros(3,np.float32)) for k in ('pistol','slide','hammer','trigger')]
wparts.append(sp_part('service_pistol_magazine_loaded',ROT_W,np.zeros(3,np.float32),MAG_IN))
allP=np.concatenate([p[0] for p in wparts]); lo,hi=allP.min(0),allP.max(0)
shift=np.array([-(lo[0]+hi[0])/2,-lo[1],-(lo[2]+hi[2])/2],np.float32)
out=bytearray(); views=[]; accs=[]; imgs=[]; texs=[]
P=[];N=[];UV=[];I=[];base=0
for (p,n,uv,ix) in wparts:
    P.append(p+shift); N.append(n); UV.append(uv); I.append(ix+base); base+=len(p)
P,N,UV,I=np.concatenate(P),np.concatenate(N),np.concatenate(UV),np.concatenate(I)
attrs={'POSITION':put_acc(P,'VEC3',target=34962,minmax=True),'NORMAL':put_acc(N,'VEC3',target=34962),'TEXCOORD_0':put_acc(UV,'VEC2',target=34962)}
ind=put_acc(I.reshape(-1,1),'SCALAR',5125,34963)
# textures are shared with service_pistol/ (relative uris), not copied
rel=os.path.relpath(os.path.join(sdir,'textures'),os.path.dirname(os.path.abspath(world_dst))).replace(os.sep,'/')
for f,nm in (('service_pistol_nor_gl_2k.jpg','service9_normal'),('service_pistol_diff_2k.jpg','service9_baseColor'),
             ('service_pistol_arm_2k.jpg','service9_occlusionRoughnessMetallic')):
    imgs.append({'name':nm,'mimeType':'image/jpeg','uri':rel+'/'+f}); texs.append({'source':len(imgs)-1,'sampler':0})
t_n,t_c,t_a=0,1,2
w={'asset':{'version':'2.0','generator':'build_service9.py (FPS engine)'},'scene':0,'scenes':[{'nodes':[0]}],
   'nodes':[{'name':'service9','mesh':0}],
   'meshes':[{'name':'service9','primitives':[{'attributes':attrs,'indices':ind,'material':0}]}],
   'materials':[{'name':'service9','normalTexture':{'index':t_n},'occlusionTexture':{'index':t_a},
                 'pbrMetallicRoughness':{'baseColorTexture':{'index':t_c},'metallicRoughnessTexture':{'index':t_a}}}],
   'textures':texs,'images':imgs,'samplers':[{}],'accessors':accs,'bufferViews':views}
binname=os.path.splitext(os.path.basename(world_dst))[0]+'.bin'
w['buffers']=[{'byteLength':len(out),'uri':binname}]
open(os.path.join(os.path.dirname(world_dst),binname),'wb').write(bytes(out))
json.dump(w,open(world_dst,'w'),indent=1)
print('wrote',world_dst,len(out)//1024,'KiB; size',[round(float(x),3) for x in (hi-lo)])
