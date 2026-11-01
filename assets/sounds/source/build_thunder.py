#!/usr/bin/env python3
"""Real thunder (close strikes and distant rumbles) and a rain-on-roof loop, cut from field recordings.

    python3 -I assets/sounds/source/build_thunder.py [cache_dir]

thunder_near_* : close strikes with the crack (Gregor Quendel "Thunder / Lightning Ambience", CC-BY 4.0;
                 InspectorJ "Thunder, Very Close, Rain, 01", CC-BY 3.0)
thunder_far_*  : rumbles without a crack from the Quendel recording, low-passed, soft onset
rain_roof_loop : Ogrebane "Rain in the Gutter Loop" (CC0), rain on a roof and into a gutter
Thunder is mono Ogg Vorbis (the game sets the level by distance); the loop is stereo. Credits: ../CREDITS.md.
"""
import os,sys,subprocess,urllib.request,urllib.parse
import numpy as np
CACHE=os.path.join(sys.argv[1] if len(sys.argv)>1 else '/tmp/sound_cache','thunder')
OUT=os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)),'..','ambient'))
OGA='https://opengameart.org/sites/default/files/'
SRC={'quendel':'gregor_quendel_-_free_thunder_ambience_-_mp3.mp3','inspectorj':'Thunder, Very Close, Rain, 01.wav','gutter':'rain-gutter-loop_0.mp3'}
def path(k):
    p=os.path.join(CACHE,SRC[k])
    if not os.path.exists(p):
        os.makedirs(CACHE,exist_ok=True)
        open(p,'wb').write(urllib.request.urlopen(urllib.request.Request(OGA+urllib.parse.quote(SRC[k]),headers={'User-Agent':'Mozilla/5.0'})).read())
    return p
def lufs(f):
    o=subprocess.run(['ffmpeg','-nostats','-i',f,'-af','ebur128','-f','null','-'],capture_output=True).stderr.decode()
    return float(o.split('Summary:')[1].split('I:')[1].split('LUFS')[0])
def cut(k,start,dur,name,af,target,fade_in=0.01,ch=1):
    tmp=os.path.join('/tmp',name+'.wav'); out=os.path.join(OUT,name+'.ogg')
    chain=f'atrim=start={start}:duration={dur},asetpts=PTS-STARTPTS,{af},afade=t=in:d={fade_in},afade=t=out:st={dur-1.5}:d=1.5'
    subprocess.run(['ffmpeg','-v','error','-y','-i',path(k),'-af',chain,'-ac',str(ch),'-ar','48000',tmp],check=True)
    g=target-lufs(tmp)
    subprocess.run(['ffmpeg','-v','error','-y','-i',tmp,'-af',f'volume={g}dB,alimiter=limit=0.89:attack=2:release=80','-c:a','libvorbis','-q:a','5',out],check=True)
    os.remove(tmp); print('wrote',name,dur,'s',round(g,1),'dB')
NEAR='highpass=f=25'
FAR='highpass=f=25,lowpass=f=900'
# (source, start s, duration s) picked by an onset scan: low-band jump with a 2 kHz+ crack = close strike
cut('quendel',14.3,8.0,'thunder_near_1',NEAR,-15)   # starts on the crack: the game adds the distance delay
cut('quendel',173.3,7.0,'thunder_near_2',NEAR,-15)
cut('quendel',197.2,8.0,'thunder_near_3',NEAR,-15)
cut('inspectorj',0.7,7.0,'thunder_near_4',NEAR,-15)
# rumbles without a crack
cut('quendel',41.5,8.0,'thunder_far_1',FAR,-22,fade_in=0.6)
cut('quendel',73.0,8.0,'thunder_far_2',FAR,-22,fade_in=0.6)
cut('quendel',99.5,9.0,'thunder_far_3',FAR,-22,fade_in=0.6)
cut('quendel',151.5,6.0,'thunder_far_4',FAR,-22,fade_in=0.6)
cut('quendel',345.0,6.0,'thunder_far_5',FAR,-22,fade_in=0.6)
# rain on a roof: the source is already a loop; keep it whole, stereo
src=path('gutter'); out=os.path.join(OUT,'rain_roof_loop.ogg')
subprocess.run(['ffmpeg','-v','error','-y','-i',src,'-ar','48000','-c:a','libvorbis','-q:a','4',out],check=True)
g=-30-lufs(out); subprocess.run(['ffmpeg','-v','error','-y','-i',src,'-af',f'volume={g}dB','-ar','48000','-c:a','libvorbis','-q:a','4',out],check=True)
print('wrote rain_roof_loop')
