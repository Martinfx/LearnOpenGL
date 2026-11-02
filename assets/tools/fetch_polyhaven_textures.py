#!/usr/bin/env python3
"""Download Poly Haven PBR texture sets (CC0) into assets/textures/pbr/<id>/ and write a ready material.

    python3 assets/tools/fetch_polyhaven_textures.py [--res 2k] id [id ...]

Per set: albedo.jpg (sRGB), normal.jpg (OpenGL), roughness.jpg, ao.jpg, height.jpg (and metallic.jpg when present),
assets/materials/pbr/<id>.material with tiling = 1 / real size, and a CREDITS row printed to stdout.
"""
import json,os,sys,urllib.request
ROOT=os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)),'..'))
UA={'User-Agent':'Mozilla/5.0'}
def get(u): return urllib.request.urlopen(urllib.request.Request(u,headers=UA)).read()
MAPS=[('albedo','Diffuse'),('normal','nor_gl'),('roughness','Rough'),('ao','AO'),('height','Displacement'),('metallic','Metal')]
args=sys.argv[1:]; res='2k'
if args and args[0]=='--res': res=args[1]; args=args[2:]
for aid in args:
    info=json.loads(get(f'https://api.polyhaven.com/info/{aid}')); files=json.loads(get(f'https://api.polyhaven.com/files/{aid}'))
    d=os.path.join(ROOT,'textures','pbr',aid); os.makedirs(d,exist_ok=True); have=[]
    for name,key in MAPS:
        if key not in files: continue
        f=files[key][res]; f=f.get('jpg') or f.get('png')
        open(os.path.join(d,name+'.jpg'),'wb').write(get(f['url'])); have.append(name)
    dims=info.get('dimensions',[2000,2000]); w,h=dims[0]/1000,dims[1]/1000
    m=[ '# FPS engine material (physically based: metal/roughness)','version 2',f'name "{info["name"]}"','color 1 1 1','roughness 1',
        f'metallic {1 if "metallic" in have else 0}','normal_strength 1','emissive 0 0 0',f'tiling {1/w:.4f} {1/h:.4f}','world_uv 1']
    for name,key in (('albedo','albedo_map'),('normal','normal_map'),('roughness','roughness_map'),('ao','occlusion_map'),('metallic','metallic_map')):
        if name in have: m.append(f'{key} "../../textures/pbr/{aid}/{name}.jpg"')
    if 'height' in have: m+=['height_depth 0.02',f'height_map "../../textures/pbr/{aid}/height.jpg"']
    open(os.path.join(ROOT,'materials','pbr',aid+'.material'),'w').write('\n'.join(m)+'\n')
    authors=', '.join(info.get('authors',{}).keys())
    print(f'| `{aid}` | {info["name"]} | {authors} | {w:g} × {h:g} m | https://polyhaven.com/a/{aid} |',flush=True)
