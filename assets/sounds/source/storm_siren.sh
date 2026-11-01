#!/bin/sh
# Anomaly storm warning siren: a slow wailing tone (3 harmonics, pitch sweeping 340..500 Hz every
# 4 s) with a long echo over the Zone. Output: ../ambient/storm_siren.ogg (CC0, made for this project).
cd "$(dirname "$0")/../ambient" || exit 1
ffmpeg -v error -y -f lavfi -i "aevalsrc='(0.55*sin(2*PI*(420*t-127.32*cos(2*PI*t/4)))+0.25*sin(4*PI*(420*t-127.32*cos(2*PI*t/4)))+0.12*sin(6*PI*(420*t-127.32*cos(2*PI*t/4))))*0.6':s=44100:d=14" \
    -af "afade=t=in:d=1.5,afade=t=out:st=10:d=4,aecho=0.8:0.6:400|900:0.35|0.2" -c:a libvorbis -q:a 4 storm_siren.ogg
