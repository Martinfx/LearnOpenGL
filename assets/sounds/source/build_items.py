#!/usr/bin/env python3
"""Item pickup and heal sounds (items/*.wav + .sound), layered from real CC0 Foley recordings.

    python3 -I assets/sounds/source/build_items.py [cache_dir]

Source: artisticdude "RPG Sound Pack" (OpenGameArt, CC0), inventory folder (cloth, cloth-heavy, beads,
chainmail, metal-small, armor-light). Layers:
  pickup_ammo_*   : loose rounds rattling (beads / chainmail) + a small metal knock of the can or box
  pickup_medkit_* : canvas rustle (cloth / cloth-heavy)
  pickup_weapon_* : sling rustle + metal of the rifle (armor-light / metal-small)
  heal_*          : a bandage being unwrapped and pressed on (a run of cloth rustles, ~1.4 s)
Mono 16-bit 48 kHz, momentary-max loudness about -20 LUFS (heal -23). 2D UI-like sounds (spatial 0). Credits: ../CREDITS.md.
"""
import os,sys,io,zipfile,subprocess,urllib.request
import numpy as np,soundfile as sf
from scipy.signal import lfilter
CACHE=os.path.join(sys.argv[1] if len(sys.argv)>1 else '/tmp/sound_cache','rpg')
OUT=os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)),'..','items'))
SR=48000
def src(name):
    d=os.path.join(CACHE,'RPG Sound Pack','inventory')
    if not os.path.isdir(d):
        data=urllib.request.urlopen(urllib.request.Request('https://opengameart.org/sites/default/files/rpg_sound_pack.zip',headers={'User-Agent':'Mozilla/5.0'})).read()
        zipfile.ZipFile(io.BytesIO(data)).extractall(CACHE)
    p=os.path.join(d,name+'.wav')
    x=np.frombuffer(subprocess.run(['ffmpeg','-v','error','-i',p,'-af','highpass=f=80','-ac','1','-ar',str(SR),'-f','f32le','-'],capture_output=True,check=True).stdout,np.float32).astype(np.float64)
    a=int(np.argmax(np.abs(x)>np.abs(x).max()*0.02)); x=x[max(0,a-int(0.003*SR)):]
    return x/np.abs(x).max()
def pitch(x,k): return np.interp(np.arange(0,len(x)-1,k),np.arange(len(x)),x)
def mloud(x):
    k=lfilter([1.0,-2.0,1.0],[1,-1.99004745483398,0.99007225036621],
              lfilter([1.53512485958697,-2.69169618940638,1.19839281085285],[1,-1.69065929318241,0.73248077421585],x))
    w=int(0.4*SR); k=np.pad(k,(0,max(0,w-len(k)))); c=np.concatenate([[0],np.cumsum(k*k)])
    return -0.691+10*np.log10(max(1e-12,np.max((c[w:]-c[:-w])/w)))
def mix(layers,dur):
    y=np.zeros(int(dur*SR))
    for name,t,g,k in layers:
        x=pitch(src(name),k)*g; a=int(t*SR); n=min(len(x),len(y)-a); y[a:a+n]+=x[:n]
    return y
def write(name,y,target):
    nz=np.where(np.abs(y)>np.abs(y).max()*1e-3)[0]; y=y[:nz[-1]+1].copy(); f=int(0.03*SR); y[-f:]*=np.linspace(1,0,f)
    for _ in range(4):
        y=y*10**((target-mloud(y))/20)
        if np.abs(y).max()>0.8: y=0.8*np.tanh(y/0.8)
    sf.write(os.path.join(OUT,name+'.wav'),y,SR,subtype='PCM_16'); print('wrote',name,round(len(y)/SR,2),'s'); return name+'.wav'
def sound(name,files,vol,pitchr=(0.96,1.04)):
    with open(os.path.join(OUT,name+'.sound'),'w') as f:
        f.write('# Sound: variations (one is picked at random per play) and playback settings\n')
        for fn in files: f.write(f'file "{fn}"\n')
        f.write(f'volume {vol}\npitch {pitchr[0]} {pitchr[1]}\ndistance 1 25\nrolloff 1\nloop 0\nstream 0\nspatial 0\ngroup sfx\nmax_instances 2\n')
os.makedirs(OUT,exist_ok=True)
# (layer, start s, gain, playback rate)
sound('pickup_ammo',[
    write('pickup_ammo_1',mix([('beads',0,1.0,0.8),('metal-small1',0.02,0.55,1.0)],0.9),-20),
    write('pickup_ammo_2',mix([('chainmail1',0,0.8,1.15),('metal-small2',0.05,0.6,0.9)],0.9),-20),
    write('pickup_ammo_3',mix([('beads',0.03,0.9,0.7),('chainmail2',0,0.5,1.25),('metal-small3',0,0.5,0.85)],0.9),-20)],0.7)
sound('pickup_medkit',[
    write('pickup_medkit_1',mix([('cloth-heavy',0,1.0,0.9),('cloth',0.12,0.5,1.0)],0.8),-21),
    write('pickup_medkit_2',mix([('cloth',0,1.0,0.85),('cloth-heavy',0.08,0.6,1.05)],0.8),-21),
    write('pickup_medkit_3',mix([('cloth-heavy',0,1.0,0.8)],0.8),-21)],0.7)
sound('pickup_weapon',[
    write('pickup_weapon_1',mix([('cloth-heavy',0,0.8,0.8),('armor-light',0.06,1.0,0.8),('metal-small3',0.18,0.4,0.75)],1.0),-19),
    write('pickup_weapon_2',mix([('cloth',0,0.7,0.8),('armor-light',0.03,1.0,0.72),('metal-small1',0.14,0.45,0.7)],1.0),-19)],0.75)
heal=[]
for i,seq in enumerate(((('cloth',0,0.7,1.1),('cloth',0.32,0.8,0.95),('cloth-heavy',0.68,0.9,1.0),('cloth',1.0,0.5,1.2)),
                            (('cloth-heavy',0,0.6,1.15),('cloth',0.28,0.8,1.0),('cloth',0.55,0.7,1.15),('cloth-heavy',0.9,1.0,0.9)))):
    heal.append(write(f'heal_{i+1}',mix(list(seq),1.7),-23))
sound('heal',heal,0.7,(0.98,1.02))
