# Generates ambient/thunder_near_1..2.ogg and thunder_far_1..3.ogg (procedural, CC0).
# Near: a bright crack (high-passed noise burst with fast sub-bursts) into a rolling
# rumble; far: only the low rumble with a soft onset. The rumble is brown noise whose
# loudness rolls in random swells (echoes of the bolt's segments from the terrain).
# python3 thunder.py outdir, then
# for f in outdir/*.wav; do ffmpeg -i $f -c:a libvorbis -q:a 4 ../ambient/$(basename ${f%.wav}).ogg; done
import numpy as np, wave, sys, os
sr = 44100

def onepole_lp(x, fc):
    a = np.exp(-2 * np.pi * fc / sr)
    y = np.empty_like(x); s = 0.0
    for i in range(len(x)):
        s = (1 - a) * x[i] + a * s; y[i] = s
    return y

def swells(rng, n, count, dur):
    t = np.arange(n) / sr
    env = np.zeros(n)
    for _ in range(count):
        c = rng.uniform(0.0, dur * 0.7) ** 1.2 / (dur * 0.7) ** 0.2
        w = rng.uniform(0.25, 1.2)
        env += rng.uniform(0.3, 1.0) * np.exp(-((t - c) / w) ** 2)
    return env

def thunder(seed, dur, near):
    rng = np.random.default_rng(seed)
    n = int(sr * dur); t = np.arange(n) / sr
    white = rng.standard_normal(n)
    brown = np.cumsum(white); brown -= onepole_lp(brown, 8.0)  # brown noise without drift
    rumble = onepole_lp(onepole_lp(brown, 260 if near else 140), 400 if near else 180)
    env = swells(rng, n, 9 if near else 6, dur) * np.exp(-t / (dur * 0.45))
    env *= np.clip(t / (0.05 if near else 0.9), 0, 1)  # far thunder fades in
    out = rumble / np.max(np.abs(rumble)) * env
    if near:
        crack = white - onepole_lp(white, 1500)  # high-passed
        cenv = np.zeros(n)
        start = int(sr * 0.02)
        for k in range(rng.integers(4, 8)):  # the bolt's flickering return strokes
            p = start + int(sr * rng.uniform(0, 0.25))
            L = int(sr * rng.uniform(0.02, 0.08))
            cenv[p:p + L] += rng.uniform(0.5, 1.0) * np.exp(-np.arange(min(L, n - p)) / (L * 0.3))
        cenv = onepole_lp(cenv, 60)
        out = out * 0.8 + crack / np.max(np.abs(crack)) * cenv * 1.6
    out = np.tanh(out / np.max(np.abs(out)) * 1.3)
    fade = np.clip((dur - t) / 0.8, 0, 1)
    return out / np.max(np.abs(out)) * 0.9 * fade

outdir = sys.argv[1]
os.makedirs(outdir, exist_ok=True)
for name, seed, dur, near in [("thunder_near_1", 11, 6.5, True), ("thunder_near_2", 12, 7.5, True),
                              ("thunder_far_1", 21, 7.0, False), ("thunder_far_2", 22, 8.5, False), ("thunder_far_3", 23, 6.0, False)]:
    pcm = (thunder(seed, dur, near) * 32767).astype(np.int16)
    with wave.open(os.path.join(outdir, name + ".wav"), "wb") as w:
        w.setnchannels(1); w.setsampwidth(2); w.setframerate(sr); w.writeframes(pcm.tobytes())
