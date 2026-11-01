# Generates ambient/alarm_loop.ogg (procedural, CC0): an old two-tone facility alarm
# heard through a concrete building. python3 alarm_siren.py out.wav, then
# ffmpeg -i out.wav -c:a libvorbis -q:a 4 ../ambient/alarm_loop.ogg
import numpy as np, wave, sys
sr = 44100
period = 1.6                       # one high-low cycle, seconds
n = int(sr * period * 4)           # 4 cycles = seamless loop
t = np.arange(n) / sr
phase_in_cycle = (t % period) / period
# Two tones with a short glide between them (tired electromechanical horn).
f = np.where(phase_in_cycle < 0.5, 660.0, 520.0)
glide = int(0.04 * sr)
kernel = np.ones(glide) / glide
f = np.convolve(np.concatenate([f[-glide:], f]), kernel, mode="valid")[: n]
phase = 2 * np.pi * np.cumsum(f) / sr
# Square-ish horn: odd harmonics, slightly rough.
x = np.sin(phase) + 0.35 * np.sin(3 * phase) + 0.18 * np.sin(5 * phase) + 0.08 * np.sin(7 * phase)
x *= 1.0 + 0.04 * np.sin(2 * np.pi * 31 * t)  # a buzz in the old speaker
# Concrete halls: a few late echoes.
out = x.copy()
for delay, gain in ((0.11, 0.35), (0.23, 0.22), (0.41, 0.12)):
    d = int(delay * sr)
    out += gain * np.roll(x, d)  # roll keeps the loop seamless
out = np.tanh(out * 0.6)
out /= np.max(np.abs(out))
pcm = (out * 0.7 * 32767).astype(np.int16)
with wave.open(sys.argv[1], "wb") as w:
    w.setnchannels(1); w.setsampwidth(2); w.setframerate(sr); w.writeframes(pcm.tobytes())
