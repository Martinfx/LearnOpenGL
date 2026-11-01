# Generates ambient/campfire_loop.ogg (procedural, CC0): python3 campfire_crackle.py out.wav, then
# ffmpeg -i out.wav -c:a libvorbis -q:a 4 ../ambient/campfire_loop.ogg
import numpy as np, wave, sys
rng = np.random.default_rng(7)
sr = 44100; dur = 12.0; n = int(sr*dur)
# Base: soft roar of flames = lowpassed noise with slow breathing
noise = rng.standard_normal(n)
def lowpass(x, a):
    y = np.empty_like(x); acc = 0.0
    for i in range(len(x)):
        acc += a*(x[i]-acc); y[i] = acc
    return y
# fast one-pole via cumulative trick: use filter in chunks with numpy (simple loop is slow; use lfilter-like recursion via np)
def onepole(x, a):
    # y[n] = y[n-1] + a*(x[n]-y[n-1]); implement with vectorized exponential smoothing via convolution kernel
    k = int(6/a)
    kernel = a*(1-a)**np.arange(k)
    return np.convolve(x, kernel)[:len(x)]
roar = onepole(noise, 0.02)
roar -= onepole(roar, 0.002)  # remove rumble DC
t = np.arange(n)/sr
breath = 0.7 + 0.3*np.sin(2*np.pi*0.23*t) * np.sin(2*np.pi*0.07*t+1.3)
out = roar/np.max(np.abs(roar)) * 0.18 * breath
# Crackles: short clicks with exponential decay, band-ish noise
def add_click(pos, amp, length, bright):
    L = int(length*sr)
    if pos+L >= n: return
    env = np.exp(-np.arange(L)/(L*0.18))
    c = rng.standard_normal(L)
    if bright < 1.0:
        c = onepole(c, bright)
        c /= np.max(np.abs(c))+1e-9
    out[pos:pos+L] += c*env*amp
i = 0
while i < n:
    gap = rng.exponential(0.07)
    i += int(gap*sr)
    burst = rng.random() < 0.15
    for b in range(rng.integers(3,9) if burst else 1):
        p = i + (int(rng.random()*0.06*sr) if burst else 0)
        add_click(p, rng.uniform(0.05,0.35)*(1.3 if burst else 1.0), rng.uniform(0.002,0.012), rng.uniform(0.3,1.0))
# Occasional pops (wood snapping)
for _ in range(int(dur*0.6)):
    p = int(rng.random()*(n-sr))
    add_click(p, rng.uniform(0.5,0.8), rng.uniform(0.015,0.04), 0.5)
# Seamless loop: crossfade last 0.5 s into the start
xf = int(0.5*sr)
fade = np.linspace(0,1,xf)
out[:xf] = out[:xf]*fade + out[-xf:]*(1-fade)
out = out[:-xf]
out = np.tanh(out*1.2)*0.8
pcm = (out*32767).astype(np.int16)
with wave.open(sys.argv[1],'wb') as w:
    w.setnchannels(1); w.setsampwidth(2); w.setframerate(sr); w.writeframes(pcm.tobytes())
