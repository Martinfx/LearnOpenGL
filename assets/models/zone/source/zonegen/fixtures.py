"""Facade fixtures (original, CC0): the middle layer of detail that makes a wall read as a building someone built
and lived in (docs/LEVEL_ART_GUIDE.md, "Proč to působí amatérsky", point 4).

- downpipe(h): a rain-water downpipe of height h metres: a rusty sheet-metal pipe 11 cm across held 12 cm off the
  wall by clamps every 2 m, a hopper head under the roof edge and a shoe at the bottom turned away from the wall.
  The wall is the plane z = 0, the pipe stands in front of it (+Z), origin at the foot.
- plates: enamel house-number plates and street signs of a Soviet town (blue and white), one RGBA atlas
  (zone/textures/plates_albedo.png) and one decal quad per plate, same convention as the graffiti (XY plane facing
  +Z, origin bottom centre).
The level script assets/levels/source/add_fixtures.py places them where the wall is really flat (ray test)."""
import math
import os

import numpy as np
from PIL import Image, ImageDraw, ImageFilter, ImageFont

from .mats import PALETTE, TEX_OUT
from .mesh import Mesh

PIPE_HEIGHTS = (3, 6, 9, 12, 15, 18, 24, 30)
R = 0.055                   # pipe radius
OFF = 0.12                  # wall to pipe axis


def downpipe(h):
    m = Mesh(PALETTE)
    top = h - 0.35
    m.cylinder('rust_dark', (0, 0.18, OFF), (0, top, OFF), R, seg=10, caps=False)
    # shoe: a short bend out of the wall at the foot
    m.cylinder('rust_dark', (0, 0.18, OFF), (0, 0.06, OFF + 0.16), R, seg=10, caps=True)
    # hopper head under the roof edge, and its outlet into the pipe
    m.box('rust_dark', (-0.16, top + 0.05, 0.02), (0.16, h, OFF + 0.14))
    m.cylinder('rust_dark', (0, top - 0.02, OFF), (0, top + 0.06, OFF), R * 1.15, seg=10, caps=False)
    # clamps every 2 m: a collar and a flat bracket into the wall
    y = 1.2
    while y < top - 0.3:
        m.cylinder('rust_dark', (0, y - 0.03, OFF), (0, y + 0.03, OFF), R * 1.25, seg=10, caps=True)
        m.box('rust_dark', (-0.015, y - 0.02, 0.0), (0.015, y + 0.02, OFF - R))
        y += 2.0
    return m


# name: (text, colours (background, text), width m, height m)
BLUE, WHITE, INK = (0.12, 0.25, 0.52), (0.92, 0.93, 0.9), (0.1, 0.12, 0.2)
NYC_GREEN = (0.08, 0.36, 0.22)       # New York street name signs: white letters on green
PLATES = {f'house_{n}': (str(n), (BLUE, WHITE), 0.36 if n < 10 else 0.42, 0.3) for n in range(1, 25)}
PLATES.update({
    'street_zavod':  ('HESTER ST', (NYC_GREEN, WHITE), 1.0, 0.24),
    'street_mira':   ('GRAND ST', (NYC_GREEN, WHITE), 0.8, 0.24),
    'street_lenin':  ('ORCHARD ST', (NYC_GREEN, WHITE), 0.9, 0.24),
    'street_novaya': ('ELDRIDGE ST', (NYC_GREEN, WHITE), 0.9, 0.24),
    'street_pochta': ('MULBERRY ST', (NYC_GREEN, WHITE), 1.0, 0.24),
    'shelter':       ('FALLOUT SHELTER', ((0.82, 0.2, 0.12), (0.95, 0.94, 0.9)), 0.7, 0.3),
})
STREETS = [k for k in PLATES if k.startswith('street_')]
PCOLS, PROWS, PCELL_W, PCELL_H = 4, 8, 512, 160
PALETTE['plates'] = dict(tex='plates', decal=True, tile=1.0, color=(1, 1, 1), rough=0.6, metal=0.0)


def make_plate_atlas(force=False):
    path = f'{TEX_OUT}/plates_albedo.png'
    if os.path.exists(path) and not force:
        return path
    rng = np.random.default_rng(7)
    W, H = PCOLS * PCELL_W, PROWS * PCELL_H
    atlas = Image.new('RGBA', (W, H), (0, 0, 0, 0))
    font_path = '/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf'
    for k, (name, (text, (bg, fg), wm, hm)) in enumerate(PLATES.items()):
        col, row = k % PCOLS, k // PCOLS
        # the plate fills its cell's width or height, keeping its aspect
        s = min((PCELL_W - 16) / wm, (PCELL_H - 16) / hm)
        w, h = int(wm * s), int(hm * s)
        im = Image.new('RGBA', (w, h), (0, 0, 0, 0))
        d = ImageDraw.Draw(im)
        bgc = tuple(int(c * 255) for c in bg) + (255,)
        fgc = tuple(int(c * 255) for c in fg) + (255,)
        d.rounded_rectangle((0, 0, w - 1, h - 1), radius=int(h * 0.12), fill=bgc)
        d.rounded_rectangle((4, 4, w - 5, h - 5), radius=int(h * 0.1), outline=fgc, width=max(2, h // 30))
        size = int(h * 0.62)
        f = ImageFont.truetype(font_path, size)
        while d.textlength(text, font=f) > w * 0.86 and size > 8:
            size -= 2
            f = ImageFont.truetype(font_path, size)
        tw = d.textlength(text, font=f)
        d.text(((w - tw) / 2, (h - size) / 2 - size * 0.08), text, font=f, fill=fgc)
        # decades outside: chipped enamel (rusty spots), faded, dirty bottom edge
        a = np.asarray(im, np.float32) / 255
        yy, xx = np.mgrid[0:h, 0:w]
        for _ in range(int(rng.integers(3, 9))):
            cx, cy, r = rng.random() * w, rng.random() * h, rng.uniform(2, h * 0.06)
            m = np.hypot(xx - cx, yy - cy) < r
            a[m, :3] = a[m, :3] * 0.3 + np.array([0.33, 0.18, 0.08]) * 0.7
        a[..., :3] = a[..., :3] * 0.85 + 0.08
        dirt = np.clip((yy / h - 0.6) / 0.4, 0, 1)[..., None]
        a[..., :3] = a[..., :3] * (1 - 0.35 * dirt) + np.array([0.2, 0.17, 0.12]) * 0.35 * dirt
        im = Image.fromarray(np.clip(a * 255, 0, 255).astype(np.uint8), 'RGBA')
        atlas.alpha_composite(im, (col * PCELL_W + (PCELL_W - w) // 2, row * PCELL_H + (PCELL_H - h) // 2))
    atlas.save(path)
    return path


def plate(name):
    """Decal quad of one plate (see module doc)."""
    k = list(PLATES).index(name)
    col, row = k % PCOLS, k // PCOLS
    _t, _c, wm, hm = PLATES[name]
    s = min((PCELL_W - 16) / wm, (PCELL_H - 16) / hm)
    w, h = wm * s, hm * s
    W, H = PCOLS * PCELL_W, PROWS * PCELL_H
    u0 = (col * PCELL_W + (PCELL_W - w) / 2) / W
    u1 = (col * PCELL_W + (PCELL_W + w) / 2) / W
    v0 = (row * PCELL_H + (PCELL_H - h) / 2) / H
    v1 = (row * PCELL_H + (PCELL_H + h) / 2) / H
    m = Mesh(PALETTE)
    m.poly('plates', [(-wm / 2, 0, 0), (wm / 2, 0, 0), (wm / 2, hm, 0), (-wm / 2, hm, 0)],
           uvs=[(u0, -v1), (u1, -v1), (u1, -v0), (u0, -v0)])
    return m


CABLE_SPANS = (6, 10, 14, 18, 24, 32)


def cable(span, sag=0.045, r=0.016, seed=0):
    """A sagging cable between x = -span/2 and +span/2 at y = 0 (parabolic catenary, sag `sag` x span), with a
    porcelain-less steel clamp at each end. Uniform scale keeps the shape."""
    m = Mesh(PALETTE)
    n = max(8, int(span * 1.2))
    pts = []
    for i in range(n + 1):
        t = -1.0 + 2.0 * i / n
        pts.append((t * span / 2, -sag * span * (1.0 - t * t), 0.0))
    for a, b in zip(pts, pts[1:]):
        m.cylinder('rubber', a, b, r, seg=5, caps=False)
    for sx in (-1, 1):
        m.box('rust_dark', (sx * span / 2 - 0.06, -0.04, -0.04), (sx * span / 2 + 0.06, 0.04, 0.04))
    return m


def kerb(length=6.0, seed=0):
    """A run of 1 m granite-grey concrete kerb stones along X: the road side faces +Z at z = 0, top at y = 0.17,
    0.15 m wide into -Z, 0.35 m deep. Decades of frost: stones sit a little crooked, a few are cracked off or gone."""
    rng = np.random.default_rng(seed)
    m = Mesh(PALETTE)
    n = int(round(length))
    for i in range(n):
        if rng.random() < 0.06:
            continue
        x0 = -length / 2 + i * length / n + 0.006
        x1 = x0 + length / n - 0.012
        dy = rng.uniform(-0.03, 0.015)
        dz = rng.uniform(-0.02, 0.025)
        top = 0.17 + dy
        if rng.random() < 0.1:                         # a broken stone: lower, a chunk missing at one end
            top -= rng.uniform(0.04, 0.09)
            cut = rng.uniform(0.2, 0.5)
            if rng.random() < 0.5:
                x0 += cut
            else:
                x1 -= cut
        m.box('concrete', (x0, -0.2, -0.15 + dz), (x1, top, 0.0 + dz))
        m.box('concrete', (x0, top - 0.02, -0.01 + dz), (x1, top, 0.01 + dz))     # chamfer lip
    return m


# Shop signs of the ground floor: one long panel over a shop front, big condensed letters as the town's trade
# organisation painted them, faded for decades, a letter fallen off here and there.
# name: (text, background, letters, width m)
RED, GREEN, CREAM, DARK = (0.55, 0.12, 0.08), (0.16, 0.3, 0.2), (0.86, 0.82, 0.7), (0.14, 0.15, 0.17)
SIGNS = {
    'gastronom':  ('DELICATESSEN', RED, CREAM, 5.4),
    'khleb':      ('BAKERY', CREAM, RED, 2.8),
    'moloko':     ('DAIRY', BLUE, WHITE, 3.6),
    'ovoshchi':   ('FRUIT  PRODUCE', GREEN, CREAM, 5.6),
    'apteka':     ('PHARMACY', CREAM, GREEN, 3.6),
    'parikmakh':  ('BARBER SHOP', BLUE, WHITE, 5.8),
    'knigi':      ('BOOKS', RED, CREAM, 3.2),
    'obuv':       ('SHOES', DARK, CREAM, 3.2),
    'pochta':     ('POST OFFICE', BLUE, WHITE, 5.8),
    'produkty':   ('GROCERY', RED, WHITE, 4.6),
    'galantereya': ('DRY GOODS', GREEN, WHITE, 5.0),
    'remont':     ('WATCH REPAIR', DARK, (0.8, 0.62, 0.25), 5.0),
    'stolovaya':  ('DINER', CREAM, RED, 4.4),
    'univermag':  ('DEPARTMENT STORE', RED, CREAM, 5.4),
    'tkani':      ('FABRICS', BLUE, CREAM, 3.2),
    'khoztovary': ('HARDWARE', GREEN, CREAM, 5.0),
}
SIGN_H = 0.55
SCOLS, SROWS, SCELL_W, SCELL_H = 2, 8, 1024, 128
PALETTE['signs'] = dict(tex='signs', decal=True, tile=1.0, color=(1, 1, 1), rough=0.75, metal=0.0)


def make_sign_atlas(force=False):
    path = f'{TEX_OUT}/signs_albedo.png'
    if os.path.exists(path) and not force:
        return path
    rng = np.random.default_rng(11)
    atlas = Image.new('RGBA', (SCOLS * SCELL_W, SROWS * SCELL_H), (0, 0, 0, 0))
    font_path = '/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf'
    for k, (name, (text, bg, fg, wm)) in enumerate(SIGNS.items()):
        col, row = k % SCOLS, k // SCOLS
        w, h = SCELL_W - 8, SCELL_H - 8
        bgc = tuple(int(c * 255) for c in bg) + (255,)
        im = Image.new('RGBA', (w, h), bgc)
        # letters drawn wide, then squeezed into the panel: the tall condensed lettering of Soviet shop signs
        size = int(h * 0.78)
        f = ImageFont.truetype(font_path, size)
        tmp = Image.new('L', (int(f.getlength(text)) + 20, h), 0)
        ImageDraw.Draw(tmp).text((10, (h - size) / 2 - size * 0.12), text, font=f, fill=255)
        # natural letter shapes squeezed to 80 % width on the real panel (cell pixels are not square on it)
        px_per_m_x, px_per_m_y = w / wm, h / SIGN_H
        tw = min(int(w * 0.92), int(tmp.width * px_per_m_x / px_per_m_y * 0.8))
        mask = np.asarray(tmp.resize((tw, h), Image.LANCZOS), np.float32) / 255
        # a letter or two fell off: only the paler, cleaner ghost of it stays on the panel
        cols = np.where(mask.max(0) > 0.3)[0]
        ghost = np.ones_like(mask)
        if len(cols) and rng.random() < 0.7:
            gaps = np.split(cols, np.where(np.diff(cols) > 2)[0] + 1)
            for g in rng.choice(len(gaps), size=min(len(gaps), int(rng.integers(1, 3))), replace=False):
                ghost[:, gaps[g][0]:gaps[g][-1] + 1] = 0.25
        a = np.asarray(im, np.float32) / 255
        x0 = (w - tw) // 2
        fgv = np.array(fg, np.float32)
        lm = (mask * ghost)[..., None]
        a[:, x0:x0 + tw, :3] = a[:, x0:x0 + tw, :3] * (1 - lm) + fgv * lm
        ghost_m = (mask * (1 - ghost) * 0.25)[..., None]
        a[:, x0:x0 + tw, :3] = a[:, x0:x0 + tw, :3] * (1 - ghost_m) + np.array([0.9, 0.9, 0.85]) * ghost_m
        # decades of sun and rain: faded, rust running from the fixings, dirt collected along the bottom edge
        yy, xx = np.mgrid[0:h, 0:w]
        a[..., :3] = a[..., :3] * 0.72 + 0.16 * np.array([0.92, 0.9, 0.84])
        for _ in range(int(rng.integers(3, 7))):
            cx, length = rng.uniform(0, w), rng.uniform(0.3, 1.0) * h
            run = np.exp(-((xx - cx) / rng.uniform(2, 6)) ** 2) * (yy < length) * (1 - yy / max(length, 1)) * 0.75
            a[..., :3] = a[..., :3] * (1 - run[..., None]) + np.array([0.36, 0.2, 0.09]) * run[..., None]
        blot = np.asarray(Image.fromarray((rng.random((5, 40)) * 255).astype(np.uint8))
                          .resize((w, h), Image.BICUBIC), np.float32) / 255
        flake = np.clip((blot - 0.86) / 0.04, 0, 1)[..., None] * 0.55
        a[..., :3] = a[..., :3] * (1 - flake) + np.array([0.42, 0.4, 0.37]) * flake       # paint gone, bare sheet
        dirt = np.clip((yy / h - 0.55) / 0.45, 0, 1)[..., None] * 0.4
        a[..., :3] = a[..., :3] * (1 - dirt) + np.array([0.2, 0.17, 0.12]) * dirt
        atlas.alpha_composite(Image.fromarray(np.clip(a * 255, 0, 255).astype(np.uint8), 'RGBA'),
                              (col * SCELL_W + 4, row * SCELL_H + 4))
    atlas.save(path)
    return path


def sign(name, flat=False, m=None):
    """A shop sign over a shop front: a sheet-metal box 12 cm deep on the wall (z = 0, facing +Z), the lettered panel
    on its face; origin at the bottom centre. `flat` leaves out the box (far LOD). Drawn into `m` (with its current
    transform) when given."""
    k = list(SIGNS).index(name)
    col, row = k % SCOLS, k // SCOLS
    wm = SIGNS[name][3]
    W, H = SCOLS * SCELL_W, SROWS * SCELL_H
    u0, u1 = (col * SCELL_W + 4) / W, (col * SCELL_W + SCELL_W - 4) / W
    v0, v1 = (row * SCELL_H + 4) / H, (row * SCELL_H + SCELL_H - 4) / H
    m = m if m is not None else Mesh(PALETTE)
    d = 0.12
    if not flat:
        m.box('rust_dark', (-wm / 2, 0.0, 0.0), (wm / 2, SIGN_H, d - 0.005), faces='xXyYZ')
    m.poly('signs', [(-wm / 2, 0, d), (wm / 2, 0, d), (wm / 2, SIGN_H, d), (-wm / 2, SIGN_H, d)],
           uvs=[(u0, -v1), (u1, -v1), (u1, -v0), (u0, -v0)])
    return m
