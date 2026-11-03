#!/usr/bin/env python3
"""Reload sounds of the 9 mm service pistol, timed to its view model clips (fp_service9.glb).

    python3 assets/sounds/source/build_service9_reload.py

Reuses the pistol handling recordings already cut by build_sounds.py (reload_pistol_empty.wav holds
mag out at 0.05 s, mag in at 0.75 s, slide pull at 1.35 s, slide release at 1.6 s; Still North Media,
CC0, see ../CREDITS.md) and lays them on the clip timelines:
  reload        2.33 s: mag out 0.33, mag in 1.22, slide pull 1.78, release 2.00
  reload_empty  1.75 s: mag out 0.33, mag in 1.20, slide release 1.30 (slide stop)
"""
import os,wave,numpy as np
D=os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)),'..','weapons'))
def read(n):
    w=wave.open(os.path.join(D,n)); sr=w.getframerate(); assert w.getnchannels()==1 and w.getsampwidth()==2
    return np.frombuffer(w.readframes(w.getnframes()),np.int16).astype(np.float32)/32768,sr
src,SR=read('reload_pistol_empty.wav')
def cut(t0,t1):
    x=src[int(t0*SR):int(t1*SR)].copy(); f=int(0.01*SR); x[-f:]*=np.linspace(1,0,f); return x
p_out,p_in,p_rack,p_rel=cut(0.04,0.70),cut(0.74,1.33),cut(1.34,1.59),cut(1.59,min(2.2,len(src)/SR))
def seq(parts,length):
    y=np.zeros(int(length*SR),np.float32)
    for x,t in parts:
        a=int(t*SR); n=min(len(x),len(y)-a); y[a:a+n]+=x[:n]
    return y
def write(n,y):
    y=np.clip(y,-1,1); w=wave.open(os.path.join(D,n),'wb'); w.setnchannels(1); w.setsampwidth(2); w.setframerate(SR)
    w.writeframes((y*32767).astype(np.int16).tobytes()); w.close(); print(n,round(len(y)/SR,2),'s')
write('reload_service9.wav',seq([(p_out,0.33-0.01),(p_in,1.22-0.01),(p_rack,1.78-0.01),(p_rel,2.0-0.01)],2.6))
write('reload_service9_empty.wav',seq([(p_out,0.33-0.01),(p_in,1.20-0.01),(p_rel,1.30-0.01)],1.95))
