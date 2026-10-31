# Force alphaMode MASK + doubleSided on materials (in place). Optional --match=<substr> restricts to matching names.
import json,struct,sys
args=[a for a in sys.argv[1:] if not a.startswith('--match=')]; match=[a[8:] for a in sys.argv[1:] if a.startswith('--match=')]
for p in args:
    b=open(p,'rb').read(); n=struct.unpack('<I',b[12:16])[0]; g=json.loads(b[20:20+n]); rest=b[20+n:]
    for m in g.get('materials',[]):
        if match and not any(s in m.get('name','') for s in match): continue
        m['alphaMode']='MASK'; m['alphaCutoff']=0.5; m['doubleSided']=True
    j=json.dumps(g,separators=(',',':')).encode(); j+=b' '*((4-len(j)%4)%4)
    out=b[:12]+struct.pack('<I',len(j))+b'JSON'+j+rest
    out=out[:8]+struct.pack('<I',len(out))+out[12:]; open(p,'wb').write(out)
