#!/usr/bin/env python3
"""Synthesizes the placeholder sound effects in assets/sounds/ (original, CC0).

Everything is generated from noise and oscillators, so the sounds are ours and
can be regenerated or tweaked here. Short effects are 16-bit mono WAV (decoded
into memory); long loops are Ogg Vorbis (streamed). Needs numpy, and ffmpeg for
the .ogg files.

    python3 assets/tools/make_sounds.py
"""
import os
import subprocess
import sys
import tempfile
import wave

import numpy as np

RATE = 44100
ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "sounds")
rng = np.random.default_rng(1986)


def t_axis(seconds):
    return np.arange(int(seconds * RATE)) / RATE


def noise(seconds):
    return rng.uniform(-1.0, 1.0, int(seconds * RATE))


def onepole(x, cutoff):
    cutoff = np.broadcast_to(np.asarray(cutoff, dtype=float), x.shape)
    a = 1.0 - np.exp(-2.0 * np.pi * cutoff / RATE)
    y = np.empty_like(x)
    acc = 0.0
    for i in range(len(x)):
        acc += a[i] * (x[i] - acc)
        y[i] = acc
    return y


def lowpass(x, cutoff, order=3):
    """Cascaded one-pole low-pass (6 dB/octave per order); `cutoff` may be per sample."""
    for _ in range(order):
        x = onepole(x, cutoff)
    return x


def highpass(x, cutoff):
    return x - onepole(x, cutoff)


def bandpass(x, center, q=2.0):
    """State-variable band-pass; `center` may be a per-sample array."""
    center = np.broadcast_to(np.asarray(center, dtype=float), x.shape)
    f = 2.0 * np.sin(np.pi * np.minimum(center, RATE / 6.0) / RATE)
    damp = 1.0 / q
    low = band = 0.0
    y = np.empty_like(x)
    for i in range(len(x)):
        high = x[i] - low - damp * band
        band += f[i] * high
        low += f[i] * band
        y[i] = band
    return y


def env(seconds, attack, decay):
    t = t_axis(seconds)
    a = np.minimum(t / max(attack, 1e-4), 1.0)
    return a * np.exp(-np.maximum(t - attack, 0.0) / decay)


def sweep(seconds, f0, f1, curve=8.0):
    t = t_axis(seconds)
    freq = f1 + (f0 - f1) * np.exp(-t * curve)
    return np.sin(2.0 * np.pi * np.cumsum(freq) / RATE)


def pad(x, seconds):
    out = np.zeros(int(seconds * RATE))
    out[: min(len(x), len(out))] += x[: len(out)]
    return out


def at(dst, src, seconds, gain=1.0):
    i = int(seconds * RATE)
    n = min(len(src), len(dst) - i)
    if n > 0:
        dst[i : i + n] += src[:n] * gain


def normalize(x, peak=0.89):
    m = np.max(np.abs(x))
    return x * (peak / m) if m > 0 else x


def fade_out(x, seconds=0.02):
    n = min(len(x), int(seconds * RATE))
    x = x.copy()
    x[-n:] *= np.linspace(1.0, 0.0, n)
    return x


def write_wav(rel, x, peak=0.89):
    path = os.path.join(ROOT, rel)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    data = (np.clip(normalize(fade_out(x), peak), -1, 1) * 32767).astype("<i2")
    with wave.open(path, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(RATE)
        w.writeframes(data.tobytes())
    print("wrote", rel)


def write_ogg(rel, x, peak=0.8, quality=4):
    path = os.path.join(ROOT, rel)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with tempfile.TemporaryDirectory() as tmp:
        wav = os.path.join(tmp, "loop.wav")
        data = (np.clip(normalize(x, peak), -1, 1) * 32767).astype("<i2")
        with wave.open(wav, "wb") as w:
            w.setnchannels(1)
            w.setsampwidth(2)
            w.setframerate(RATE)
            w.writeframes(data.tobytes())
        subprocess.run(["ffmpeg", "-y", "-loglevel", "error", "-i", wav, "-c:a", "libvorbis", "-q:a", str(quality),
                        "-map_metadata", "-1", "-fflags", "+bitexact", path], check=True)
    print("wrote", rel)


def seamless(x, overlap):
    """Crossfades the end into the start so the sound loops without a click."""
    n = int(overlap * RATE)
    head, body, tail = x[:n], x[n:-n], x[-n:]
    ramp = np.linspace(0.0, 1.0, n)
    return np.concatenate([tail * (1.0 - ramp) + head * ramp, body])


def metal_click(freq, length=0.06, ring=0.015):
    t = t_axis(length)
    tone = sum(np.sin(2 * np.pi * freq * k * t + rng.uniform(0, 6.28)) / k for k in (1.0, 2.37, 3.91))
    return (tone * 0.6 + lowpass(noise(length), 6000, 1) * 0.5) * env(length, 0.0005, ring)


# ---------------------------------------------------------------- weapons

def gunshot(length, crack_cut, body_hz, tail, weight):
    crack = lowpass(highpass(noise(length), 1200), 7000, 1) * env(length, 0.0003, 0.012 * weight)
    blast = lowpass(noise(length), np.linspace(crack_cut, 300, int(length * RATE))) * env(length, 0.001, 0.05 * weight)
    thump = sweep(length, body_hz * 2.5, body_hz, 30) * env(length, 0.001, 0.07 * weight)
    echo = lowpass(noise(length), 700) * env(length, 0.02, tail)
    x = crack * 0.9 + blast * 1.2 + thump * 0.9 + echo * 0.35
    # a late slap from a far wall
    at(x, lowpass(noise(0.4), 900) * env(0.4, 0.01, 0.12), 0.18 + rng.uniform(0, 0.08), 0.25)
    return x


def make_weapons():
    for i in range(3):
        write_wav(f"weapons/rifle_shot_{i + 1}.wav", gunshot(1.2, 5200 + 600 * i, 55 + 4 * i, 0.32, 1.25))
        write_wav(f"weapons/pistol_shot_{i + 1}.wav", gunshot(0.8, 7000 + 500 * i, 85 + 6 * i, 0.2, 0.8))
        write_wav(f"weapons/smg_shot_{i + 1}.wav", gunshot(0.7, 6200 + 400 * i, 70 + 5 * i, 0.16, 0.85))

    reload = np.zeros(int(2.2 * RATE))
    at(reload, metal_click(1900, 0.08, 0.02), 0.15, 0.7)                      # magazine release
    at(reload, lowpass(noise(0.25), 1500) * env(0.25, 0.03, 0.06), 0.25, 0.4)  # magazine slides out
    at(reload, metal_click(1300, 0.1, 0.03), 0.95, 1.0)                       # magazine seats
    at(reload, metal_click(2600, 0.05, 0.01), 1.0, 0.6)
    at(reload, metal_click(1600, 0.12, 0.03), 1.45, 0.9)                      # bolt back
    at(reload, metal_click(2100, 0.1, 0.025), 1.6, 1.0)                       # bolt forward
    write_wav("weapons/reload.wav", reload)

    write_wav("weapons/dry_fire.wav", pad(metal_click(3100, 0.06, 0.008), 0.15))

    draw = np.zeros(int(0.5 * RATE))
    at(draw, bandpass(noise(0.3), 900, 0.8) * env(0.3, 0.08, 0.06), 0.0, 0.5)  # cloth
    at(draw, metal_click(2400, 0.06, 0.012), 0.28, 0.8)
    write_wav("weapons/draw.wav", draw)

    for i in range(2):
        n = 0.38
        t = t_axis(n)
        center = 350 + 1300 * np.sin(np.pi * np.clip(t / n, 0, 1)) ** 2 * (1.0 + 0.2 * i)
        whoosh = bandpass(noise(n), center, 1.5) * np.sin(np.pi * np.clip(t / n, 0, 1)) ** 3
        write_wav(f"weapons/swing_{i + 1}.wav", whoosh, 0.7)


# ---------------------------------------------------------------- impacts

def make_impacts():
    for i in range(3):
        n = 0.5
        x = highpass(noise(n), 900) * env(n, 0.0004, 0.01) * 1.2
        x += bandpass(noise(n), 1700 + 400 * i, 3) * env(n, 0.001, 0.04)
        for _ in range(6):  # debris
            at(x, metal_click(rng.uniform(2500, 5000), 0.02, 0.004), rng.uniform(0.03, 0.3), rng.uniform(0.05, 0.25))
        write_wav(f"impacts/impact_hard_{i + 1}.wav", x, 0.75)
    for i in range(2):
        n = 0.3
        x = lowpass(noise(n), 900) * env(n, 0.002, 0.04) + sweep(n, 220, 90 + 15 * i, 25) * env(n, 0.002, 0.05) * 0.8
        x += bandpass(noise(n), 400, 1.2) * env(n, 0.01, 0.06) * 0.6  # wet tail
        write_wav(f"impacts/impact_flesh_{i + 1}.wav", x, 0.75)


# ---------------------------------------------------------------- player

def step(seed_hz):
    n = 0.28
    x = lowpass(noise(n), 1400) * env(n, 0.004, 0.035)
    x += sweep(n, 160, seed_hz, 30) * env(n, 0.002, 0.03) * 0.5        # heel
    for _ in range(14):                                                  # gravel crunch
        at(x, highpass(noise(0.008), 2500) * env(0.008, 0.0002, 0.002), rng.uniform(0.0, 0.12), rng.uniform(0.1, 0.35))
    return x


def make_player():
    for i in range(4):
        write_wav(f"player/step_gravel_{i + 1}.wav", step(70 + 8 * i), 0.6)
    n = 0.5
    land = lowpass(noise(n), 900) * env(n, 0.003, 0.06) + sweep(n, 140, 50, 20) * env(n, 0.002, 0.09)
    for _ in range(20):
        at(land, highpass(noise(0.008), 2500) * env(0.008, 0.0002, 0.002), rng.uniform(0.0, 0.18), rng.uniform(0.1, 0.3))
    write_wav("player/land.wav", land, 0.75)
    for i in range(2):  # pain: a short voiced exhale
        n = 0.45
        t = t_axis(n)
        f0 = (130 + 20 * i) * (1.0 - 0.25 * t / n)
        voice = np.sign(np.sin(2 * np.pi * np.cumsum(f0) / RATE)) * 0.4
        breath = bandpass(voice + noise(n) * 0.5, 700 + 150 * i, 2.5) + bandpass(voice, 1400, 3) * 0.5
        write_wav(f"player/hurt_{i + 1}.wav", breath * env(n, 0.02, 0.14), 0.7)


# ---------------------------------------------------------------- ambience (streamed loops)

def make_ambience():
    n = 32.0
    t = t_axis(n + 2.0)
    gust = 0.55 + 0.45 * np.sin(2 * np.pi * t / 11.0) * np.sin(2 * np.pi * t / 4.3 + 1.0)
    wind = bandpass(noise(n + 2.0), 280 + 520 * gust, 1.4) * (0.35 + 0.65 * gust)
    wind += lowpass(noise(n + 2.0), 180) * 1.5                          # low rumble
    whistle = bandpass(noise(n + 2.0), 1100 + 300 * np.sin(2 * np.pi * t / 7.0), 18) * np.clip(gust - 0.6, 0, 1) * 2.5
    write_ogg("ambient/wind_loop.ogg", seamless(wind + whistle, 2.0))

    n = 12.0
    geiger = np.zeros(int((n + 1.0) * RATE))
    time = 0.0
    while time < n + 0.9:
        rate = 5.0 + 4.0 * np.sin(2 * np.pi * time / 6.0) ** 2           # clicks per second, drifting
        time += rng.exponential(1.0 / rate)
        at(geiger, highpass(noise(0.004), 1500) * env(0.004, 0.0001, 0.0008), time, rng.uniform(0.6, 1.0))
    write_ogg("ambient/geiger_loop.ogg", seamless(geiger, 1.0), 0.7)

    n = 40.0
    t = t_axis(n + 4.0)
    drone = sum(np.sin(2 * np.pi * f * t + 0.6 * np.sin(2 * np.pi * t / p)) * g
                for f, p, g in ((41.2, 13.0, 1.0), (55.0, 17.0, 0.7), (82.6, 11.0, 0.35), (61.8, 23.0, 0.4)))
    drone *= 0.6 + 0.4 * np.sin(2 * np.pi * t / 20.0) ** 2
    drone += bandpass(noise(n + 4.0), 220 + 60 * np.sin(2 * np.pi * t / 9.0), 6) * 0.25
    write_ogg("ambient/zone_drone_loop.ogg", seamless(drone, 4.0))


if __name__ == "__main__":
    groups = {"weapons": make_weapons, "impacts": make_impacts, "player": make_player, "ambient": make_ambience}
    for name in sys.argv[1:] or groups:
        groups[name]()
