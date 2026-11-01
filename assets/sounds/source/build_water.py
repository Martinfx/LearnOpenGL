#!/usr/bin/env python3
"""Water sounds: swim strokes, body splashes, underwater loop, bullet hits on water, from real CC0 recordings.

    python3 -I assets/sounds/source/build_water.py [cache_dir]

player/swim_*         : strokes from jcpmcdonald "Skippy Fish water sound collection" (swim.wav) and the thrashing
                        part (0.25-2.5 s, before the voice) of peludo "drowning" (water splash and sand footsteps)
player/splash_*       : peludo splash1 / splash2, pitched down a little and layered with the low body of ezwa
                        "6 short water splashes" #04 for the weight of a whole body
ambient/underwater_loop : gmason "Underwater or space engine rumble" low-passed under BMacZero "Bubble sound
                        effects" bubble loops (muffled), 20 s with a crossfaded seam, about -24 LUFS
impacts/impact_water_*: short hits of ezwa splashes (#02 #03 #05) and rubberduck "40 CC0 water/splash/slime SFX"
                        (splash_09, splash_10), high-passed and shortened to a bullet's plop and spray
All sources are CC0 (OpenGameArt). Mono 16-bit 48 kHz except the loop (stereo). Credits: ../CREDITS.md.
"""
import os,sys,io,zipfile,subprocess,urllib.request
import numpy as np,soundfile as sf
from scipy.signal import lfilter
CACHE=os.path.join(sys.argv[1] if len(sys.argv)>1 else '/tmp/sound_cache','water')
OUT=os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)),'..'))
SR=48000; OGA='https://opengameart.org/sites/default/files/'
def fetch(name):
    p=os.path.join(CACHE,name)
    if not os.path.exists(p):
        os.makedirs(CACHE,exist_ok=True)
        open(p,'wb').write(urllib.request.urlopen(urllib.request.Request(OGA+name,headers={'User-Agent':'Mozilla/5.0'})).read())
    return p
def ezwa(n):
    d=os.path.join(CACHE,'ezwa')
    if not os.path.isdir(d):
        import py7zr; py7zr.SevenZipFile(fetch('ezwa-water_splash.7z')).extractall(d)
    return os.path.join(d,'ezwa-water_splash',f'water_splash-{n:02d}.flac')
def rubber(n):
    d=os.path.join(CACHE,'slime')
    if not os.path.isdir(d): zipfile.ZipFile(fetch('water-splash-slime-sfx.zip')).extractall(d)
    return os.path.join(d,n+'.ogg')
def load(p,af='highpass=f=40',ch=1):
    x=np.frombuffer(subprocess.run(['ffmpeg','-v','error','-i',p,'-af',af,'-ac',str(ch),'-ar',str(SR),'-f','f32le','-'],capture_output=True,check=True).stdout,np.float32).astype(np.float64)
    return x.reshape(-1,ch) if ch>1 else x
def seg(x,a,b): return x[int(a*SR):int(b*SR)].copy()
def start(x,thr=0.05):
    a=int(np.argmax(np.abs(x)>np.abs(x).max()*thr)); return x[max(0,a-int(0.004*SR)):]
def pitch(x,k): return np.interp(np.arange(0,len(x)-1,k),np.arange(len(x)),x)
def kw(x):
    return lfilter([1.0,-2.0,1.0],[1,-1.99004745483398,0.99007225036621],
                   lfilter([1.53512485958697,-2.69169618940638,1.19839281085285],[1,-1.69065929318241,0.73248077421585],x,axis=0),axis=0)
def mloud(x):
    k=kw(x); k=k if k.ndim>1 else k[:,None]; w=int(0.4*SR); k=np.pad(k,((0,max(0,w-len(k))),(0,0)))
    c=np.concatenate([np.zeros((1,k.shape[1])),np.cumsum(k*k,axis=0)]).sum(1)
    return -0.691+10*np.log10(max(1e-12,np.max((c[w:]-c[:-w])/w)))
def lufs(x):
    k=kw(x); k=k if k.ndim>1 else k[:,None]; return -0.691+10*np.log10(np.mean(np.sum(k*k,axis=1)))
def write(rel,y,target,meas=mloud,fade_in=0.004,fade=0.05):
    y=np.array(y,dtype=np.float64)
    nz=np.where(np.abs(y)>np.abs(y).max()*10**(-55/20))[0]; y=y[:nz[-1]+1].copy()
    f=int(fade*SR); y[-f:]*=np.linspace(1,0,f)[:,None] if y.ndim>1 else np.linspace(1,0,f)
    f=int(fade_in*SR); y[:f]*=np.linspace(0,1,f)[:,None] if y.ndim>1 else np.linspace(0,1,f)
    for _ in range(4):
        y=y*10**((target-meas(y))/20)
        if np.abs(y).max()>0.8: y=0.8*np.tanh(y/0.8)
    sf.write(os.path.join(OUT,rel),y,SR,subtype='PCM_16'); print('wrote',rel,round(len(y)/SR,2),'s'); return os.path.basename(rel)
def sound(rel,files,vol,dist,pitchr=(0.95,1.05),loop=0,spatial=1,maxi=4,stream=0):
    with open(os.path.join(OUT,rel),'w') as f:
        f.write('# Sound: variations (one is picked at random per play) and playback settings\n')
        for fn in files: f.write(f'file "{fn}"\n')
        f.write(f'volume {vol}\npitch {pitchr[0]} {pitchr[1]}\ndistance {dist[0]} {dist[1]}\nrolloff 1\nloop {loop}\nstream {stream}\nspatial {spatial}\ngroup sfx\nmax_instances {maxi}\n')

# swimming strokes
sw=load(fetch('swim.wav')); dr=load(fetch('drowning_0.wav'))
strokes=[seg(sw,0.25,0.95),seg(sw,1.05,1.85),seg(dr,0.25,1.25),seg(dr,1.45,2.45)]
sound('player/swim.sound',[write(f'player/swim_{i+1}.wav',s,-22,fade_in=0.03,fade=0.12) for i,s in enumerate(strokes)],0.75,(1,30),maxi=2)
# a body falling in
low=start(load(ezwa(4),'lowpass=f=900'))
out=[]
for i,(name,k) in enumerate((('splash1_0.wav',0.88),('splash2_0.wav',0.85))):
    x=pitch(start(load(fetch(name))),k); y=np.zeros(len(x)+SR//2); y[:len(x)]+=x; y[:len(low)]+=0.6*low[:len(y)]*np.abs(x).max()/np.abs(low).max()
    out.append(write(f'player/splash_{i+1}.wav',y,-16))
sound('player/splash.sound',out,0.9,(2,60),maxi=2)
# underwater: muffled rumble + bubbles, a 20 s loop with a crossfaded seam
L=20.0; X=2.0
r=load(fetch('underwater_or_space_engine_0.ogg'),'lowpass=f=500,highpass=f=30',2)
b=load(fetch('bubbles-loop2-amp.wav'),'lowpass=f=700',1); b=np.tile(b,int(np.ceil((L+X)*SR/len(b))+1))[:int((L+X)*SR)]
b2=load(fetch('bubbles-loop1-amp.wav'),'lowpass=f=600',1); b2=np.tile(b2,int(np.ceil((L+X)*SR/len(b2))+1))[:int((L+X)*SR)]
n=int((L+X)*SR); m=r[int(10*SR):int(10*SR)+n]/np.abs(r).max()
m[:,0]+=0.18*b/np.abs(b).max(); m[:,1]+=0.18*b2/np.abs(b2).max()
xf=int(X*SR); t=np.linspace(0,1,xf)[:,None]; loopy=m[:int(L*SR)].copy(); loopy[:xf]=loopy[:xf]*np.sqrt(t)+m[int(L*SR):int(L*SR)+xf]*np.sqrt(1-t)
g=-24-lufs(loopy); loopy*=10**(g/20); print('underwater peak',np.abs(loopy).max())
sf.write(os.path.join(OUT,'ambient/underwater_loop.wav'),loopy,SR,subtype='PCM_16'); print('wrote ambient/underwater_loop.wav')
sound('ambient/underwater.sound',['underwater_loop.wav'],0.8,(1,10),pitchr=(1,1),loop=1,spatial=0,maxi=1)
# bullet into water: plop + spray, short
out=[]
for i,p in enumerate((ezwa(2),ezwa(5),ezwa(3),rubber('splash_10'),rubber('splash_09'))):
    x=start(load(p,'highpass=f=250'))[:int(0.45*SR)]; x=pitch(x,1.12) if i%2 else x
    x=x*np.exp(-np.arange(len(x))/(0.16*SR))
    out.append(write(f'impacts/impact_water_{i+1}.wav',x,-20,fade=0.04))
sound('impacts/impact_water.sound',out,0.8,(2,60),pitchr=(0.92,1.08),maxi=6)
