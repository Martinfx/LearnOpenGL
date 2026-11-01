# Generates the colour LUT strips in this folder (32x32x32, 1024x32 PNG):
#   neutral_32.png    - identity (start point for your own grade)
#   zone_bleak_32.png - the Zone look: muted greens, cold teal shadows, dirty warm highlights
# Usage: python3 make_luts.py  (needs numpy and Pillow)
import numpy as np
from PIL import Image
import os

N = 32
here = os.path.dirname(os.path.abspath(__file__))

def strip(fn):
    r = np.linspace(0, 1, N)
    g, b, rr = np.meshgrid(r, r, r, indexing='ij')  # g rows, b slices, r columns
    rgb = np.stack([rr, g, b], axis=-1)              # [g][b][r]
    out = np.clip(fn(rgb), 0, 1)
    img = (out * 255 + 0.5).astype(np.uint8)         # rows g, columns b*N + r
    img = img.reshape(N, N * N, 3)
    return Image.fromarray(img, 'RGB')

def zone(c):
    luma = c @ np.array([0.2126, 0.7152, 0.0722])
    l = luma[..., None]
    # Mute saturation, more in greens (vegetation turns olive-grey).
    green = np.clip(c[..., 1:2] - np.maximum(c[..., 0:1], c[..., 2:3]), 0, 1)
    sat = 0.72 - 0.35 * np.clip(green * 3, 0, 1)
    c = l + (c - l) * sat
    # Split toning: teal in the shadows, dirty yellow in the highlights.
    shadows = (1 - l) ** 2
    highlights = l ** 2
    c = c + shadows * np.array([-0.025, 0.01, 0.03]) + highlights * np.array([0.035, 0.02, -0.03])
    # Gentle S-curve.
    c = c + 0.08 * np.sin((c - 0.5) * np.pi) * (1 - np.abs(c - 0.5) * 2) * 0.5
    return c

strip(lambda c: c).save(os.path.join(here, 'neutral_32.png'))
strip(zone).save(os.path.join(here, 'zone_bleak_32.png'))
