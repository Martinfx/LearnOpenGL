#!/usr/bin/env python3
"""Download Poly Haven (CC0) models as 1K glTF into assets/models/props/<id>/ and print CREDITS rows.

    python3 -I assets/tools/fetch_polyhaven_props.py id1 id2 ...
"""
import os,sys,json,urllib.request
UA={'User-Agent':'Mozilla/5.0 (asset fetch for an open-source game)'}
OUT=os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)),'..','models','props'))
def get(url):
    for i in range(4):
        try: return urllib.request.urlopen(urllib.request.Request(url,headers=UA),timeout=120).read()
        except Exception as e: err=e
    raise err
for pid in sys.argv[1:]:
    info=json.loads(get(f'https://api.polyhaven.com/info/{pid}')); files=json.loads(get(f'https://api.polyhaven.com/files/{pid}'))
    g=files['gltf']['1k']['gltf']; d=os.path.join(OUT,pid); os.makedirs(d,exist_ok=True)
    open(os.path.join(d,f'{pid}.gltf'),'wb').write(get(g['url']))
    for rel,f in g.get('include',{}).items():
        p=os.path.join(d,rel); os.makedirs(os.path.dirname(p),exist_ok=True); open(p,'wb').write(get(f['url']))
    authors=', '.join(info.get('authors',{}).keys())
    print(f"| `{pid}` | {info['name']} | {authors} | {', '.join(c for c in info.get('categories',[]) if not c.startswith('collection'))} | https://polyhaven.com/a/{pid} |")
