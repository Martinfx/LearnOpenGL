import json,struct,sys
def read(f):
    b=open(f,'rb').read(); n=struct.unpack('<I',b[12:16])[0]; j=json.loads(b[20:20+n])
    o=20+n; bl=struct.unpack('<I',b[o:o+4])[0]; bin_=b[o+8:o+8+bl]; return j,bin_
def image_bytes(j,bin_,name):
    for I in j['images']:
        if I.get('name')==name:
            bv=j['bufferViews'][I['bufferView']]; s=bv.get('byteOffset',0); return bin_[s:s+bv['byteLength']]
if __name__=='__main__':
    f,out=sys.argv[1],sys.argv[2]; j,b=read(f)
    for I in j['images']:
        bv=j['bufferViews'][I['bufferView']]; s=bv.get('byteOffset',0)
        open(f'{out}_{I["name"]}.jpg','wb').write(b[s:s+bv['byteLength']])
