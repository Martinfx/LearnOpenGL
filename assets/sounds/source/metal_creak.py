# Generates ambient/metal_creak_1..4.ogg (procedural, CC0): rusty sheet metal and girders
# creaking in the wind. Stick-slip friction (a jittery pulse train whose rate drifts)
# excites a few long metal resonances. python3 metal_creak.py outdir, then
# for f in outdir/*.wav; do ffmpeg -i $f -c:a libvorbis -q:a 4 ../ambient/$(basename ${f%.wav}).ogg; done
import numpy as np, wave, sys, os
sr = 44100

def resonator(x, freq, decay):
    # two-pole band-pass: y[n] = x[n] + 2r cos(w) y[n-1] - r^2 y[n-2]
    r = np.exp(-1.0 / (decay * sr)); w = 2 * np.pi * freq / sr
    a1, a2 = 2 * r * np.cos(w), -r * r
    y = np.zeros_like(x); y1 = y2 = 0.0
    for i in range(len(x)):
        y0 = x[i] + a1 * y1 + a2 * y2
        y[i] = y0; y2, y1 = y1, y0
    return y * (1 - r)

def creak(seed, dur, f0, f1, modes):
    rng = np.random.default_rng(seed)
    n = int(sr * dur); t = np.arange(n) / sr
    # Friction pulse rate glides f0 -> f1 with a wobble; pulses come late or early at random.
    rate = f0 + (f1 - f0) * (t / dur) ** 1.3 + 4 * np.sin(2 * np.pi * 1.7 * t + rng.random() * 6)
    phase = np.cumsum(rate / sr)
    x = np.zeros(n)
    k = 1
    while True:
        idx = np.searchsorted(phase, k + rng.normal(0, 0.08))
        if idx >= n: break
        x[idx] += 0.6 + 0.4 * rng.random(); k += 1
    env = np.sin(np.pi * np.clip(t / dur, 0, 1)) ** 0.7  # swell and fade
    x *= env
    x += 0.004 * rng.standard_normal(n) * env  # grit
    out = sum(g * resonator(x, f, d) for f, d, g in modes)
    out = np.tanh(out / np.max(np.abs(out)) * 1.4)
    return out / np.max(np.abs(out)) * 0.8

outdir = sys.argv[1]
os.makedirs(outdir, exist_ok=True)
variants = [
    (1, 1.6, 34, 22, [(410, 0.25, 1.0), (930, 0.18, 0.6), (1730, 0.12, 0.35)]),
    (2, 2.3, 18, 30, [(290, 0.35, 1.0), (655, 0.22, 0.7), (1390, 0.15, 0.3)]),
    (3, 1.2, 45, 38, [(520, 0.2, 1.0), (1220, 0.14, 0.5), (2210, 0.1, 0.25)]),
    (4, 2.8, 26, 14, [(240, 0.4, 1.0), (560, 0.3, 0.8), (1010, 0.2, 0.4)]),
]
for seed, dur, f0, f1, modes in variants:
    out = creak(seed, dur, f0, f1, modes)
    tail = np.zeros(int(sr * 0.6)); out = np.concatenate([out, tail])  # room for the ring-out
    pcm = (out * 32767).astype(np.int16)
    with wave.open(os.path.join(outdir, f"metal_creak_{seed}.wav"), "wb") as w:
        w.setnchannels(1); w.setsampwidth(2); w.setframerate(sr); w.writeframes(pcm.tobytes())
