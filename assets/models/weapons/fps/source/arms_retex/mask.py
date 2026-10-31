import bpy,sys,re
from PIL import Image,ImageDraw
HAND=re.compile(r'(Hand_|Bone_[LR]\.|palm|middle|pink|ring|point|thumb|Joint_3)')
for f,tag in zip(sys.argv[1::2],sys.argv[2::2]):
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.ops.import_scene.gltf(filepath=f,guess_original_bind_pose=False)
    o=next(o for o in bpy.data.objects if o.type=='MESH' and any(m and m.name in ('arms','material') for m in o.data.materials))
    me=o.data; names={g.index:g.name for g in o.vertex_groups}
    hand=[sum(g.weight for g in v.groups if HAND.search(names[g.group])) for v in me.vertices]
    uv=me.uv_layers[0].data; S=2048
    im=Image.new('L',(S,S),0); d=ImageDraw.Draw(im)
    # UV islands: faces joined across an edge whose two UV ends match on both sides
    par=list(range(len(me.polygons)))
    def fnd(a):
        while par[a]!=a: par[a]=par[par[a]]; a=par[a]
        return a
    key={}
    for p in me.polygons:
        li=list(p.loop_indices); n=len(li)
        for k in range(n):
            a,b=li[k],li[(k+1)%n]; va,vb=me.loops[a].vertex_index,me.loops[b].vertex_index
            ua=tuple(round(c,5) for c in uv[a].uv); ub=tuple(round(c,5) for c in uv[b].uv)
            e=(min(va,vb),max(va,vb)); sig=(e,frozenset([(va,ua),(vb,ub)]))
            if sig in key: par[fnd(p.index)]=fnd(key[sig])
            else: key[sig]=p.index
    vote={}
    for p in me.polygons:
        h=sum(hand[i] for i in p.vertices)/len(p.vertices); r=fnd(p.index); a=p.area
        v=vote.setdefault(r,[0,0]); v[0]+=a*h; v[1]+=a
    for p in me.polygons:
        v=vote[fnd(p.index)]; h=v[0]/v[1]
        pts=[(uv[li].uv[0]*S,(1-uv[li].uv[1])*S) for li in p.loop_indices]
        d.polygon(pts,fill=255 if h>0.35 else 128)
    im.save(f'/tmp/arms/mask_{tag}.png'); print('mask',tag,len(me.polygons))
