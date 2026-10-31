#!/usr/bin/env python3
"""Bullet ricochet sounds (ricochet_*.wav, ricochet_metal_*.wav) from real CC0 metal recordings.

    python3 -I assets/sounds/source/build_ricochets.py [cache_dir]

No real recorded ricochet with a usable licence is reachable (OpenGameArt only has CC-BY-SA game sets and
archive.org items without provenance; the CC0 range recordings we scanned contain no whine). So each
ricochet is built from real recordings, not oscillators:
  * ping  : a real metal hit (blacklodgegames "27 Metal Audio Samples" and BMacZero clinks), pitched up
  * whine : the steady ring of a real struck metal piece, bent downward over time like the Doppler drop of
            a receding bullet (x1.35 -> x0.6), with the flutter of a tumbling slug (40-90 Hz) and a little of
            the hit's own noise for air.
ricochet_*: ping + long whine (0.5-0.8 s). ricochet_metal_*: bright hard ping + short whine (0.3-0.5 s).
Mono 16-bit 48 kHz. Sources and licences: ../CREDITS.md.
"""
import os,sys,subprocess,urllib.request,io,zipfile
import numpy as np,soundfile as sf
CACHE=sys.argv[1] if len(sys.argv)>1 else '/tmp/sound_cache'
OUT=os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)),'..'))
SR=48000
OGA='https://opengameart.org/sites/default/files/'
METAL27='27-Metal-Audio-Samples-blacklodgegames.com_.zip'
def metal27(name):
    d=os.path.join(CACHE,'metal27')
    if not os.path.isdir(d):
        data=urllib.request.urlopen(urllib.request.Request(OGA+METAL27,headers={'User-Agent':'Mozilla/5.0'})).read()
        zipfile.ZipFile(io.BytesIO(data)).extractall(d)
    for r,_,fs in os.walk(d):
        for f in fs:
            if f.startswith(name) and '__MACOSX' not in r: return os.path.join(r,f)
    raise FileNotFoundError(name)
def clink(k):
    p=os.path.join(CACHE,k,{'clink1':'clink1_0.wav','clink2':'clink2.wav','clink3':'clink3.wav'}[k])
    if not os.path.exists(p): raise FileNotFoundError(p+' (run build_sounds.py first)')
    return p
def load(p,af=None):
    cmd=['ffmpeg','-v','error','-i',p]+(['-af',af] if af else [])+['-ac','1','-ar',str(SR),'-f','f32le','-']
    return np.frombuffer(subprocess.run(cmd,capture_output=True,check=True).stdout,np.float32).astype(np.float64)
def onset(x,thr=0.3):
    return int(np.argmax(np.abs(x)>np.abs(x).max()*thr))
def varispeed(x,rate):
    """Read x with a time-varying playback rate (array, per output sample)."""
    pos=np.cumsum(rate); pos=pos[pos<len(x)-1]; return np.interp(pos,np.arange(len(x)),x)
def pitch(x,k): return varispeed(x,np.full(int(len(x)/k),k))
def mloud(x):
    from scipy.signal import lfilter
    k=lfilter([1.0,-2.0,1.0],[1,-1.99004745483398,0.99007225036621],
              lfilter([1.53512485958697,-2.69169618940638,1.19839281085285],[1,-1.69065929318241,0.73248077421585],x))
    w=int(0.4*SR); k=np.pad(k,(0,max(0,w-len(k)))); c=np.concatenate([[0],np.cumsum(k*k)])
    return -0.691+10*np.log10(max(1e-12,np.max((c[w:]-c[:-w])/w)))
def level(y,target,ceil=0.79):
    for _ in range(4):
        y=y*10**((target-mloud(y))/20)
        if np.abs(y).max()>ceil: y=ceil*np.tanh(y/ceil)
    return y
def write(rel,y):
    f=int(0.02*SR); y=y.copy(); y[-f:]*=np.linspace(1,0,f)
    sf.write(os.path.join(OUT,rel),np.clip(y,-1,1),SR,subtype='PCM_16'); print('wrote',rel,round(len(y)/SR,2),'s')
def sound(rel,files,vol,dist,maxi=8):
    with open(os.path.join(OUT,rel),'w') as f:
        f.write('# Sound: variations (one is picked at random per play) and playback settings\n')
        for fn in files: f.write(f'file "{fn}"\n')
        f.write(f'volume {vol}\npitch 0.9 1.1\ndistance {dist[0]} {dist[1]}\nrolloff 1\nloop 0\nstream 0\nspatial 1\ngroup sfx\nmax_instances {maxi}\n')

rng=np.random.default_rng(23)
# steady real rings to bend into whines (the sustained part of struck metal, after the attack)
def ring(name,start,dur=1.0):
    x=load(metal27(name),'highpass=f=600'); a=onset(x)+int(start*SR); return x[a:a+int(dur*SR)]
RINGS=[ring('dull_metal_collision_14',0.25,0.7),ring('rapid_clangs_01',0.35,1.0),ring('dull_metal_collision_08',0.2,0.6)]
RINGS=[r/np.abs(r).max() for r in RINGS]
RING_HZ=[3326,1722,1065]   # measured dominant partial of each ring
PINGS=[load(clink(k),'highpass=f=500') for k in ('clink1','clink2','clink3')]+\
      [load(metal27(n),'highpass=f=700') for n in ('dull_metal_collision_14','dull_metal_collision_09','wobble_07')]
PINGS=[p[max(0,onset(p)-int(0.001*SR)):] for p in PINGS]

def whine(dur,hz_start,drop,flutter):
    """Bend a real ring down in pitch over `dur` s (start tone hz_start, end hz_start*drop), with tumbling flutter."""
    j=rng.integers(len(RINGS)); src=np.tile(RINGS[j],12); f_start=hz_start/RING_HZ[j]; f_end=f_start*drop
    n=int(dur*SR); t=np.arange(n)/SR
    k=f_start*(f_end/f_start)**((t/dur)**0.7)                  # Doppler-like glide, fastest at the start
    k*=1+0.012*np.sin(2*np.pi*flutter*t+rng.uniform(0,6))      # tumbling slug wobbles the pitch
    y=varispeed(src,k)[:n]; y=np.pad(y,(0,n-len(y)))
    am=1-0.35*(0.5+0.5*np.sin(2*np.pi*flutter*t))              # ... and the level
    env=np.minimum(1,t/0.03)*np.exp(-t/(dur*0.38))
    return y*am*env
def ricochet(dur,ping_gain,whine_gain,ping_k,glide,flutter):
    p=PINGS[rng.integers(len(PINGS))]; p=pitch(p,ping_k)[:int(0.35*SR)]
    p=p*np.exp(-np.arange(len(p))/(0.07*SR)); p/=np.abs(p).max()
    w=whine(dur-0.02,*glide,flutter); w/=max(1e-9,np.abs(w).max())
    y=np.zeros(int(dur*SR)); y[:len(p)]+=p*ping_gain; d=int(0.012*SR); y[d:d+len(w)]+=w[:len(y)-d]*whine_gain
    return y

out=[]
for i in range(6):   # generic: rock / concrete / ground glance, the long whine carries it
    dur=rng.uniform(0.55,0.8)
    y=ricochet(dur,0.5,1.0,rng.uniform(1.0,1.5),(rng.uniform(3200,4800),rng.uniform(0.45,0.6)),rng.uniform(40,90))
    write(f'weapons/ricochet_{i+1}.wav',level(y,-20.0)); out.append(f'ricochet_{i+1}.wav')
sound('weapons/ricochet.sound',out,0.9,(3,80))
out=[]
for i in range(5):   # metal: hard bright ping, short whine
    dur=rng.uniform(0.32,0.5)
    y=ricochet(dur,1.0,0.7,rng.uniform(1.3,1.9),(rng.uniform(4000,5500),rng.uniform(0.6,0.75)),rng.uniform(60,110))
    write(f'weapons/ricochet_metal_{i+1}.wav',level(y,-19.0)); out.append(f'ricochet_metal_{i+1}.wav')
sound('weapons/ricochet_metal.sound',out,0.9,(3,80))
