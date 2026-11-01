#!/bin/sh
# Artifact detector ping: a soft glassy two-tone chime (1320 Hz then 1980 Hz with a 2640 Hz shimmer),
# quick decay and a short echo. Output: ../anomalies/artifact_ping.ogg (CC0, made for this project).
cd "$(dirname "$0")/../anomalies" || exit 1
ffmpeg -v error -y -f lavfi -i "aevalsrc='0.5*exp(-9*t)*sin(2*PI*1320*t)+0.4*exp(-7*max(t-0.07,0))*gte(t,0.07)*(sin(2*PI*1980*t)+0.3*sin(2*PI*2640*t))':s=44100:d=0.6" \
    -af "aecho=0.7:0.5:60:0.3,afade=t=out:st=0.4:d=0.2" -c:a libvorbis -q:a 4 artifact_ping.ogg
