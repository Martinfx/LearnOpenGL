#!/usr/bin/env python3
"""Radios left on in the zone (procedural, CC0): an old waltz on the accordion from a far-away station and an
empty band with a beacon beeping, both through a small transistor speaker (band-pass 350 Hz - 3.2 kHz, AM fading,
static, crackle, a heterodyne whistle, soft clipping). Writes mono 22 kHz seamless loops:

    python3 -I assets/sounds/source/radio.py      (needs ffmpeg with libvorbis)

-> assets/sounds/ambient/radio_waltz_loop.ogg, radio_beacon_loop.ogg (played by radio_waltz.sound, radio_beacon.sound)"""
import os
import subprocess
import tempfile
import wave

import numpy as np

SR = 22050
OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'ambient')
rng = np.random.default_rng(1986)

NOTE = {n: i for i, n in enumerate(['C', 'C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#', 'A', 'A#', 'B'])}


def freq(name):
    """'A4' -> Hz."""
    n, o = name[:-1], int(name[-1])
    return 440.0 * 2 ** ((NOTE[n] + 12 * (o + 1) - 69) / 12)


def reed(f, dur, amp, attack=0.025, release=0.09):
    """Accordion-like voice: two slightly detuned reeds, odd-heavy harmonics, bellows tremolo."""
    t = np.arange(int(dur * SR)) / SR
    y = np.zeros_like(t)
    for det in (-0.0018, 0.0018):
        ph = 2 * np.pi * f * (1 + det) * t
        for k, a in ((1, 1.0), (2, 0.35), (3, 0.55), (4, 0.18), (5, 0.3), (7, 0.12)):
            y += a * np.sin(k * ph)
    env = np.minimum(1, t / attack) * np.minimum(1, (dur - t) / release).clip(0, 1)
    return amp * y * env * (1 + 0.08 * np.sin(2 * np.pi * 5.2 * t))


# A minor waltz, 3/4 at 126 bpm: melody (note, beats) and one chord per bar (root, third, fifth)
A = [('E4', 2), ('A4', 1), ('C5', 2), ('B4', 1), ('A4', 2), ('E4', 1), ('A4', 3),
     ('D5', 2), ('C5', 1), ('B4', 2), ('A4', 1), ('G#4', 2), ('B4', 1), ('E4', 3)]
B = [('E4', 2), ('A4', 1), ('C5', 2), ('E5', 1), ('D5', 2), ('C5', 1), ('B4', 3),
     ('A4', 2), ('B4', 1), ('C5', 1), ('B4', 1), ('A4', 1), ('G#4', 2), ('B4', 1), ('A4', 3)]
CHORDS_A = ['Am', 'Am', 'Am', 'Am', 'Dm', 'Am', 'E', 'E']
CHORDS_B = ['Am', 'Am', 'Dm', 'E', 'F', 'Am', 'E', 'Am']
CHORD = {'Am': ('A2', 'A3', 'C4', 'E4'), 'Dm': ('D3', 'A3', 'D4', 'F4'), 'E': ('E2', 'G#3', 'B3', 'E4'),
         'F': ('F2', 'A3', 'C4', 'F4')}


def waltz():
    beat = 60 / 126
    melody = A + B + A + B
    chords = CHORDS_A + CHORDS_B + CHORDS_A + CHORDS_B
    n = int(len(chords) * 3 * beat * SR)
    y = np.zeros(n)
    t0 = 0.0
    for note, beats in melody:
        s = reed(freq(note), beats * beat * 0.95, 0.5)
        i = int(t0 * SR)
        y[i:i + len(s)] += s[:n - i]
        t0 += beats * beat
    for bar, c in enumerate(chords):          # oom-pah-pah
        bass, *tri = CHORD[c]
        i = int(bar * 3 * beat * SR)
        s = reed(freq(bass), beat * 0.8, 0.45)
        y[i:i + len(s)] += s
        for k in (1, 2):
            j = i + int(k * beat * SR)
            for nn in tri:
                s = reed(freq(nn), beat * 0.45, 0.12)
                y[j:j + len(s)] += s[:n - j]
    return y / np.abs(y).max()


def beacon(seconds=36.0):
    """Empty band: a beacon repeating its call sign (short and long beeps), long gaps of hiss."""
    n = int(seconds * SR)
    y = np.zeros(n)
    t = 1.0
    code = '.-- ..-. -.-'
    while t < seconds - 6:
        for ch in code:
            if ch == ' ':
                t += 0.45
                continue
            d = 0.09 if ch == '.' else 0.27
            tt = np.arange(int(d * SR)) / SR
            s = np.sin(2 * np.pi * 1020 * tt) * np.minimum(1, np.minimum(tt, d - tt) / 0.006)
            i = int(t * SR)
            y[i:i + len(s)] += 0.8 * s
            t += d + 0.09
        t += rng.uniform(4.0, 7.0)
    return y


def bandpass(x, lo=350.0, hi=3200.0):
    X = np.fft.rfft(x)
    f = np.fft.rfftfreq(len(x), 1 / SR)
    g = 1 / (1 + (lo / np.maximum(f, 1)) ** 4) / (1 + (f / hi) ** 6)
    return np.fft.irfft(X * g, len(x))


def through_radio(x, signal=0.8, hiss=0.12):
    n = len(x)
    t = np.arange(n) / SR
    fading = 0.65 + 0.35 * np.sin(2 * np.pi * 0.07 * t + 1.0) * np.sin(2 * np.pi * 0.023 * t)
    noise = rng.standard_normal(n)
    pink = np.cumsum(noise) * 0.02
    pink -= np.convolve(pink, np.ones(2000) / 2000, mode='same')
    static = hiss * (0.6 * noise + 0.4 * pink / (np.abs(pink).max() + 1e-9)) * (1.25 - fading * 0.5)
    crackle = np.zeros(n)
    for _ in range(int(n / SR * 9)):
        i = rng.integers(0, n - 400)
        crackle[i:i + 400] += rng.uniform(0.2, 0.8) * rng.standard_normal(400) * np.exp(-np.arange(400) / 60)
    whistle = 0.03 * np.sin(2 * np.pi * np.cumsum(900 + 250 * np.sin(2 * np.pi * 0.05 * t)) / SR)
    y = bandpass(signal * x * fading + static + 0.5 * crackle + whistle)
    y = np.tanh(1.8 * y / (np.abs(y).max() + 1e-9)) * 0.27   # about -26 dB mean: a small set in a room
    return y


def seamless(y, fade=0.6):
    """Crossfade the tail into the head so the loop has no seam."""
    k = int(fade * SR)
    out = y[k:].copy()
    w = np.linspace(0, 1, k)
    out[-k:] = out[-k:] * (1 - w) + y[:k] * w
    return out


def write_ogg(y, name):
    path = os.path.join(OUT, name)
    with tempfile.TemporaryDirectory() as tmp:
        wav = os.path.join(tmp, 'r.wav')
        with wave.open(wav, 'wb') as w:
            w.setnchannels(1)
            w.setsampwidth(2)
            w.setframerate(SR)
            w.writeframes((np.clip(y, -1, 1) * 32000).astype(np.int16).tobytes())
        subprocess.run(['ffmpeg', '-y', '-loglevel', 'error', '-i', wav, '-c:a', 'libvorbis', '-q:a', '3', path], check=True)
    print('wrote', os.path.normpath(path), f'{len(y) / SR:.1f} s')


if __name__ == '__main__':
    write_ogg(seamless(through_radio(waltz(), 0.75, 0.1)), 'radio_waltz_loop.ogg')
    write_ogg(seamless(through_radio(beacon(), 0.5, 0.22)), 'radio_beacon_loop.ogg')
