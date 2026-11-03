"""Textures of the incendiary bottle (procedural, CC0): python3 textures.py -> ../textures/*.png
label_albedo.png: an old, faded, torn paper label of a cheap rye bottle ("STATE LINE RYE", made up)
rag_albedo.png: dirty cotton rag, weave, oil stains, charred toward the burning end (top = v 0)
glass_albedo.png: dark green bottle glass with dust and grime toward the bottom (alpha unused)
"""
import os
import numpy as np
from PIL import Image, ImageDraw, ImageFont, ImageFilter

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, '..', 'textures')
os.makedirs(OUT, exist_ok=True)
rng = np.random.default_rng(5)


def noise(w, h, scale, seed):
    r = np.random.default_rng(seed)
    g = r.random((h // scale + 2, w // scale + 2))
    img = Image.fromarray((g * 255).astype(np.uint8)).resize((w + scale * 2, h + scale * 2), Image.BICUBIC)
    return np.asarray(img, dtype=np.float32)[:h, :w] / 255.0


def fbm(w, h, seed):
    out = np.zeros((h, w), np.float32)
    amp, tot = 1.0, 0.0
    for i, s in enumerate((64, 32, 16, 8, 4)):
        out += amp * noise(w, h, s, seed * 13 + i)
        tot += amp
        amp *= 0.55
    return out / tot


def save(name, arr):
    Image.fromarray(np.clip(arr * 255 + 0.5, 0, 255).astype(np.uint8)).save(os.path.join(OUT, name))


# Label (wraps 0.75 of the way around: 512 x 256)
W, H = 512, 256
paper = np.dstack([np.full((H, W), c) for c in (0.78, 0.71, 0.55)]).astype(np.float32)
paper *= (0.85 + 0.15 * fbm(W, H, 1))[..., None]
img = Image.fromarray((paper * 255).astype(np.uint8))
d = ImageDraw.Draw(img)
try:
    font = ImageFont.truetype('/usr/share/fonts/truetype/dejavu/DejaVuSerif-Bold.ttf', 46)
    small = ImageFont.truetype('/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf', 20)
except OSError:
    font = small = ImageFont.load_default()
d.rectangle([0, 30, W, 52], fill=(120, 34, 28))
d.rectangle([0, 200, W, 222], fill=(120, 34, 28))
d.text((W / 2, 110), 'STATE LINE', font=font, fill=(40, 30, 24), anchor='mm')
d.text((W / 2, 160), 'RYE WHISKEY  ·  80 PROOF', font=small, fill=(60, 40, 30), anchor='mm')
img = img.filter(ImageFilter.GaussianBlur(0.8))
lab = np.asarray(img, np.float32) / 255.0
# fading, stains, torn lower edge (darker where it is wet with fuel)
stain = fbm(W, H, 7)
lab = lab * (0.7 + 0.3 * stain[..., None])
lab = lab * 0.85 + 0.15 * np.array([0.6, 0.5, 0.35])
oil = np.clip((fbm(W, H, 9) - 0.55) * 4, 0, 1)
lab = lab * (1 - 0.45 * oil[..., None])
save('label_albedo.png', lab)

# Rag (256 x 256): u around, v along (v 0 = burning end)
W = H = 256
y, x = np.mgrid[0:H, 0:W].astype(np.float32)
weave = 0.5 + 0.25 * np.sin(x * 1.6) * np.sin(y * 1.6) + 0.25 * np.sin((x + y) * 0.8)
base = np.array([0.46, 0.42, 0.35], np.float32)
rag = base * (0.75 + 0.15 * weave[..., None]) * (0.75 + 0.35 * fbm(W, H, 3)[..., None])
oil = np.clip((fbm(W, H, 4) - 0.4) * 2.5, 0, 1)
rag = rag * (1 - 0.5 * oil[..., None]) + oil[..., None] * np.array([0.12, 0.09, 0.05]) * 0.5
char = np.clip(1.1 - (y / H) * 2.2 + (fbm(W, H, 6) - 0.5) * 0.8, 0, 1)
rag = rag * (1 - 0.92 * char[..., None])
save('rag_albedo.png', rag)

# Glass (256 x 512): u around, v along (v 0 = bottom)
W, H = 256, 512
y, x = np.mgrid[0:H, 0:W].astype(np.float32)
glass = np.array([0.055, 0.12, 0.06], np.float32) * (0.9 + 0.2 * fbm(W, H, 11)[..., None])
dust = np.clip((fbm(W, H, 12) - 0.35) * 1.6, 0, 1) * np.clip(1.1 - y / H * 1.6, 0, 1)
glass = glass * (1 - dust[..., None]) + dust[..., None] * np.array([0.32, 0.29, 0.24])
save('glass_albedo.png', glass)
print('textures written to', os.path.normpath(OUT))
