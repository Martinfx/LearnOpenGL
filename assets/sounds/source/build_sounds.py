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
def write_loop(rel,x,length,xf=3.0,start=5.0,db=-3.0,lufs=-30.0):
    total=len(x)/SR; start=min(start,max(0,total*0.05)); length=min(length,total-start-xf-0.05)
    a=int(start*SR); L=int(length*SR); n=int(xf*SR); y=x[a:a+L+n].copy()
    w=np.linspace(0,1,n)[:,None]; head=y[:n]*w+y[L:L+n]*(1-w)   # tail fades into head -> seamless
    y=np.concatenate([head,y[n:L]]); y=norm(y,db)
    rms=np.sqrt((y**2).mean()); y=y*min(1.0,10**((lufs+3)/20)/max(rms,1e-9))  # ~LUFS target for a broadband bed
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
# ---------------- weapons: raw 192 kHz master tracks of The Free Firearm Sound Library (CC0), archive.org
RAWZIP='https://archive.org/download/the-free-firearm-sound-effects-library-by-still-north-media/Raw%20Master%20Tracks.zip'
RAW={'AK-47':'C_39 C_42 C_7 C_10 C_11 C_12 C_13','Bersa':'F_44 F_45 F_23','Walther PPQ':'X_38 X_40 X_4 X_5 X_21 X_18',
     'Carl Gustav M45:Swedish K':'G_26 G_32 G_8 G_9 G_10 G_3','1911':'A_35 A_36','Mosin Nagant':'M_20 M_22 M_14 M_10'}
def raw(i):
    d=os.path.join(CACHE,'raw'); os.makedirs(d,exist_ok=True); p=os.path.join(d,i+'.wav')
    if not os.path.exists(p):
        from remotezip import RemoteZip
        folder=[k for k,v in RAW.items() if i in v.split()][0]
        for attempt in range(8):
            try:
                with RemoteZip(RAWZIP,headers={'User-Agent':'Mozilla/5.0'}) as z: open(p,'wb').write(z.read(f'Master Tracks/{folder}/{i}.wav')); break
            except Exception: import time; time.sleep(min(16,2**attempt))
    return p
# first-person shot chain: mid of the behind-the-shooter stereo pair, weight + body, transient kept, limited
FP='highpass=f=35,equalizer=f=110:t=q:w=1:g=4,equalizer=f=3500:t=q:w=1.5:g=2,acompressor=threshold=-26dB:ratio=4:attack=6:release=180:makeup=6,alimiter=limit=0.89:attack=1:release=60'
def fp_shots(ids,name,dur,start=1,per=5,drive=5.0):
    out=[]; n=start
    for i in ids:
        x=load(raw(i),1,FP)
        for t in onsets(x,0.5,1.5)[:per]:
            y=norm(cut(x,t,dur,pre=0.002,fade=0.75),0.0); y=np.tanh(drive*y)/np.tanh(drive)  # soft-clip: dense, loud body like a real shot at the ear
            write(f'weapons/{name}_{n}.wav',norm(y,-2.0)); out.append(f'{name}_{n}.wav'); n+=1
    return out
ak=fp_shots(['C_39','C_42'],'rifle_shot',1.3,per=3,drive=6)
pist=fp_shots(['F_44','F_45','X_38'],'pistol_shot',0.95,per=2,drive=5)
smg=fp_shots(['G_26','G_32'],'smg_shot',0.75,per=3,drive=5)
heavy=fp_shots(['A_35','A_36'],'heavy_pistol_shot',1.1,per=2,drive=6)
sniper=fp_shots(['M_20','M_22'],'sniper_shot',2.0,per=2,drive=6)
akfar=shots(find('firearms','AK-47/C_31P.wav'),'rifle_shot_far',2,2.2,hp=80,db=-3)
sound('weapons/rifle_shot.sound',ak,1,(0.97,1.03),(4,300),maxi=8)
sound('weapons/rifle_shot_far.sound',akfar,0.8,(0.95,1.05),(60,900),maxi=6)
sound('weapons/pistol_shot.sound',pist,0.9,(0.97,1.03),(3,200))
sound('weapons/smg_shot.sound',smg,0.95,(0.97,1.03),(3,220),maxi=10)
sound('weapons/heavy_pistol_shot.sound',heavy,1,(0.97,1.03),(4,250))
sound('weapons/sniper_shot.sound',sniper,1,(0.98,1.02),(6,600))
# handling: real recordings of the same guns, cut and assembled into reload sequences
HD='highpass=f=60,acompressor=threshold=-24dB:ratio=3:attack=3:release=120:makeup=4'
def part(i,t=None,dur=0.6,k=0):
    x=load(raw(i),1,HD); ts=onsets(x,0.3,0.25); t=ts[k] if t is None else t
    return norm(cut(x,t,dur,pre=0.01,fade=0.5),-3)
def seq(parts):  # [(sound, start_time_s)]
    n=max(int(t*SR)+len(x) for x,t in parts); y=np.zeros(n,np.float32)
    for x,t in parts: a=int(t*SR); y[a:a+len(x)]+=x
    return norm(y,-2)
ak_out,ak_in,ak_pull,ak_rel=part('C_11',dur=0.5),part('C_10',dur=0.5),part('C_12',dur=0.6),part('C_13',dur=0.5)
write('weapons/mag_out.wav',ak_out); write('weapons/mag_in.wav',ak_in); write('weapons/bolt.wav',seq([(ak_pull,0),(ak_rel,0.45)]))
write('weapons/reload.wav',seq([(ak_out,0.1),(ak_in,1.0)]))
write('weapons/reload_empty.wav',seq([(ak_out,0.1),(ak_in,1.0),(ak_pull,1.7),(ak_rel,2.15)]))
p_out,p_in=part('X_5',dur=0.5),part('X_4',dur=0.5)
p_rack=part('X_21',dur=0.5); p_rel=part('X_21',dur=0.5,k=1)
write('weapons/reload_pistol.wav',seq([(p_out,0.05),(p_in,0.75)]))
write('weapons/reload_pistol_empty.wav',seq([(p_out,0.05),(p_in,0.75),(p_rack,1.35),(p_rel,1.6)]))
s_out,s_in,s_bolt=part('G_9',dur=0.5),part('G_8',dur=0.5),part('G_10',dur=1.2)
write('weapons/reload_smg.wav',seq([(s_out,0.05),(s_in,0.9)]))
write('weapons/reload_smg_empty.wav',seq([(s_out,0.05),(s_in,0.9),(s_bolt,1.5)]))
write('weapons/bolt_sniper.wav',norm(cut(load(raw('M_14'),1,HD),onsets(load(raw('M_14'),1,HD),0.3,0.25)[0],1.4,pre=0.01,fade=0.3),-3))
write('weapons/dry_fire.wav',part('C_7',dur=0.25))
write('weapons/dry_fire_pistol.wav',part('X_18',dur=0.25))
write('weapons/draw.wav',seq([(part('G_3',dur=0.3),0.0),(part('C_13',dur=0.4)*0.5,0.12)]))
for n,f,v in [('reload',['reload.wav'],0.85),('reload_empty',['reload_empty.wav'],0.85),('reload_pistol',['reload_pistol.wav'],0.85),
              ('reload_pistol_empty',['reload_pistol_empty.wav'],0.85),('reload_smg',['reload_smg.wav'],0.85),('reload_smg_empty',['reload_smg_empty.wav'],0.85),
              ('mag_in',['mag_in.wav'],0.7),('mag_out',['mag_out.wav'],0.7),('bolt',['bolt.wav'],0.75),('bolt_sniper',['bolt_sniper.wav'],0.75),
              ('dry_fire',['dry_fire.wav'],0.6),('dry_fire_pistol',['dry_fire_pistol.wav'],0.6),('draw',['draw.wav'],0.6)]:
    sound(f'weapons/{n}.sound',f,v,(0.97,1.03),(1,25),maxi=2)
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
write_loop('ambient/wind_loop.ogg',load(find('windy','windy_ambiance.ogg'),2,'highpass=f=80'),60,start=10,lufs=-32)
write_loop('ambient/wind_storm_loop.ogg',load(find('storm','storm_arwen_2022.flac'),2,'highpass=f=60'),60,start=20,lufs=-28)
write_loop('ambient/zone_drone_loop.ogg',load(find('drone60','drone60.wav'),2,'highpass=f=70,lowpass=f=2500'),60,lufs=-38)
write_loop('ambient/zone_drone_2_loop.ogg',load(find('drone70','drone70.wav'),2,'highpass=f=70,lowpass=f=2500'),60,lufs=-38)
write_loop('ambient/zone_drone_3_loop.ogg',load(find('drone53','drone53.wav'),2,'highpass=f=70,lowpass=f=3000'),60,lufs=-38)
write_loop('ambient/birds_loop.ogg',load(find('birds','park_ambience_birds.wav'),2,'highpass=f=150'),60,start=30,lufs=-37)
write_loop('ambient/rain_loop.ogg',load(find('rain','amb_rain_loop_1.wav'),2),60,start=5,lufs=-33)
def amb(name,vol):
    sound(f'ambient/{name}.sound',[f'{name}_loop.ogg'],vol,(1,1),(1,60),loop=1,stream=1,spatial=0,group='ambient',maxi=2)
amb('wind',1.0); amb('wind_storm',1.0); amb('zone_drone',0.8); amb('zone_drone_2',0.8); amb('zone_drone_3',0.8); amb('birds',0.8); amb('rain',0.9)
