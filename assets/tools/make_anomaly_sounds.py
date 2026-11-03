#!/usr/bin/env python3
"""Synthesizes the anomaly, detector and Geiger counter sounds (original, CC0).

Writes assets/sounds/anomalies/*.wav|ogg and the .sound files that play them.
Placeholders until real recordings arrive; each sound is built from noise and
oscillators with the helpers of make_sounds.py. Needs numpy and ffmpeg.

    python3 assets/tools/make_anomaly_sounds.py
"""
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import make_sounds as ms  # noqa: E402
from make_sounds import (RATE, at, bandpass, env, highpass, lowpass, noise, pad, seamless, sweep,  # noqa: E402
                         t_axis, write_ogg, write_wav)

rng = np.random.default_rng(4242)
ms.rng = rng
DIR = "anomalies"


def sound_file(name, files, volume=1.0, pitch=(1.0, 1.0), distance=(2.0, 40.0), loop=False, spatial=True,
               group="sfx", max_instances=4, comment=""):
    lines = ["# " + (comment or "Sound: variations (one is picked at random per play) and playback settings")]
    lines += ['file "%s"' % f for f in files]
    lines += ["volume %g" % volume, "pitch %g %g" % pitch, "distance %g %g" % distance, "rolloff 1",
              "loop %d" % loop, "stream %d" % (1 if loop and files[0].endswith(".ogg") else 0),
              "spatial %d" % spatial, "group " + group, "max_instances %d" % max_instances]
    path = os.path.join(ms.ROOT, DIR, name + ".sound")
    with open(path, "w") as f:
        f.write("\n".join(lines) + "\n")
    print("wrote", os.path.relpath(path, ms.ROOT))


def crackle(seconds, density, cut=4000):
    x = np.zeros(int(seconds * RATE))
    time = 0.0
    while time < seconds:
        time += rng.exponential(1.0 / density)
        n = rng.uniform(0.0015, 0.006)
        at(x, highpass(noise(n), cut) * env(n, 0.0001, n * 0.3), time, rng.uniform(0.2, 1.0))
    return x


# ------------------------------------------------------------ crusher (gravity)

def make_crusher():
    n = 12.0
    t = t_axis(n + 2.0)
    pulse = 0.55 + 0.45 * np.sin(2 * np.pi * t * 0.5) ** 2
    hum = sum(np.sin(2 * np.pi * f * t + 0.8 * np.sin(2 * np.pi * t * m)) * g
              for f, m, g in ((31.0, 0.5, 1.0), (46.5, 0.25, 0.6), (62.0, 0.33, 0.3)))
    air = bandpass(noise(n + 2.0), 120 + 90 * pulse, 3) * 0.6
    creaks = np.zeros_like(t)
    for _ in range(9):
        c = 0.4
        creak = bandpass(noise(c), np.linspace(300, 180, int(c * RATE)), 12) * env(c, 0.05, 0.15)
        at(creaks, creak, rng.uniform(0.0, n), 0.5)
    write_ogg(DIR + "/crusher_loop.ogg", seamless((hum * pulse + air + creaks) * 0.8, 2.0))
    sound_file("crusher_loop", ["crusher_loop.ogg"], 0.8, distance=(3, 35), loop=True, group="ambient", max_instances=8,
               comment="Gravity trap idle: pulsing sub hum and creaking")

    c = 0.9
    groan = (sweep(c, 40, 120, 2.5) * 0.8 + bandpass(noise(c), np.linspace(150, 600, int(c * RATE)), 5)) * env(c, 0.4, 0.5)
    write_wav(DIR + "/crusher_charge.wav", groan)
    sound_file("crusher_charge", ["crusher_charge.wav"], 0.9, distance=(3, 45), comment="Gravity trap squeezing")

    for i in range(2):
        b = 1.6
        thump = sweep(b, 120, 35, 9) * env(b, 0.002, 0.25) * 1.4
        crunch = lowpass(noise(b), np.linspace(5000, 300, int(b * RATE))) * env(b, 0.001, 0.09) * 1.2
        x = thump + crunch
        for k in range(10):
            at(x, highpass(noise(0.03), 1200) * env(0.03, 0.0005, 0.01), rng.uniform(0.0, 0.18), rng.uniform(0.3, 0.8))
        whoosh = bandpass(noise(b), np.linspace(1500, 200, int(b * RATE)), 2) * env(b, 0.05, 0.4) * 0.6
        write_wav(DIR + "/crusher_burst_%d.wav" % (i + 1), x + whoosh)
    sound_file("crusher_burst", ["crusher_burst_1.wav", "crusher_burst_2.wav"], 1.0, (0.92, 1.05), (4, 90),
               comment="Gravity trap crush and fling")


# ------------------------------------------------------------ discharge (electric)

def zap(seconds, f0, f1):
    """Electric arc: a buzzy, wavering tone (clipped saw) inside sizzling noise."""
    t = t_axis(seconds)
    freq = np.linspace(f0, f1, len(t)) * (1.0 + 0.15 * rng.standard_normal(len(t)).cumsum() / np.sqrt(len(t)))
    saw = 2.0 * ((np.cumsum(freq) / RATE) % 1.0) - 1.0
    tone = np.tanh(saw * 3.0) * 0.5
    sizzle = highpass(noise(seconds), 3000) * (0.4 + 0.6 * (rng.random(len(t)) < 0.02).astype(float))
    return tone + sizzle * 0.6


def thunder_crack(seconds):
    """Close lightning: a sharp N-wave snap, a ripping tear and a rolling low tail."""
    t = t_axis(seconds)
    n = len(t)
    snap = np.zeros(n)
    w = int(0.004 * RATE)
    snap[:w] = np.linspace(1.0, -1.0, w)
    snap = lowpass(snap, 9000, 1) * 2.5
    tear = np.zeros(n)
    time = 0.0
    while time < 0.25:  # a burst of sub-strikes ripping along the channel
        time += rng.exponential(0.012)
        at(tear, highpass(noise(0.01), 1500) * env(0.01, 0.0002, 0.003), time, rng.uniform(0.4, 1.0) * np.exp(-time / 0.12))
    tear = lowpass(tear, 8000, 1)
    roll = lowpass(noise(seconds), 220) * (np.minimum(t / 0.05, 1.0) * np.exp(-t / 0.7)) * 3.0
    roll *= 0.7 + 0.3 * np.abs(np.sin(2 * np.pi * t * 3.1) * np.sin(2 * np.pi * t * 1.7))
    hum = zap(seconds, 140, 90) * env(seconds, 0.001, 0.25) * 0.35
    return snap + tear * 1.4 + roll + hum


def make_discharge():
    n = 8.0
    t = t_axis(n + 1.0)
    # Transformer-like hum that swells and wavers, a high whine, and crackle in bursts.
    swell = 0.65 + 0.35 * np.sin(2 * np.pi * t * 0.37) ** 2
    buzz = sum(np.sin(2 * np.pi * 50 * k * t + 0.3 * np.sin(2 * np.pi * t * 0.2 * k)) / k for k in (1, 2, 3, 4, 5, 7, 9)) * 0.22 * swell
    whine = np.sin(2 * np.pi * (2300 + 40 * np.sin(2 * np.pi * t * 0.13)) * t) * 0.012
    bursts = np.clip(np.sin(2 * np.pi * t * 0.23) * np.sin(2 * np.pi * t * 0.61), 0.0, 1.0)
    crk = crackle(n + 1.0, 8) * 0.25 + crackle(n + 1.0, 70) * 0.35 * bursts
    write_ogg(DIR + "/discharge_loop.ogg", seamless(buzz + whine + crk, 1.0))
    sound_file("discharge_loop", ["discharge_loop.ogg"], 0.5, distance=(2, 24), loop=True, group="ambient", max_instances=8,
               comment="Electric field idle: wavering transformer hum, whine and bursts of crackle")

    for i in range(3):
        c = 0.4
        x = crackle(c, 160) * env(c, 0.01, 0.12) + zap(c, rng.uniform(300, 600), rng.uniform(120, 220)) * env(c, 0.005, 0.08) * 0.5
        write_wav(DIR + "/discharge_crackle_%d.wav" % (i + 1), x)
    sound_file("discharge_crackle", ["discharge_crackle_%d.wav" % (i + 1) for i in range(3)], 0.6, (0.9, 1.15), (2, 30),
               comment="Electric field arming: crackle and a short buzzing arc")

    for i in range(4):
        a = 0.18
        x = zap(a, rng.uniform(500, 1200), rng.uniform(200, 400)) * env(a, 0.002, 0.04)
        write_wav(DIR + "/discharge_arc_%d.wav" % (i + 1), x)
    sound_file("discharge_arc", ["discharge_arc_%d.wav" % (i + 1) for i in range(4)], 0.35, (0.85, 1.2), (1.5, 18), max_instances=3,
               comment="Small idle arc crawling over the electric field")

    for i in range(3):
        b = 2.2
        write_wav(DIR + "/discharge_burst_%d.wav" % (i + 1), thunder_crack(b) + crackle(b, 300) * env(b, 0.0, 0.25) * 0.6)
    sound_file("discharge_burst", ["discharge_burst_%d.wav" % (i + 1) for i in range(3)], 1.0, (0.9, 1.1), (4, 140),
               comment="Lightning strike of the electric field: snap, tear and rolling thunder")


# ------------------------------------------------------------ vent (heat / fire)

def jet(seconds, strength=1.0):
    """Burning gas jet: turbulent broadband roar with a fluttering low body."""
    t = t_axis(seconds)
    flutter = 0.75 + 0.25 * lowpass(noise(seconds), 9, 2) * 6.0
    body = lowpass(noise(seconds), 350) * 2.2 * flutter
    roar = bandpass(noise(seconds), 700 + 300 * flutter, 0.8) * 0.9
    hiss = highpass(noise(seconds), 2500) * 0.18
    pops = crackle(seconds, 25, 1800) * 0.6
    return (body + roar + hiss + pops) * strength


def make_vent():
    n = 10.0
    t = t_axis(n + 2.0)
    flicker = 0.6 + 0.4 * np.abs(np.sin(2 * np.pi * t * 0.7) * np.sin(2 * np.pi * t * 1.3))
    hiss = bandpass(noise(n + 2.0), 900 + 400 * flicker, 1.2) * 0.22
    rumble = lowpass(noise(n + 2.0), 120) * 1.8 * flicker
    breath = lowpass(noise(n + 2.0), 60, 2) * 3.0 * (0.5 + 0.5 * np.sin(2 * np.pi * t * 0.25) ** 2)
    embers = crackle(n + 2.0, 6, 2500) * 0.35
    write_ogg(DIR + "/vent_loop.ogg", seamless(hiss + rumble + breath + embers, 2.0))
    sound_file("vent_loop", ["vent_loop.ogg"], 0.6, distance=(2, 26), loop=True, group="ambient", max_instances=8,
               comment="Heat vent idle: breathing rumble, hot air hiss and crackling embers")

    c = 0.6
    t = t_axis(c)
    suck = lowpass(noise(c), np.linspace(80, 900, len(t))) * env(c, 0.45, 0.2) + bandpass(noise(c), np.linspace(300, 1500, len(t)), 3) * (t / c) ** 2 * 0.5
    write_wav(DIR + "/vent_charge.wav", suck)
    sound_file("vent_charge", ["vent_charge.wav"], 0.9, distance=(3, 50), comment="Heat vent drawing breath before it erupts")

    for i in range(2):
        b = 2.6
        t = t_axis(b)
        shape = np.minimum(t / 0.06, 1.0) * np.clip((b - t) / 0.7, 0.0, 1.0)
        whomp = sweep(b, 110, 38, 7) * env(b, 0.004, 0.3) * 1.6 + lowpass(noise(b), 500) * env(b, 0.002, 0.12) * 2.0
        write_wav(DIR + "/vent_burst_%d.wav" % (i + 1), jet(b) * shape + whomp)
    sound_file("vent_burst", ["vent_burst_1.wav", "vent_burst_2.wav"], 1.0, (0.95, 1.05), (4, 100),
               comment="Heat vent eruption: ignition whomp and the roar of the fire column")


# ------------------------------------------------------------ acid (chemical)

def bubble(seconds=0.06):
    f0 = rng.uniform(250, 900)
    return np.sin(2 * np.pi * np.cumsum(np.linspace(f0, f0 * 2.2, int(seconds * RATE))) / RATE) * env(seconds, 0.002, seconds * 0.3)


def make_acid():
    n = 9.0
    t = t_axis(n + 1.0)
    x = lowpass(noise(n + 1.0), 300) * 0.4 + bandpass(noise(n + 1.0), 3500, 1.5) * 0.08
    fizz = highpass(noise(n + 1.0), 5000) * 0.05 * (0.6 + 0.4 * np.sin(2 * np.pi * t * 0.3) ** 2)
    x += fizz
    time = 0.0
    while time < n + 1.0:  # bubbles come in clusters
        time += rng.exponential(1.0 / 4.0)
        for _ in range(int(rng.integers(1, 5))):
            at(x, bubble(rng.uniform(0.03, 0.09)), time + rng.uniform(0.0, 0.25), rng.uniform(0.15, 0.6))
        if rng.random() < 0.15:  # a big slow glop
            g = 0.25
            glop = np.sin(2 * np.pi * np.cumsum(np.linspace(90, 210, int(g * RATE))) / RATE) * env(g, 0.01, 0.06)
            at(x, glop, time, 0.8)
    write_ogg(DIR + "/acid_loop.ogg", seamless(x, 1.0))
    sound_file("acid_loop", ["acid_loop.ogg"], 0.5, distance=(2, 20), loop=True, group="ambient", max_instances=8,
               comment="Acid pool idle: bubbling sludge")

    for i in range(2):
        b = 1.2
        sizzle = highpass(noise(b), 2500) * env(b, 0.01, 0.35) * 0.7
        for _ in range(12):
            at(sizzle, bubble(0.05), rng.uniform(0.0, 0.6), rng.uniform(0.3, 0.7))
        write_wav(DIR + "/acid_burst_%d.wav" % (i + 1), sizzle)
    sound_file("acid_burst", ["acid_burst_1.wav", "acid_burst_2.wav"], 0.8, (0.9, 1.1), (2, 35), comment="Acid hiss and splash")


# ------------------------------------------------------------ rift (spatial)

def make_rift():
    n = 10.0
    t = t_axis(n + 2.0)
    phase = sum(np.sin(2 * np.pi * f * t + 3 * np.sin(2 * np.pi * t * m)) * g
                for f, m, g in ((220.0, 0.13, 0.4), (331.0, 0.07, 0.3), (443.5, 0.11, 0.2), (110.0, 0.05, 0.5)))
    phase *= 0.5 + 0.5 * np.sin(2 * np.pi * t * 0.2) ** 2
    shimmer = bandpass(noise(n + 2.0), 4000 + 1500 * np.sin(2 * np.pi * t * 0.3), 8) * 0.15
    write_ogg(DIR + "/rift_loop.ogg", seamless(phase * 0.5 + shimmer, 2.0))
    sound_file("rift_loop", ["rift_loop.ogg"], 0.45, distance=(2, 25), loop=True, group="ambient", max_instances=8,
               comment="Spatial rift idle: phasing whine")

    b = 1.0
    t = t_axis(b)
    suck = bandpass(noise(b), np.linspace(300, 3000, int(b * RATE)), 3) * np.minimum(t / 0.45, 1.0) * (t < 0.5)
    pop = sweep(b, 200, 40, 20) * env(b, 0.0, 0.08)
    at(pop, highpass(noise(0.02), 2000) * env(0.02, 0.0, 0.006), 0.0, 1.0)
    x = suck * 0.8 + np.concatenate([np.zeros(int(0.5 * RATE)), pop])[: len(t)] * 1.3
    tail = bandpass(noise(b), 900, 6) * env(b, 0.5, 0.25) * 0.3
    write_wav(DIR + "/rift_burst.wav", x + tail)
    sound_file("rift_burst", ["rift_burst.wav"], 1.0, (0.9, 1.1), (3, 70), comment="Spatial rift throw: in-rush and pop")


# ------------------------------------------------------------ player gear

def make_gear():
    for i in range(4):
        n = 0.006
        click = highpass(noise(n), 1800) * env(n, 0.0001, rng.uniform(0.0006, 0.0012))
        write_wav(DIR + "/geiger_click_%d.wav" % (i + 1), pad(click, 0.01), 0.8)
    sound_file("geiger_click", ["geiger_click_%d.wav" % (i + 1) for i in range(4)], 0.7, (0.9, 1.15), spatial=False,
               group="ui", max_instances=12, comment="Geiger counter: one click (the game plays them at the dose rate)")

    b = 0.09
    t = t_axis(b)
    beep = np.sign(np.sin(2 * np.pi * 2350 * t)) * 0.3 + np.sin(2 * np.pi * 2350 * t) * 0.7
    beep = lowpass(beep, 6000, 1) * np.minimum(t / 0.003, 1.0) * np.clip((b - t) / 0.01, 0, 1)
    write_wav(DIR + "/detector_beep.wav", beep, 0.6)
    sound_file("detector_beep", ["detector_beep.wav"], 0.7, spatial=False, group="ui", max_instances=3,
               comment="Anomaly detector beep (faster when closer)")

    b = 0.35
    write_wav(DIR + "/bolt_throw.wav", bandpass(noise(b), np.linspace(1800, 700, int(b * RATE)), 2) * env(b, 0.04, 0.1), 0.5)
    sound_file("bolt_throw", ["bolt_throw.wav"], 0.5, (0.9, 1.1), (1, 15), comment="Throwing a bolt")
    for i in range(3):
        write_wav(DIR + "/bolt_hit_%d.wav" % (i + 1), ms.metal_click(rng.uniform(2400, 3600), 0.12, 0.03))
    sound_file("bolt_hit", ["bolt_hit_%d.wav" % (i + 1) for i in range(3)], 0.6, (0.9, 1.1), (1, 25), max_instances=6,
               comment="Bolt landing on something hard")


if __name__ == "__main__":
    os.makedirs(os.path.join(ms.ROOT, DIR), exist_ok=True)
    groups = {"crusher": make_crusher, "discharge": make_discharge, "vent": make_vent, "acid": make_acid,
              "rift": make_rift, "gear": make_gear}
    for name in sys.argv[1:] or list(groups):
        groups[name]()
