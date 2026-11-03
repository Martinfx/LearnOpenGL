"""Shop signs of an abandoned New York street (original designs, CC0): painted fascia boards over the
shop windows, decades faded, peeling, rust-streaked, letters lost.

make_atlas() paints zone/textures/shop_signs_albedo.png: 2 columns x 8 rows of 1024 x 256 boards
(alpha 1 on the board, 0 round its broken edges). The city grammar (engine/src/CityGrammar.cpp,
material shop_signs, kSigns) maps one board over a shopfront; keep SIGNS in the same order."""
import os

import numpy as np
from PIL import Image, ImageDraw, ImageFilter, ImageFont

from .mats import TEX_OUT

W, H = 1024, 256
COLS, ROWS = 2, 8
BOLD = '/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf'
CONDENSED = '/usr/share/fonts/truetype/freefont/FreeSansBold.ttf'
SERIF = '/usr/share/fonts/truetype/dejavu/DejaVuSerif-Bold.ttf'

# text, board colour, letter colour, font, small line
SIGNS = [
    ('DELI', (0.12, 0.28, 0.16), (0.92, 0.86, 0.62), BOLD, 'SANDWICHES - COFFEE'),
    ('PHARMACY', (0.86, 0.86, 0.82), (0.12, 0.3, 0.55), BOLD, 'PRESCRIPTIONS'),
    ('LAUNDROMAT', (0.15, 0.3, 0.55), (0.95, 0.95, 0.92), CONDENSED, 'WASH - DRY - FOLD'),
    ('LIQUORS', (0.55, 0.08, 0.06), (0.95, 0.9, 0.7), SERIF, 'WINES & SPIRITS'),
    ('BARBER SHOP', (0.9, 0.88, 0.8), (0.6, 0.08, 0.06), SERIF, 'HOT TOWEL SHAVE'),
    ('HARDWARE', (0.85, 0.62, 0.1), (0.1, 0.1, 0.1), BOLD, 'KEYS CUT - PAINT'),
    ('PIZZA', (0.1, 0.35, 0.18), (0.95, 0.93, 0.85), BOLD, 'BY THE SLICE'),
    ('GROCERY', (0.62, 0.12, 0.08), (0.96, 0.92, 0.78), BOLD, 'FRUITS - VEGETABLES'),
    ('PAWN SHOP', (0.08, 0.08, 0.1), (0.9, 0.72, 0.2), BOLD, 'WE BUY GOLD'),
    ('DINER', (0.75, 0.75, 0.72), (0.65, 0.05, 0.08), SERIF, 'OPEN 24 HOURS'),
    ('DRY CLEANING', (0.2, 0.22, 0.45), (0.95, 0.95, 0.9), CONDENSED, 'SAME DAY SERVICE'),
    ('BAR & GRILL', (0.12, 0.1, 0.08), (0.88, 0.6, 0.2), SERIF, 'COLD BEER'),
    ('SHOE REPAIR', (0.45, 0.3, 0.15), (0.95, 0.9, 0.75), SERIF, 'WHILE U WAIT'),
    ('NEWS & CANDY', (0.9, 0.78, 0.1), (0.12, 0.12, 0.12), BOLD, 'LOTTO - CIGARETTES'),
    ('CHECKS CASHED', (0.1, 0.4, 0.2), (0.95, 0.95, 0.9), CONDENSED, 'MONEY ORDERS'),
    ('FOR RENT', (0.88, 0.88, 0.85), (0.7, 0.06, 0.05), BOLD, 'CALL 212-555-0143'),
]


def _noise(rng, w, h, scale):
    g = rng.random((h // scale + 2, w // scale + 2)).astype(np.float32)
    im = Image.fromarray((g * 255).astype(np.uint8)).resize((w + scale * 2, h + scale * 2), Image.BICUBIC)
    return np.asarray(im, np.float32)[:h, :w] / 255


def _board(k, rng):
    text, board, ink, font, small = SIGNS[k]
    img = Image.new('RGB', (W, H), tuple(int(c * 255) for c in board))
    d = ImageDraw.Draw(img)
    # Letters: as big as fit, a line of small print under them.
    size = 170
    while size > 40:
        f = ImageFont.truetype(font, size)
        x0, y0, x1, y1 = d.textbbox((0, 0), text, font=f)
        if x1 - x0 < W * 0.86 and y1 - y0 < H * 0.6:
            break
        size -= 6
    f = ImageFont.truetype(font, size)
    x0, y0, x1, y1 = d.textbbox((0, 0), text, font=f)
    tx, ty = (W - (x1 - x0)) / 2 - x0, H * 0.4 - (y1 - y0) / 2 - y0
    letters = Image.new('L', (W, H), 0)
    dl = ImageDraw.Draw(letters)
    dl.text((tx, ty), text, font=f, fill=255)
    fs = ImageFont.truetype(CONDENSED, 30)
    sx0, _, sx1, _ = dl.textbbox((0, 0), small, font=fs)
    dl.text(((W - (sx1 - sx0)) / 2, H * 0.76), small, font=fs, fill=255)
    lm = np.asarray(letters, np.float32) / 255
    # A letter or two fell off: blank a random vertical slice.
    if rng.random() < 0.5:
        a = rng.uniform(0.15, 0.8) * W
        lm[:, int(a):int(a + rng.uniform(30, 70))] *= 0.1
    rgb = np.asarray(img, np.float32) / 255
    inkc = np.array(ink, np.float32)
    rgb = rgb * (1 - lm[..., None]) + inkc * lm[..., None]
    # Weather: fading to grey, peeling paint to bare grey wood, rust streaks from the fixings, grime.
    fade = rng.uniform(0.25, 0.5)
    grey = rgb.mean(axis=2, keepdims=True)
    rgb = rgb * (1 - fade) + (grey * 0.8 + 0.12) * fade
    peel = _noise(rng, W, H, 48) * 0.8 + _noise(rng, W, H, 10) * 0.2
    pm = np.clip((peel - 0.68) * 5, 0, 1)[..., None] * 0.8
    rgb = rgb * (1 - pm) + (rgb * 0.4 + np.array([0.3, 0.29, 0.26], np.float32)) * pm
    for _ in range(int(rng.integers(2, 6))):
        x = int(rng.uniform(0.05, 0.95) * W)
        streak = np.zeros((H, W), np.float32)
        ln = int(rng.uniform(0.3, 1.0) * H)
        streak[int(H * 0.08):int(H * 0.08) + ln, max(0, x - 3):x + 3] = 1
        streak = np.asarray(Image.fromarray((streak * 255).astype(np.uint8)).filter(ImageFilter.GaussianBlur(4)), np.float32) / 255
        rgb = rgb * (1 - streak[..., None] * 0.6) + np.array([0.35, 0.18, 0.08], np.float32) * streak[..., None] * 0.6
    dirt = _noise(rng, W, H, 40)[..., None]
    rgb *= 0.72 + 0.28 * dirt
    yy = np.linspace(0, 1, H, dtype=np.float32)[:, None, None]
    rgb *= 0.85 + 0.15 * (1 - yy)  # darker at the bottom (rain splash)
    # Frame and broken corners (alpha).
    rgb[:8], rgb[-8:], rgb[:, :8], rgb[:, -8:] = rgb[:8] * 0.5, rgb[-8:] * 0.5, rgb[:, :8] * 0.5, rgb[:, -8:] * 0.5
    a = np.ones((H, W), np.float32)
    if rng.random() < 0.5:
        cx = W if rng.random() < 0.5 else 0
        r = rng.uniform(60, 160)
        X, Y = np.meshgrid(np.arange(W), np.arange(H))
        broken = (np.abs(X - cx) + (H - Y) * 0.7 + _noise(rng, W, H, 8) * 40) < r
        a[broken] = 0
    return np.dstack([np.clip(rgb, 0, 1), a])


def make_atlas(force=False):
    path = f'{TEX_OUT}/shop_signs_albedo.png'
    if os.path.exists(path) and not force:
        return path
    out = np.zeros((H * ROWS, W * COLS, 4), np.float32)
    for k in range(len(SIGNS)):
        rng = np.random.default_rng(500 + k)
        c, r = k % COLS, k // COLS
        out[r * H:(r + 1) * H, c * W:(c + 1) * W] = _board(k, rng)
    os.makedirs(TEX_OUT, exist_ok=True)
    Image.fromarray(np.clip(out * 255 + 0.5, 0, 255).astype(np.uint8), 'RGBA').save(path)
    return path


if __name__ == '__main__':
    print(make_atlas(force=True))
