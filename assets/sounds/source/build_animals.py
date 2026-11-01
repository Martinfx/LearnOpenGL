#!/usr/bin/env python3
"""Distant dogs and crows for the ambience sets, from real CC0 recordings on OpenGameArt.

    python3 -I assets/sounds/source/build_animals.py [cache_dir]

dog_bark_far_*  : barks of pauliuw "Dog sounds" and HaelDB "Dog barking mono", laid out as a dog barking in a run,
                  pushed far away (high-pass, air absorption low-pass, early reflections and a long outdoor tail)
dog_whine_far_* : pauliuw "Sad Dog" whimpering, distant
crow_*          : IgnasD "Crows singing" and zeroisnotnull "Crow caw", mid distance
Mono 16-bit 48 kHz WAV, about -22 LUFS (whines -25) (spatialised as random distant sounds by the .ambience sets). Credits: ../CREDITS.md.
"""
import os,sys,subprocess,urllib.request,io
import numpy as np,soundfile as sf
CACHE=os.path.join(sys.argv[1] if len(sys.argv)>1 else '/tmp/sound_cache','animals')
OUT=os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)),'..'))
SR=48000; OGA='https://opengameart.org/sites/default/files/'
def get(name):
    p=os.path.join(CACHE,name)
    if not os.path.exists(p):
        os.makedirs(CACHE,exist_ok=True)
        open(p,'wb').write(urllib.request.urlopen(urllib.request.Request(OGA+urllib.request.quote(name),headers={'User-Agent':'Mozilla/5.0'})).read())
        if p.endswith('.7z'):
            import py7zr; py7zr.SevenZipFile(p).extractall(os.path.join(CACHE,'dog'))
    return p
def load(p,af='highpass=f=150'):
    return np.frombuffer(subprocess.run(['ffmpeg','-v','error','-i',p,'-af',af,'-ac','1','-ar',str(SR),'-f','f32le','-'],capture_output=True,check=True).stdout,np.float32).astype(np.float64)
def onsets(x,thr=0.3,gap=0.25):
    m=np.abs(x); h=int(SR*0.005); e=np.maximum.reduceat(m,np.arange(0,len(m),h)); out=[]
    for i,v in enumerate(e):
        t=i*h/SR
        if v>e.max()*thr and (not out or t-out[-1]>gap): out.append(t)
    return out
def grain(x,t,dur):
    a=max(0,int((t-0.01)*SR)); y=x[a:a+int(dur*SR)].copy(); f=int(0.04*SR); y[-f:]*=np.linspace(1,0,f); return y
def process(y,af):
    p=subprocess.run(['ffmpeg','-v','error','-f','f64le','-ar',str(SR),'-ac','1','-i','-','-af',af,'-f','f64le','-'],input=y.tobytes(),capture_output=True,check=True)
    return np.frombuffer(p.stdout,np.float64)
FAR='apad=pad_dur=2.5,highpass=f=300,lowpass=f=2300,aecho=0.8:0.55:70|160|330|610:0.32|0.22|0.15|0.09'
MID='apad=pad_dur=1.2,highpass=f=250,lowpass=f=6000,aecho=0.8:0.5:45|120|260:0.22|0.14|0.08'
def lufs(y):
    o=subprocess.run(['ffmpeg','-nostats','-f','f64le','-ar',str(SR),'-ac','1','-i','-','-af','ebur128','-f','null','-'],input=np.ascontiguousarray(y).tobytes(),capture_output=True).stderr.decode()
    return float(o.split('Summary:')[1].split('I:')[1].split('LUFS')[0])
def write(rel,y,target):
    y=np.array(y); nz=np.where(np.abs(y)>np.abs(y).max()*10**(-60/20))[0]; y=y[:nz[-1]+1].copy() if len(nz) else y
    f=int(0.08*SR); y[-f:]*=np.linspace(1,0,f); y=y/np.abs(y).max()*0.5
    for _ in range(3):
        y=y*10**((target-lufs(y))/20)
        if np.abs(y).max()>0.8: y=0.8*np.tanh(y/0.8)   # soft-limit a stray click
    sf.write(os.path.join(OUT,rel),y,SR,subtype='PCM_16'); print('wrote',rel,round(len(y)/SR,2),'s')
def sound(rel,files,vol,dist,pitch=(0.95,1.05)):
    with open(os.path.join(OUT,rel),'w') as f:
        f.write('# Sound: variations (one is picked at random per play) and playback settings\n')
        for fn in files: f.write(f'file "{fn}"\n')
        f.write(f'volume {vol}\npitch {pitch[0]} {pitch[1]}\ndistance {dist[0]} {dist[1]}\nrolloff 1\nloop 0\nstream 0\nspatial 1\ngroup ambient\nmax_instances 2\n')

rng=np.random.default_rng(9)
get('dog.7z'); D=os.path.join(CACHE,'dog','Dog')
barks=[load(os.path.join(D,f)) for f in ('Dog Bark.wav','Dog Bark 1.wav','Dog Bark 2.wav','Dog Bark 3.wav')]
hb=load(get('dog_barking_mono.wav')); barks+=[grain(hb,t,0.45) for t in onsets(hb)]
for f in ('Dog.wav','Dog 1.wav','Dog 2.wav'):
    x=load(os.path.join(D,f)); barks+=[grain(x,t,0.3) for t in onsets(x)[:2]]
barks=[b/np.abs(b).max() for b in barks]
out=[]
for i in range(6):   # a dog barking in a run: 2-5 barks, uneven gaps, the same dog (one source + small pitch drift)
    n=int(rng.integers(2,6)); src=int(rng.integers(len(barks))); k=rng.uniform(0.9,1.08)
    y=np.zeros(int(4.5*SR)); t=0.05
    for j in range(n):
        b=barks[(src+int(rng.integers(0,2)))%len(barks)]
        kk=k*rng.uniform(0.98,1.02); b=np.interp(np.arange(0,len(b)-1,kk),np.arange(len(b)),b)*rng.uniform(0.75,1.0)
        a=int(t*SR); y[a:a+len(b)]+=b[:len(y)-a]; t+=len(b)/SR+rng.uniform(0.18,0.6)
        if t>3.6: break
    y=process(y[:int((t+0.2)*SR)],FAR); write(f'ambient/dog_bark_far_{i+1}.wav',y,-22); out.append(f'dog_bark_far_{i+1}.wav')
sound('ambient/dog_bark_far.sound',out,0.9,(40,600))
out=[]
for i,f in enumerate(('Sad Dog.wav','Sad Dog 1.wav')):
    x=load(os.path.join(D,f)); y=process(x[int(0.1*SR):],FAR); write(f'ambient/dog_whine_far_{i+1}.wav',y,-25); out.append(f'dog_whine_far_{i+1}.wav')
sound('ambient/dog_whine_far.sound',out,0.7,(30,300))
cr=load(get('Crows.ogg'),'highpass=f=200'); cc=load(get('crow_caw.wav'),'highpass=f=200')
ts=onsets(cr,0.25,0.2); out=[]
for i in range(6):   # 2-4 caws from one bird
    a=ts[(i*3+1)%len(ts)]; n=int(rng.integers(2,5)); b=ts[min(len(ts)-1,(i*3+1)%len(ts)+n)]   # skip the clipped first caw
    if b<=a: b=a+1.2
    y=cr[int((a-0.02)*SR):int((b+0.1)*SR)].copy(); y=process(y,MID); write(f'ambient/crow_{i+1}.wav',y,-22); out.append(f'crow_{i+1}.wav')
y=process(cc,MID); write('ambient/crow_7.wav',y,-22); out.append('crow_7.wav')
sound('ambient/crow.sound',out,0.8,(15,250),pitch=(0.93,1.07))
