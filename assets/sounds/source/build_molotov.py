#!/usr/bin/env python3
"""Incendiary bottle breaking: weapons/molotov_break_{1,2,3}.wav (mono 16-bit 48 kHz).

    python3 assets/sounds/source/build_molotov.py [cache_dir]

Each one is a real glass-breaking recording ("75 CC0 breaking / falling / hit sfx" by rubberduck, CC0,
OpenGameArt) layered with a synthesized fuel "whoomp" (a swelling, low-passed noise burst with a short
crackle tail, made here, CC0). Needs ffmpeg and numpy.
"""
import io, os, subprocess, sys, urllib.request, wave, zipfile
import numpy as np

CACHE = sys.argv[1] if len(sys.argv) > 1 else '/tmp/sound_cache'
OUT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'weapons'))
URL = 'https://opengameart.org/sites/default/files/sfx_breaking_and_falling.zip'
SR = 48000
rng = np.random.default_rng(31)


def archive():
    d = os.path.join(CACHE, 'bfh')
    if not os.path.isdir(d):
        os.makedirs(d)
        data = urllib.request.urlopen(urllib.request.Request(URL, headers={'User-Agent': 'Mozilla/5.0'})).read()
        zipfile.ZipFile(io.BytesIO(data)).extractall(d)
    return d


def decode(path):
    raw = subprocess.run(['ffmpeg', '-v', 'quiet', '-i', path, '-ac', '1', '-ar', str(SR), '-f', 's16le', '-'],
                         capture_output=True, check=True).stdout
    return np.frombuffer(raw, np.int16).astype(np.float64) / 32768.0


def onepole(x, a):
    k = int(6 / a)
    kernel = a * (1 - a) ** np.arange(k)
    return np.convolve(x, kernel)[:len(x)]


def whoomp(length, seed):
    r = np.random.default_rng(seed)
    n = int(length * SR)
    t = np.arange(n) / SR
    noise = r.standard_normal(n)
    body = onepole(noise, 0.035) - onepole(onepole(noise, 0.035), 0.004)
    body /= np.max(np.abs(body)) + 1e-9
    # fast swell (fuel catching) then a long decaying roar
    env = (1 - np.exp(-t / 0.045)) * np.exp(-t / 0.55)
    low = onepole(r.standard_normal(n), 0.006)
    low /= np.max(np.abs(low)) + 1e-9
    out = body * env * 0.55 + low * (1 - np.exp(-t / 0.08)) * np.exp(-t / 0.4) * 0.5
    # crackle of burning drops
    for _ in range(40):
        p = int(r.uniform(0.08, length * 0.9) * SR)
        L = int(r.uniform(0.002, 0.008) * SR)
        if p + L < n:
            out[p:p + L] += r.standard_normal(L) * np.exp(-np.arange(L) / (L * 0.2)) * r.uniform(0.05, 0.18) * np.exp(-p / SR / 0.8)
    return out


def write(path, x):
    x = np.tanh(x * 1.2) / np.tanh(1.2)
    x = x / (np.max(np.abs(x)) + 1e-9) * 0.89
    with wave.open(path, 'wb') as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(SR)
        w.writeframes((x * 32767).astype(np.int16).tobytes())


d = archive()
for i, take in enumerate((1, 3, 5)):
    glass = decode(os.path.join(d, f'bfh1_glass_breaking_{take:02d}.ogg'))
    glass = glass[np.argmax(np.abs(glass) > 0.02):]  # from the first hit
    length = max(1.3, len(glass) / SR + 0.2)
    fire = whoomp(length, 100 + i)
    mix = np.zeros(int(length * SR))
    mix[:len(glass)] += glass[:len(mix)] / (np.max(np.abs(glass)) + 1e-9) * 0.9
    delay = int(0.03 * SR)  # the fuel flares a moment after the glass
    mix[delay:] += fire[:len(mix) - delay] * 0.75
    fade = int(0.15 * SR)
    mix[-fade:] *= np.linspace(1, 0, fade)
    write(os.path.join(OUT, f'molotov_break_{i + 1}.wav'), mix)
    print('wrote', f'molotov_break_{i + 1}.wav', round(len(mix) / SR, 2), 's')
