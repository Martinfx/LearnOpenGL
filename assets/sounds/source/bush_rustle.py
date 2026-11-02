#!/usr/bin/env python3
"""Generates player/bush_1..5.wav: leaves and twigs brushing past the legs (procedural, CC0).

    python3 assets/sounds/source/bush_rustle.py

Each variation is ~0.6 s of leaf grains (band-passed noise bursts, 1.5-9 kHz, swelling and
fading like a leg pushing through) with a twig tick or two. Mono 16-bit 48 kHz (OpenAL
spatialises mono buffers only). Numpy only.
"""
import os, wave
import numpy as np

SR = 48000
OUT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'player'))


def bandpass(x, lo, hi):
    spec = np.fft.rfft(x)
    f = np.fft.rfftfreq(len(x), 1 / SR)
    gain = np.clip((f - lo * 0.7) / (lo * 0.3), 0, 1) * np.clip((hi * 1.3 - f) / (hi * 0.3), 0, 1)
    return np.fft.irfft(spec * gain, len(x))


def rustle(seed):
    rng = np.random.default_rng(seed)
    dur = rng.uniform(0.5, 0.75)
    n = int(SR * dur)
    out = np.zeros(n)
    t = np.arange(n) / n
    # The push: swells in, peaks a third of the way, trails off.
    peak = rng.uniform(0.25, 0.4)
    swell = np.where(t < peak, (t / peak) ** 1.5, np.exp(-(t - peak) / (0.35 * (1 - peak))))
    # Leaf grains: short crisp bursts, more of them near the peak.
    for _ in range(int(rng.uniform(70, 110))):
        pos = int(np.clip(rng.normal(peak, 0.25), 0, 0.95) * n)
        length = int(SR * rng.uniform(0.004, 0.025))
        if pos + length >= n:
            continue
        g = rng.standard_normal(length) * np.hanning(length) ** 2
        out[pos:pos + length] += g * rng.uniform(0.3, 1.0)
    out = bandpass(out, 1500, 9000)
    # A soft bed of leaf hiss under the grains.
    bed = bandpass(rng.standard_normal(n), 2500, 7000) * 0.25
    out = (out + bed) * swell
    # Twig ticks: one or two short, lower clicks.
    for _ in range(int(rng.integers(1, 3))):
        pos = int(rng.uniform(0.15, 0.7) * n)
        length = int(SR * 0.012)
        if pos + length < n:
            click = rng.standard_normal(length) * np.exp(-np.arange(length) / (length * 0.15))
            out[pos:pos + length] += bandpass(np.pad(click, (0, 2048)), 600, 3500)[:length] * rng.uniform(0.6, 1.2)
    fade = int(SR * 0.01)
    out[:fade] *= np.linspace(0, 1, fade)
    out[-fade:] *= np.linspace(1, 0, fade)
    return out / np.max(np.abs(out)) * 0.7


for i in range(1, 6):
    data = (rustle(1000 + i) * 32767).astype('<i2')
    with wave.open(os.path.join(OUT, f'bush_{i}.wav'), 'wb') as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(SR)
        w.writeframes(data.tobytes())
print('wrote', OUT)
