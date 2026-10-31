import json,struct,sys
sys.path.insert(0,'/tmp/arms'); from glbtex import read
def put(path,repl):
    """Replace image payloads (by image name) in a .glb, re-packing the BIN chunk; everything else is untouched."""
    j,b=read(path); views=j['bufferViews']; new={}
    for I in j['images']:
        if I.get('name') in repl: new[I['bufferView']]=repl[I['name']]
    out=bytearray()
    for i,v in enumerate(views):
        data=new.get(i, b[v.get('byteOffset',0):v.get('byteOffset',0)+v['byteLength']])
        while len(out)%4: out+=b'\0'
        v['byteOffset']=len(out); v['byteLength']=len(data); out+=data
    while len(out)%4: out+=b'\0'
    j['buffers'][0]['byteLength']=len(out)
    js=json.dumps(j,separators=(',',':')).encode()
    while len(js)%4: js+=b' '
    total=12+8+len(js)+8+len(out)
    with open(path,'wb') as f:
        f.write(struct.pack('<III',0x46546C67,2,total)); f.write(struct.pack('<II',len(js),0x4E4F534A)); f.write(js)
        f.write(struct.pack('<II',len(out),0x004E4942)); f.write(out)
    print('patched',path,len(new),'images',total//1024,'KB')
