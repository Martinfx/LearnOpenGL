#!/bin/sh
# PDA news beep: two short square-ish tones (1320 Hz, then 1760 Hz). Output: ../ui/pda_beep.ogg (CC0, made for this project).
cd "$(dirname "$0")/../ui" || exit 1
ffmpeg -v error -y -f lavfi -i "aevalsrc='0.35*(if(lt(t,0.09),sin(2*PI*1320*t)+0.3*sin(6*PI*1320*t),0)+if(between(t,0.13,0.24),sin(2*PI*1760*t)+0.3*sin(6*PI*1760*t),0))':s=44100:d=0.3" \
    -af "lowpass=f=5000" -c:a libvorbis -q:a 4 pda_beep.ogg
