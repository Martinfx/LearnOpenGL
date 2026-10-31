#!/usr/bin/env python3
"""Spent-casing drop sounds (casing_concrete / casing_dirt / casing_metal) from real CC0 recordings.

    python3 -I assets/sounds/source/build_casings.py [cache_dir]

No dedicated CC0 brass-casing recording is reachable, so these are assembled from real recordings of
small metal objects being dropped: Vinrax "Coin Drop" (a coin bouncing and settling on a table) and
BMacZero "Metal Impact Sounds" clinks, plus Fantozzi sand steps (pitched up, very short) for the dirt contact. Each hit grain is
pitched up to a casing's size, and grains are laid out as a real bounce: gaps and levels shrink with every
bounce (restitution ~0.6), ending in a short rattle. Mono 16-bit 48 kHz, ~-24 LUFS (momentary max).
Sources and licences: ../CREDITS.md.
"""
import os,sys,subprocess,urllib.request,io
import numpy as np,soundfile as sf
CACHE=sys.argv[1] if len(sys.argv)>1 else '/tmp/sound_cache'
OUT=os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)),'..'))
SR=48000
OGA='https://opengameart.org/sites/default/files/'
SRC={'coin':'coin_drop.wav','clink1':'clink1_0.wav','clink2':'clink2.wav','clink3':'clink3.wav'}
def path(key):
    d=os.path.join(CACHE,key); p=os.path.join(d,SRC[key])
    if not os.path.exists(p):
        os.makedirs(d,exist_ok=True)
        data=urllib.request.urlopen(urllib.request.Request(OGA+SRC[key],headers={'User-Agent':'Mozilla/5.0'})).read()
        open(p,'wb').write(data)
    return p
def load(p,af=None):
    cmd=['ffmpeg','-v','error','-i',p]+(['-af',af] if af else [])+['-ac','1','-ar',str(SR),'-f','f32le','-']
    return np.frombuffer(subprocess.run(cmd,capture_output=True,check=True).stdout,np.float32).copy()
def pitch(x,k):  # resample: k>1 = smaller object, higher and shorter
    n=int(len(x)/k); return np.interp(np.arange(n)*k,np.arange(len(x)),x)
def grain(x,t,dur,ring):
    a=max(0,int((t-0.002)*SR)); y=x[a:a+int(dur*SR)].copy()
    env=np.exp(-np.arange(len(y))/(ring*SR)); env[:int(0.001*SR)]*=np.linspace(0,1,int(0.001*SR)); return y*env
def hits(x,thr=0.25,gap=0.035):
    m=np.abs(x); hop=int(0.001*SR); e=np.maximum.reduceat(m,np.arange(0,len(m),hop)); out=[]; last=-1
    for i,v in enumerate(e):
        if v>e.max()*thr and (not out or i*hop/SR-out[-1]>gap): out.append(i*hop/SR)
    return out
def mloud(x):
    from scipy.signal import lfilter
    k=lfilter([1.0,-2.0,1.0],[1,-1.99004745483398,0.99007225036621],
              lfilter([1.53512485958697,-2.69169618940638,1.19839281085285],[1,-1.69065929318241,0.73248077421585],x))
    w=int(0.4*SR); k=np.pad(k,(0,max(0,w-len(k)))); c=np.concatenate([[0],np.cumsum(k*k)])
    return -0.691+10*np.log10(max(1e-12,np.max((c[w:]-c[:-w])/w)))
def level(y,target=-24.0,ceil=0.7):
    for _ in range(4):
        y=y*10**((target-mloud(y))/20)
        if np.abs(y).max()>ceil: y=ceil*np.tanh(y/ceil)
    return y
def write(rel,x,length):
    y=np.zeros(int(length*SR)); n=min(len(x),len(y)); y[:n]=x[:n]
    y+=np.resize(FLOOR,len(y))*10**(-62/20)/max(1e-9,np.abs(FLOOR).max())   # the recording's own room tone, no digital silence
    f=int(0.03*SR); y[-f:]*=np.linspace(1,0,f)
    sf.write(os.path.join(OUT,rel),np.clip(y,-1,1),SR,subtype='PCM_16'); print('wrote',rel,round(length,2),'s')
def sound(rel,files,vol,dist):
    with open(os.path.join(OUT,rel),'w') as f:
        f.write('# Sound: variations (one is picked at random per play) and playback settings\n')
        for fn in files: f.write(f'file "{fn}"\n')
        f.write(f'volume {vol}\npitch 0.92 1.08\ndistance {dist[0]} {dist[1]}\nrolloff 1\nloop 0\nstream 0\nspatial 1\ngroup sfx\nmax_instances 12\n')
def bounce(grains,first_gap,n,rest=0.62,gain=0.6,jitter=0.15,rng=None,rattle=None):
    """Lay grains out like a bouncing casing: each gap and level shrinks by the restitution factor."""
    out=np.zeros(int(1.0*SR)); t=0.004; g=1.0; gap=first_gap
    for i in range(n):
        gr=grains[rng.integers(len(grains))]*g*(1+rng.uniform(-jitter,jitter))
        a=int(t*SR); out[a:a+len(gr)]+=gr[:len(out)-a]; t+=gap*(1+rng.uniform(-jitter,jitter)); gap*=rest; g*=gain
    if rattle is not None:
        a=int(t*SR); r=rattle[:len(out)-a]; out[a:a+len(r)]+=r*g*3
    return out

rng=np.random.default_rng(7)
coin=load(path('coin'),'highpass=f=300')
ch=hits(coin)
FLOOR=load(path('coin'),'highpass=f=150')[int(0.05*SR):int(0.4*SR)]                                     # 3 clear table hits, then a short spinning rattle
settle=coin[int((ch[-1]+0.07)*SR):int(1.06*SR)]; settle=settle*np.linspace(1,0,len(settle))**1.5
clinks=[load(path(k),'highpass=f=400') for k in ('clink1','clink2','clink3')]
def fantozzi(name):  # Fantozzi footsteps (CC0), already in the cache from build_sounds.py
    for r,_,fs in os.walk(os.path.join(CACHE,'fantozzi')):
        if name in fs: return os.path.join(r,name)
    raise FileNotFoundError(name+' (run build_sounds.py first)')
sand=[load(fantozzi(n),'highpass=f=300') for n in ('Fantozzi-SandL1.flac','Fantozzi-SandR2.flac','Fantozzi-SandL3.flac')]

out={'concrete':[],'metal':[],'dirt':[]}
# concrete / hard floor: bright brass ticks, 3-5 bounces and a short rattle
for v in range(6):
    k=rng.uniform(1.25,1.6)
    gr=[pitch(grain(coin,t,0.16,0.03),k) for t in ch]+[pitch(grain(c,hits(c)[0],0.06,0.012),k*1.1) for c in clinks]
    y=bounce(gr,rng.uniform(0.10,0.15),rng.integers(3,6),rng=rng,rattle=pitch(settle,k)*0.5)
    y=level(y); write(f'weapons/casing_concrete_{v+1}.wav',y,0.5); out['concrete'].append(f'casing_concrete_{v+1}.wav')
# metal floor / plate: longer ring, the floor answers with its own clink
for v in range(5):
    k=rng.uniform(1.1,1.4)
    gr=[pitch(grain(coin,t,0.18,0.06),k) for t in ch]
    y=bounce(gr,rng.uniform(0.11,0.16),rng.integers(3,5),rest=0.66,gain=0.65,rng=rng,rattle=pitch(settle,k)*0.6)
    c=clinks[v%3]; c=pitch(grain(c,hits(c)[0],0.25,0.08),rng.uniform(0.9,1.1)); y[:len(c)]+=c*0.35
    y=level(y,-23.0); write(f'weapons/casing_metal_{v+1}.wav',y,0.6); out['metal'].append(f'casing_metal_{v+1}.wav')
# dirt / grass: one or two damped ticks swallowed by the ground, no rattle
coin_d=load(path('coin'),'highpass=f=250,lowpass=f=3500')
for v in range(5):
    k=rng.uniform(1.2,1.5)
    gr=[pitch(grain(coin_d,t,0.04,0.006),k) for t in ch]
    y=bounce(gr,rng.uniform(0.06,0.09),rng.integers(1,3),rest=0.5,gain=0.35,rng=rng)
    sd=sand[v%3]; sd=pitch(grain(sd,hits(sd,0.4)[0],0.06,0.012),2.2)*0.5   # soft soil contact (real sand)
    y[:len(sd)]+=sd
    y=level(y,-27.0); write(f'weapons/casing_dirt_{v+1}.wav',y,0.3); out['dirt'].append(f'casing_dirt_{v+1}.wav')
sound('weapons/casing_concrete.sound',out['concrete'],0.8,(1,15))
sound('weapons/casing_metal.sound',out['metal'],0.8,(1,18))
sound('weapons/casing_dirt.sound',out['dirt'],0.7,(1,10))
