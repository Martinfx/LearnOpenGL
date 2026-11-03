"""Wall writing and stains for Skrunda-1 (original designs, CC0): spray-painted slogans of the last soldiers and
of stalkers, a stencilled radiation warning, chalk, soot above burnt-out windows and damp stains.

make_atlas() paints one RGBA atlas (zone/textures/graffiti_albedo.png, alpha-blended decal like the grime);
quad(name) is a decal model of one design: a quad in the XY plane facing +Z, origin at the bottom centre, sized
in metres, placed 2 cm off a wall by make_skrunda_level.py (collision off)."""
import math
import os

import numpy as np
from PIL import Image, ImageDraw, ImageFilter, ImageFont

from .mats import PALETTE, TEX_OUT
from .mesh import Mesh

CELL = 512
COLS, ROWS = 4, 3
FONT_HAND = '/usr/share/fonts/truetype/freefont/FreeSansBoldOblique.ttf'
FONT_STENCIL = '/usr/share/fonts/truetype/dejavu/DejaVuSansMono-Bold.ttf'
FONT_CHALK = '/usr/share/fonts/truetype/freefont/FreeSansOblique.ttf'

# name: (column, row, width m, height m, kind, text, colour)
DESIGNS = {
    'no_exit':     (0, 0, 2.4, 0.9, 'spray', 'NO WAY OUT', (0.45, 0.07, 0.05)),
    'stay_alive':  (1, 0, 1.8, 0.8, 'spray', 'STAY ALIVE', (0.05, 0.05, 0.06)),
    'les_91':      (2, 0, 1.5, 0.7, 'spray', 'LES 91', (0.1, 0.16, 0.36)),
    'eddie':       (3, 0, 1.3, 0.6, 'chalk', 'EDDIE\nWAS HERE', (0.85, 0.84, 0.8)),
    'radiation':   (0, 1, 1.2, 1.2, 'stencil', 'RADIATION', (0.72, 0.58, 0.12)),
    'arrow_clear': (1, 1, 1.4, 0.7, 'arrow', 'CLEAR', (0.88, 0.88, 0.84)),
    'entrance':    (2, 1, 0.9, 0.6, 'stencil_num', '2\n11-20', (0.92, 0.9, 0.84)),
    'cross':       (3, 1, 0.8, 0.8, 'cross', '', (0.5, 0.06, 0.05)),
    'soot':        (0, 2, 2.2, 3.0, 'soot', '', (0.03, 0.025, 0.02)),
    'damp':        (1, 2, 2.6, 2.0, 'damp', '', (0.16, 0.13, 0.08)),
    'leak':        (2, 2, 1.4, 2.6, 'leak', '', (0.12, 0.1, 0.06)),
    'keep_out':    (3, 2, 2.0, 0.8, 'spray', 'KEEP OUT', (0.38, 0.05, 0.04)),
}
PALETTE['graffiti'] = dict(tex='graffiti', decal=True, tile=1.0, color=(1, 1, 1), rough=1.0, metal=0.0)


def _noise(rng, w, h, scale):
    """Smooth value noise 0..1 (bilinear upsampled random grid)."""
    g = rng.random((h // scale + 2, w // scale + 2)).astype(np.float32)
    im = Image.fromarray((g * 255).astype(np.uint8)).resize((w + scale * 2, h + scale * 2), Image.BICUBIC)
    return np.asarray(im, np.float32)[:h, :w] / 255


def _hand_text(text, font_path, size_px, rng, jitter=0.08):
    """Text drawn letter by letter with uneven size, angle and baseline (a hand with a spray can)."""
    lines = text.split('\n')
    font = ImageFont.truetype(font_path, size_px)
    W = int(size_px * 0.9 * max(len(l) for l in lines)) + size_px
    H = int(size_px * 1.25 * len(lines)) + size_px
    im = Image.new('L', (W, H), 0)
    y = size_px // 3
    for line in lines:
        x = size_px // 3
        for ch in line:
            s = int(size_px * rng.uniform(1 - jitter, 1 + jitter))
            f = ImageFont.truetype(font_path, s)
            g = Image.new('L', (int(s * 1.6), int(s * 1.6)), 0)
            ImageDraw.Draw(g).text((s * 0.2, s * 0.1), ch, font=f, fill=255)
            g = g.rotate(rng.uniform(-9, 9) * jitter / 0.08, resample=Image.BICUBIC)
            im.paste(255, (int(x), int(y + rng.uniform(-0.07, 0.07) * s)), g)
            x += font.getlength(ch) * rng.uniform(0.92, 1.05) if ch != ' ' else size_px * 0.45
        y += size_px * 1.15
    box = im.getbbox() or (0, 0, W, H)
    return im.crop(box)


def _fit(mask, w, h, margin=0.08):
    """Scale a mask image into a w x h canvas keeping its aspect, centred."""
    mw, mh = mask.size
    k = min(w * (1 - 2 * margin) / mw, h * (1 - 2 * margin) / mh)
    m = mask.resize((max(1, int(mw * k)), max(1, int(mh * k))), Image.BICUBIC)
    out = Image.new('L', (w, h), 0)
    out.paste(m, ((w - m.size[0]) // 2, (h - m.size[1]) // 2))
    return out


def _drips(a, rng, count, max_len):
    """Paint runs: thin streaks down from the lower edges of the strokes."""
    h, w = a.shape
    cols = np.where(a.max(axis=0) > 0.6)[0]
    if len(cols) == 0:
        return a
    out = a.copy()
    for _ in range(count):
        x = int(rng.choice(cols))
        ys = np.where(a[:, x] > 0.6)[0]
        y0 = int(ys.max())
        ln = int(rng.uniform(0.25, 1.0) * max_len)
        wdt = rng.uniform(1.2, 2.6)
        for dy in range(ln):
            y = y0 + dy
            if y >= h:
                break
            t = 1 - dy / ln
            for dx in range(-3, 4):
                xx = x + dx
                if 0 <= xx < w:
                    out[y, xx] = max(out[y, xx], 0.85 * t ** 0.6 * math.exp(-(dx / wdt) ** 2))
        if y0 + ln < h:   # the bead at the end of the run
            yy, xx = np.ogrid[-4:5, -4:5]
            bead = np.exp(-(xx ** 2 + yy ** 2) / 6.0) * 0.6
            y1, x1 = min(h - 5, y0 + ln), min(w - 5, max(4, x))
            out[y1 - 4:y1 + 5, x1 - 4:x1 + 5] = np.maximum(out[y1 - 4:y1 + 5, x1 - 4:x1 + 5], bead)
    return out


def _spray(mask, rng, drips=10):
    """Hard core + soft overspray + runs + wall roughness breaking the paint."""
    core = np.asarray(mask.filter(ImageFilter.GaussianBlur(1.6)), np.float32) / 255
    soft = np.asarray(mask.filter(ImageFilter.GaussianBlur(9)), np.float32) / 255
    a = np.clip(core * 1.15, 0, 1)
    a = np.maximum(a, soft * 0.35)
    a = _drips(a, rng, drips, mask.size[1] * 0.35)
    return a


def _stroke_mask(size, draw_fn, width):
    im = Image.new('L', size, 0)
    draw_fn(ImageDraw.Draw(im), width)
    return im


def _design(kind, text, rng, w, h):
    """Alpha (h x w float) of one design."""
    if kind == 'spray':
        return _spray(_fit(_hand_text(text, FONT_HAND, 160, rng), w, h, 0.1), rng, 14)
    if kind == 'chalk':
        m = _fit(_hand_text(text, FONT_CHALK, 140, rng, jitter=0.12), w, h, 0.1)
        a = np.asarray(m.filter(ImageFilter.GaussianBlur(0.8)), np.float32) / 255
        grain = rng.random(a.shape).astype(np.float32)
        return np.clip(a * (grain > 0.35) * 0.9, 0, 1)
    if kind == 'stencil':
        im = Image.new('L', (w, h), 0)
        d = ImageDraw.Draw(im)
        cx, cy, r = w / 2, h * 0.4, w * 0.3
        d.ellipse((cx - r * 0.18, cy - r * 0.18, cx + r * 0.18, cy + r * 0.18), fill=255)
        for k in range(3):           # trefoil blades with the stencil gap round the hub
            a0 = -90 + 120 * k - 30
            d.pieslice((cx - r, cy - r, cx + r, cy + r), a0, a0 + 60, fill=255)
        d.ellipse((cx - r * 0.3, cy - r * 0.3, cx + r * 0.3, cy + r * 0.3), fill=0)
        d.ellipse((cx - r * 0.18, cy - r * 0.18, cx + r * 0.18, cy + r * 0.18), fill=255)
        f = ImageFont.truetype(FONT_STENCIL, 64)
        tw = f.getlength(text)
        d.text((cx - tw / 2, h * 0.76), text, font=f, fill=255)
        for x in range(0, w, 37):    # stencil bridges through the letters
            d.line((x, h * 0.76, x, h * 0.92), fill=0, width=3)
        a = np.asarray(im.filter(ImageFilter.GaussianBlur(1.0)), np.float32) / 255
        soft = np.asarray(im.filter(ImageFilter.GaussianBlur(6)), np.float32) / 255
        return np.maximum(a * 0.95, soft * 0.18)
    if kind == 'stencil_num':        # entrance and flat numbers painted on the canopy wall (Soviet habit)
        im = Image.new('L', (w, h), 0)
        d = ImageDraw.Draw(im)
        top, bottom = text.split('\n')
        f1, f2 = ImageFont.truetype(FONT_STENCIL, 230), ImageFont.truetype(FONT_STENCIL, 96)
        d.text((w / 2 - f1.getlength(top) / 2, 10), top, font=f1, fill=255)
        d.text((w / 2 - f2.getlength(bottom) / 2, h * 0.62), bottom, font=f2, fill=255)
        a = np.asarray(im.filter(ImageFilter.GaussianBlur(1.0)), np.float32) / 255
        return a * 0.9
    if kind == 'arrow':
        def arrow(d, wd):
            y = h * 0.32
            d.line((w * 0.1, y + rng.uniform(-6, 6), w * 0.78, y), fill=255, width=wd)
            d.line((w * 0.78, y, w * 0.62, y - h * 0.16), fill=255, width=wd)
            d.line((w * 0.78, y, w * 0.63, y + h * 0.15), fill=255, width=wd)
        m = _stroke_mask((w, h), arrow, 26)
        t = _fit(_hand_text(text, FONT_HAND, 120, rng), w, int(h * 0.42), 0.08)
        m.paste(255, (0, int(h * 0.55)), t)
        return _spray(m, rng, 6)
    if kind == 'cross':
        def cross(d, wd):
            d.line((w * 0.18, h * 0.2, w * 0.82, h * 0.8), fill=255, width=wd)
            d.line((w * 0.8, h * 0.18, w * 0.2, h * 0.82), fill=255, width=wd)
            d.ellipse((w * 0.08, h * 0.08, w * 0.92, h * 0.92), outline=255, width=wd // 2)
        return _spray(_stroke_mask((w, h), cross, 34), rng, 8)
    yy, xx = np.mgrid[0:h, 0:w].astype(np.float32)
    u, v = xx / w - 0.5, 1 - yy / h            # v = 0 at the bottom
    n1, n2 = _noise(rng, w, h, 48), _noise(rng, w, h, 12)
    if kind == 'soot':               # smoke from a window that burnt out: widens and fades upwards
        width = 0.18 + 0.3 * v
        a = np.exp(-(u / width) ** 2 * 2.2) * np.clip(1.15 - v, 0, 1) ** 1.3
        a *= 0.55 + 0.45 * n1 + 0.15 * (n2 - 0.5)
        return np.clip(a * 1.2, 0, 0.95)
    if kind == 'damp':               # damp patch with a darker tide line
        r = np.sqrt((u * 1.1) ** 2 + ((v - 0.5) * 1.3) ** 2) + 0.18 * (n1 - 0.5)
        body = np.clip((0.42 - r) / 0.25, 0, 1) ** 1.5 * 0.3
        rim = np.exp(-((r - 0.42) / 0.035) ** 2) * 0.16
        return np.clip((body + rim) * (0.7 + 0.3 * n2), 0, 0.9)
    if kind == 'leak':               # water running down from a leaking roof or balcony slab
        a = np.zeros((h, w), np.float32)
        for _ in range(9):
            c = rng.uniform(-0.3, 0.3)
            sg = rng.uniform(0.02, 0.07)
            ln = rng.uniform(0.4, 1.0)
            a += rng.uniform(0.2, 0.5) * np.exp(-((u - c - 0.03 * np.sin(v * 9)) / sg) ** 2) * np.clip((v - (1 - ln)) / ln, 0, 1)
        a += 0.4 * np.exp(-(u / 0.35) ** 2) * np.clip((v - 0.8) / 0.2, 0, 1)
        return np.clip(a * (0.65 + 0.35 * n2), 0, 0.85)
    raise ValueError(kind)


def make_atlas(force=False):
    path = f'{TEX_OUT}/graffiti_albedo.png'
    if os.path.exists(path) and not force:
        return path
    rng = np.random.default_rng(91)
    W, H = COLS * CELL, ROWS * CELL
    A = np.zeros((H, W), np.float32)
    C = np.zeros((H, W, 3), np.float32)
    wall = _noise(rng, W, H, 6) * 0.5 + _noise(rng, W, H, 2) * 0.5     # paint caught by the brick and plaster
    for name, (col, row, wm, hm, kind, text, colour) in DESIGNS.items():
        # the design keeps the quad's aspect inside the square cell (height uses the whole cell)
        k = min(CELL / wm, CELL / hm)
        w, h = int(wm * k), int(hm * k)
        a = _design(kind, text, rng, w, h)
        x0, y0 = col * CELL + (CELL - w) // 2, row * CELL + (CELL - h) // 2
        A[y0:y0 + h, x0:x0 + w] = a
        C[row * CELL:(row + 1) * CELL, col * CELL:(col + 1) * CELL] = colour
    A = np.clip(A * 1.3, 0, 1) * (0.85 + 0.15 * np.clip(wall * 1.4 - 0.2, 0, 1))
    for c in range(COLS + 1):            # keep cell borders clear so mip levels never bleed
        A[:, max(0, c * CELL - 3):c * CELL + 3] = 0
    for r in range(ROWS + 1):
        A[max(0, r * CELL - 3):r * CELL + 3, :] = 0
    rgba = np.dstack([C, np.clip(A, 0, 1)])
    Image.fromarray(np.clip(rgba * 255 + 0.5, 0, 255).astype(np.uint8), 'RGBA').save(path)
    return path


def quad(name):
    """Decal model of one design (see module doc)."""
    col, row, wm, hm, *_ = DESIGNS[name]
    k = min(CELL / wm, CELL / hm)
    w, h = wm * k / CELL, hm * k / CELL           # used part of the cell (0..1)
    u0 = (col + (1 - w) / 2) / COLS
    u1 = (col + (1 + w) / 2) / COLS
    v0 = (row + (1 - h) / 2) / ROWS
    v1 = (row + (1 + h) / 2) / ROWS
    m = Mesh(PALETTE)
    m.poly('graffiti', [(-wm / 2, 0, 0), (wm / 2, 0, 0), (wm / 2, hm, 0), (-wm / 2, hm, 0)],
           uvs=[(u0, -v1), (u1, -v1), (u1, -v0), (u0, -v0)])
    return m
