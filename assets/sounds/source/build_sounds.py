#!/usr/bin/env python3
"""Download real CC0 / CC-BY recordings and cut them into the game's sound files.

    python3 assets/sounds/source/build_sounds.py [cache_dir]

Needs ffmpeg, numpy, soundfile and py7zr. Downloads go to cache_dir (default /tmp/sound_cache); the
outputs replace the files under assets/sounds/. One-shot effects are mono 16-bit 48 kHz WAV (OpenAL only
spatialises mono buffers); ambience loops are stereo Ogg Vorbis with a crossfaded seamless loop point.
Sources and licences: ../CREDITS.md.
"""
import os,sys,subprocess,zipfile,urllib.request,urllib.parse,io
import numpy as np,soundfile as sf
CACHE=sys.argv[1] if len(sys.argv)>1 else '/tmp/sound_cache'
OUT=os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)),'..'))
SR=48000
OGA='https://opengameart.org/sites/default/files/'
ARCHIVES={  # name -> file on OpenGameArt
 'firearms':'Prepared%20SFX%20Library.7z','arreload':'assaultriflereload1_0.wav','gunreload':'gunreload1.wav',
 'hgreload':'reload.wav','clipload1':'clipload1.wav','clipload2':'clipload2.wav','clicks':'equipment_clicks3.wav',
 'kdd':'%5Bkdd%5DDifferentSteps_0.zip','fantozzi':'Fantozzi-footsteps.7z','metalsteps':'metal_steps_48k24b.7z',
 'surfaces':'footsteps_0.zip','screams':'slightscreams.7z','baradari':'michelbaradari-human.7z',
 'windy':'windy_ambiance.ogg','storm':'storm_arwen_2022.flac','birds':'park_ambience_birds.wav','rain':'amb_rain_loop_1.wav','drone60':'drone60.wav','drone70':'drone70.wav',
 'drone53':'drone53.wav','bfh':'sfx_breaking_and_falling.zip','mw':'100-CC0-wood-metal-SFX.zip',
 'woodcrack':'independent_nu_ljudbank-wood_crack_hit_destruction.7z','qimpact':'qubodupImpact.7z',
 'clink1':'clink1_0.wav','clink2':'clink2.wav','clink3':'clink3.wav','bing':'bing1.wav','thud3':'thud3.wav',
}
def fetch(key):
    fn=ARCHIVES[key]; d=os.path.join(CACHE,key)
    if os.path.isdir(d): return d
    os.makedirs(d); data=urllib.request.urlopen(urllib.request.Request(OGA+fn,headers={'User-Agent':'Mozilla/5.0'})).read()
    name=urllib.parse.unquote(fn)
    if name.endswith('.zip'): zipfile.ZipFile(io.BytesIO(data)).extractall(d)
    elif name.endswith('.7z'):
        import py7zr; py7zr.SevenZipFile(io.BytesIO(data)).extractall(d)
    else: open(os.path.join(d,name),'wb').write(data)
    return d
def find(key,name):
    d=fetch(key)
    for r,_,fs in os.walk(d):
        if '__MACOSX' in r: continue
        for f in fs:
            if os.path.join(r,f).endswith(name): return os.path.join(r,f)
    raise FileNotFoundError(key+':'+name)
def load(path,ch=1,af=None):
    cmd=['ffmpeg','-v','error','-i',path]+(['-af',af] if af else [])+['-ac',str(ch),'-ar',str(SR),'-f','f32le','-']
    x=np.frombuffer(subprocess.run(cmd,capture_output=True,check=True).stdout,np.float32).copy()
    return x.reshape(-1,ch) if ch>1 else x
def onsets(x,thr=0.35,gap=0.6):
    m=np.abs(x if x.ndim==1 else x.max(1)); hop=int(SR*0.002); env=np.maximum.reduceat(m,np.arange(0,len(m),hop))
    pk=env.max(); out=[]; last=-1e9
    for i,v in enumerate(env):
        t=i*hop/SR
        if v>pk*thr and t-last>gap: out.append(t); last=t
    return out
def cut(x,t0,dur,pre=0.004,fade=0.5):
    a=max(0,int((t0-pre)*SR)); y=x[a:a+int(dur*SR)].copy(); n=len(y)
    fi=min(n,int(0.002*SR)); y[:fi]*=np.linspace(0,1,fi) if y.ndim==1 else np.linspace(0,1,fi)[:,None]
    nf=int(n*fade); env=np.ones(n); env[n-nf:]=np.linspace(1,0,nf)**2
    return y*(env if y.ndim==1 else env[:,None])
def trim(x,thr=0.02,pre=0.003,tail=0.25):
    m=np.abs(x if x.ndim==1 else x.max(1)); idx=np.where(m>m.max()*thr)[0]
    a=max(0,idx[0]-int(pre*SR)); b=min(len(x),idx[-1]+int(tail*SR)); return x[a:b]
def norm(x,db=-1.0):
    return x*(10**(db/20)/max(1e-9,np.abs(x).max()))
def write(rel,x):
    p=os.path.join(OUT,rel); os.makedirs(os.path.dirname(p),exist_ok=True)
    sf.write(p,np.clip(x,-1,1),SR,subtype='PCM_16'); print('wrote',rel,round(len(x)/SR,2),'s')
def write_loop(rel,x,length,xf=3.0,start=5.0,db=-3.0):
    total=len(x)/SR; start=min(start,max(0,total*0.05)); length=min(length,total-start-xf-0.05)
    a=int(start*SR); L=int(length*SR); n=int(xf*SR); y=x[a:a+L+n].copy()
    w=np.linspace(0,1,n)[:,None]; head=y[:n]*w+y[L:L+n]*(1-w)   # tail fades into head -> seamless
    y=np.concatenate([head,y[n:L]]); y=norm(y,db)
    p=os.path.join(OUT,rel); tmp=p+'.tmp.wav'; sf.write(tmp,y,SR,subtype='PCM_16')
    subprocess.run(['ffmpeg','-v','error','-y','-i',tmp,'-c:a','libvorbis','-q:a','4',p],check=True); os.remove(tmp)
    print('wrote',rel,round(length,1),'s loop')
def sound(rel,files,volume,pitch,dist,loop=0,stream=0,spatial=1,group='sfx',maxi=6):
    p=os.path.join(OUT,rel)
    with open(p,'w') as f:
        f.write('# Sound: variations (one is picked at random per play) and playback settings\n')
        for fn in files: f.write(f'file "{fn}"\n')
        f.write(f'volume {volume}\npitch {pitch[0]} {pitch[1]}\ndistance {dist[0]} {dist[1]}\nrolloff 1\nloop {loop}\nstream {stream}\nspatial {spatial}\ngroup {group}\nmax_instances {maxi}\n')
def shots(src,name,count,dur,hp=40,picks=None,db=-0.5,thr=0.5,gap=1.5):
    x=load(src,1,f'highpass=f={hp}'); ts=onsets(x,thr,gap); ts=[ts[i] for i in picks] if picks else ts[:count]
    out=[]
    for i,t in enumerate(ts,1):
        write(f'weapons/{name}_{i}.wav',norm(cut(x,t,dur,fade=0.6),db)); out.append(f'{name}_{i}.wav')
    return out
# ---------------- weapons (The Free Firearm Sound Library, CC0)
FL='Prepared SFX Library/'
ak=shots(find('firearms','AK-47/C_28P.wav'),'rifle_shot',4,1.8)
akfar=shots(find('firearms','AK-47/C_31P.wav'),'rifle_shot_far',2,2.2,hp=80,db=-3)
pist=shots(find('firearms','Bersa/F_47P.wav'),'pistol_shot',2,1.4)
pist+= [f.replace('pistol_shot_','pistol_shot_') for f in []]
x=load(find('firearms','Walther PPQ/X_39P.wav'),1,'highpass=f=40'); ts=onsets(x,0.5,1.5)
for i,t in enumerate(ts[:2],3): write(f'weapons/pistol_shot_{i}.wav',norm(cut(x,t,1.4,fade=0.6),-0.5)); pist.append(f'pistol_shot_{i}.wav')
smg=shots(find('firearms','Carl Gustav M45/G_31P.wav'),'smg_shot',3,1.1)
heavy=shots(find('firearms','1911/A_42P.wav'),'heavy_pistol_shot',2,1.6)
sniper=shots(find('firearms','Mosin Nagant/M_21P.wav'),'sniper_shot',4,2.6)
sound('weapons/rifle_shot.sound',ak,1,(0.97,1.03),(4,300),maxi=8)
sound('weapons/rifle_shot_far.sound',akfar,0.8,(0.95,1.05),(60,900),maxi=6)
sound('weapons/pistol_shot.sound',pist,0.9,(0.97,1.03),(3,200))
sound('weapons/smg_shot.sound',smg,0.95,(0.97,1.03),(3,220),maxi=10)
sound('weapons/heavy_pistol_shot.sound',heavy,1,(0.97,1.03),(4,250))
sound('weapons/sniper_shot.sound',sniper,1,(0.98,1.02),(6,600))
# reloads / handling (CC0)
write('weapons/reload.wav',norm(trim(load(find('arreload','assaultriflereload1_0.wav'))),-2))
write('weapons/reload_pistol.wav',norm(trim(load(find('hgreload','reload.wav'))),-2))
write('weapons/reload_smg.wav',norm(trim(load(find('gunreload','gunreload1.wav'))),-2))
write('weapons/mag_in.wav',norm(trim(load(find('clipload1','clipload1.wav'))),-3))
write('weapons/mag_out.wav',norm(trim(load(find('clipload2','clipload2.wav'))),-3))
cl=load(find('clicks','equipment_clicks3.wav')); ts=onsets(cl,0.3,0.4)
write('weapons/dry_fire.wav',norm(cut(cl,ts[0],0.18,fade=0.5),-4))
write('weapons/draw.wav',norm(cut(cl,ts[3],0.55,fade=0.4),-3))
write('weapons/bolt.wav',norm(cut(cl,ts[5],0.5,fade=0.4),-3))
sound('weapons/reload.sound',['reload.wav'],0.8,(1,1),(1,30),maxi=2)
sound('weapons/reload_pistol.sound',['reload_pistol.wav'],0.8,(1,1),(1,30),maxi=2)
sound('weapons/reload_smg.sound',['reload_smg.wav'],0.8,(1,1),(1,30),maxi=2)
sound('weapons/mag_in.sound',['mag_in.wav'],0.7,(0.95,1.05),(1,20),maxi=2)
sound('weapons/mag_out.sound',['mag_out.wav'],0.7,(0.95,1.05),(1,20),maxi=2)
sound('weapons/bolt.sound',['bolt.wav'],0.7,(0.97,1.03),(1,20),maxi=2)
sound('weapons/dry_fire.sound',['dry_fire.wav'],0.6,(0.95,1.05),(1,15),maxi=2)
sound('weapons/draw.sound',['draw.wav'],0.6,(0.95,1.05),(1,15),maxi=2)
# ---------------- bullet impacts (CC0)
def imp(name,srcs,dur=0.45,db=-2,af='highpass=f=120'):
    out=[]
    for i,(k,f) in enumerate(srcs,1):
        x=trim(load(find(k,f),1,af),0.05,tail=dur); write(f'impacts/{name}_{i}.wav',norm(cut(x,0.004,dur,fade=0.6),db)); out.append(f'{name}_{i}.wav')
    return out
conc=imp('impact_concrete',[('bfh','bfh1_rock_hit_01.ogg'),('qimpact','qubodupImpactStone.flac'),('bfh','bfh1_rock_breaking_01.ogg'),('bfh','bfh1_rock_breaking_02.ogg')],af='highpass=f=200,asetrate=48000*1.15,aresample=48000')
metal=imp('impact_metal',[('clink1','clink1_0.wav'),('clink2','clink2.wav'),('clink3','clink3.wav'),('bfh','bfh1_metal_hit_01.ogg'),('bfh','bfh1_metal_hit_03.ogg'),('mw','metal_hit_03.ogg')],dur=0.6)
wood=imp('impact_wood',[('woodcrack','impactwood01.mp3.flac'),('woodcrack','impactwood05.mp3.flac'),('woodcrack','impactwood09.mp3.flac'),('woodcrack','impactwood14.mp3.flac')])
dirt=imp('impact_dirt',[('fantozzi','Fantozzi-SandL1.flac'),('fantozzi','Fantozzi-SandR2.flac'),('fantozzi','Fantozzi-SandL3.flac')],dur=0.3,af='highpass=f=150,asetrate=48000*1.35,aresample=48000')
flesh=imp('impact_flesh',[('qimpact','qubodupImpactMeat01.flac'),('qimpact','qubodupImpactMeat02.flac')],dur=0.4,af='highpass=f=80')
sound('impacts/impact_hard.sound',conc,0.7,(0.9,1.1),(2,60),maxi=8)
sound('impacts/impact_concrete.sound',conc,0.7,(0.9,1.1),(2,60),maxi=8)
sound('impacts/impact_metal.sound',metal,0.65,(0.9,1.1),(2,70),maxi=8)
sound('impacts/impact_wood.sound',wood,0.7,(0.9,1.1),(2,60),maxi=8)
sound('impacts/impact_dirt.sound',dirt,0.6,(0.9,1.1),(2,40),maxi=8)
sound('impacts/impact_flesh.sound',flesh,0.8,(0.9,1.1),(2,40),maxi=6)
# ---------------- footsteps
def steps(name,srcs,db=-6,af='highpass=f=60',dur=0.45):
    out=[]
    for i,(k,f) in enumerate(srcs,1):
        x=trim(load(find(k,f),1,af),0.04,tail=0.2); write(f'player/{name}_{i}.wav',norm(cut(x,0.003,min(dur,len(x)/SR),fade=0.4),db)); out.append(f'{name}_{i}.wav')
    return out
def steps_split(name,k,f,start=1,db=-6,thr=0.4,gap=0.07,dur=0.12):
    x=load(find(k,f),1,'highpass=f=60'); out=[]
    for i,t in enumerate(onsets(x,thr,gap),start): write(f'player/{name}_{i}.wav',norm(cut(x,t,dur,fade=0.5),db)); out.append(f'{name}_{i}.wav')
    return out
st={}
st['concrete']=steps('step_concrete',[('fantozzi',f'Fantozzi-Stone{s}{n}.flac') for s in 'LR' for n in (1,2,3)])
st['dirt']=steps('step_dirt',[('fantozzi',f'Fantozzi-Sand{s}{n}.flac') for s in 'LR' for n in (1,2,3)])
st['gravel']=steps('step_gravel',[('surfaces',f'gravel/{n}.ogg') for n in range(6)])
st['grass']=steps('step_grass',[('surfaces',f'grass/{n}.ogg') for n in range(6)])
st['metal']=steps('step_metal',[('metalsteps',f'metal_steps_{n:02d}.wav') for n in (1,4,7,10,13,16)])
st['wood']=steps('step_wood',[('kdd','wood01.ogg'),('kdd','wood02.ogg'),('kdd','wood03.ogg'),('surfaces','wood/0.ogg'),('surfaces','wood/3.ogg'),('surfaces','wood/6.ogg')])
st['water']=steps('step_water',[('surfaces',f'water/{n}.ogg') for n in range(5)],dur=0.6)
for k,v in st.items(): sound(f'player/step_{k}.sound',v,0.35,(0.92,1.08),(1,25))
write('player/land.wav',norm(trim(load(find('thud3','thud3.wav'),1,'lowpass=f=3000')),-4))
sound('player/land.sound',['land.wav'],0.5,(0.9,1.1),(1,25),maxi=2)
# ---------------- voice
hurt=[]
for i,n in enumerate((1,3,5,7,9,11),1):
    write(f'player/hurt_{i}.wav',norm(trim(load(find('screams',f'slightscream-{n:02d}.flac'))),-2)); hurt.append(f'hurt_{i}.wav')
sound('player/hurt.sound',hurt,0.8,(0.95,1.05),(1,30),maxi=2)
death=[]
for i,n in enumerate(('die1.wav','die2.wav'),1):
    write(f'player/death_{i}.wav',norm(trim(load(find('baradari',n))),-2)); death.append(f'death_{i}.wav')
sound('player/death.sound',death,0.9,(0.97,1.03),(1,40),maxi=2)
# ---------------- ambience (stereo loops)
write_loop('ambient/wind_loop.ogg',load(find('windy','windy_ambiance.ogg'),2),60,start=10)
write_loop('ambient/wind_storm_loop.ogg',load(find('storm','storm_arwen_2022.flac'),2),60,start=20)
write_loop('ambient/zone_drone_loop.ogg',load(find('drone60','drone60.wav'),2,'lowpass=f=2500'),60)
write_loop('ambient/zone_drone_2_loop.ogg',load(find('drone70','drone70.wav'),2,'lowpass=f=2500'),60)
write_loop('ambient/zone_drone_3_loop.ogg',load(find('drone53','drone53.wav'),2,'lowpass=f=3000'),60)
write_loop('ambient/birds_loop.ogg',load(find('birds','park_ambience_birds.wav'),2),60,start=30,db=-6)
write_loop('ambient/rain_loop.ogg',load(find('rain','amb_rain_loop_1.wav'),2),60,start=5)
def amb(name,vol):
    sound(f'ambient/{name}.sound',[f'{name}_loop.ogg'],vol,(1,1),(1,60),loop=1,stream=1,spatial=0,group='ambient',maxi=2)
amb('wind',0.45); amb('wind_storm',0.5); amb('zone_drone',0.35); amb('zone_drone_2',0.35); amb('zone_drone_3',0.35); amb('birds',0.3); amb('rain',0.4)
