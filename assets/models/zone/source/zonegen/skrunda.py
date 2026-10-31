"""Buildings of Skrunda-1 (Mezaine, Latvia), the abandoned Soviet radar town, built to the real
footprints from OpenStreetMap (see assets/levels/source/osm/). CC0, part of the engine repo.

Every building is enterable: real storeys (3 m), stairwells, doors 1.0 x 2.1 m, flats/rooms.
Materials are Poly Haven photo scans (silicate brick, plaster, concrete) packed by mats.py.

Local frame of every building: long side along X, entrances on +Z, ground at y = 0
(the level places it on the terrain). The plinth reaches 0.6 m below ground for uneven terrain.
"""
import math
import os
import numpy as np
from PIL import Image, ImageDraw
from .mesh import Mesh, rot_y
from . import tex
from .mats import PALETTE, TEX_OUT, _font, _weather, _flat_maps
from .buildings import wall_x, wall_z, slab, stair_flight, railing_x, railing_z, stairwell, STOREY, SLAB, DOOR_H

BASE = 0.6          # ground floor level above the ground (raised plinth, as on the real blocks)


# --------------------------------------------------------------------------- signs
SIGN_ROWS = ['ГОСПИТАЛЬ', 'ТОРГОВЫЙ ДОМ «ВЕНТА»', 'СТОЛОВАЯ', 'ул. Новая', 'ШТАБ', 'ОПАСНО ДЛЯ ЖИЗНИ',
             'КОТЕЛЬНАЯ', 'ВЪЕЗД ЗАПРЕЩЁН']


def make_signs():
    """Sign atlas, 8 rows of 2048 x 256. Real Russian names used in Skrunda-1 (shop "Venta",
    Novaya street) and generic Soviet signage."""
    path = f'{TEX_OUT}/skrunda_signs_albedo.jpg'
    if os.path.exists(path):
        return
    W, H = 2048, 2048
    im = Image.new('RGB', (W, H), (50, 50, 50))
    d = ImageDraw.Draw(im)
    styles = [((225, 222, 210), (150, 30, 25)), ((30, 60, 110), (235, 235, 230)), ((215, 205, 180), (40, 40, 40)),
              ((25, 70, 140), (240, 240, 240)), ((150, 30, 25), (240, 230, 210)), ((235, 200, 30), (20, 20, 20)),
              ((200, 200, 195), (30, 30, 30)), ((235, 235, 230), (170, 25, 25))]
    for i, text in enumerate(SIGN_ROWS):
        bg, fg = styles[i]
        y0 = i * 256
        d.rectangle([0, y0, W, y0 + 256], fill=bg)
        d.rectangle([10, y0 + 10, W - 10, y0 + 246], outline=fg, width=8)
        f = _font(150 if len(text) < 14 else 110)
        tw = d.textlength(text, font=f)
        d.text(((W - tw) / 2, y0 + 40 if len(text) < 14 else y0 + 62), text, font=f, fill=fg)
    im = _weather(im, 31, 0.1)          # light: signs must stay readable from the street
    im.save(path, quality=88)
    _flat_maps('skrunda_signs', rough=0.7, metal=0.1, size=(256, 256))


PALETTE['skrunda_signs'] = dict(tex='skrunda_signs', tile=1.0, color=(1, 1, 1), rough=1.0, metal=1.0)


def sign(m, row, x0, x1, y0, y1, z, facing=1, frac=1.0):
    """Quad with one atlas row; frac crops the row width (short texts)."""
    v0, v1 = row / 8, (row + 1) / 8
    u0, u1 = 0.5 - frac / 2, 0.5 + frac / 2
    if facing > 0:
        m.poly('skrunda_signs', [(x0, y0, z), (x1, y0, z), (x1, y1, z), (x0, y1, z)],
               uvs=[(u0, -v1), (u1, -v1), (u1, -v0), (u0, -v0)])
    else:
        m.poly('skrunda_signs', [(x1, y0, z), (x0, y0, z), (x0, y1, z), (x1, y1, z)],
               uvs=[(u0, -v1), (u1, -v1), (u1, -v0), (u0, -v0)])


# --------------------------------------------------------------------------- grime decals
def make_grime():
    """RGBA atlas of weathering decals (rain streaks under sills, drips from the roof edge, splash dirt at the
    plinth), like on the sand-lime brick walls in the Skrunda-1 photos. Top half: 4 sill streaks (the last
    one rusty from the tin sill); bottom left: roof-edge drips; bottom right: plinth splash band."""
    path = f'{TEX_OUT}/grime_albedo.png'
    if os.path.exists(path):
        return
    rng = np.random.default_rng(77)
    N = 1024
    A = np.zeros((N, N), np.float32)
    C = np.zeros((N, N, 3), np.float32)
    C[:] = (0.15, 0.14, 0.12)
    yy, xx = np.mgrid[0:N, 0:N].astype(np.float32)
    noise = tex.noise_mask(5, 40, 0.0, 1.0)(N, N)[:N, :N] if hasattr(tex, 'noise_mask') else np.ones((N, N))
    noise = 0.55 + 0.45 * np.clip(noise, 0, 1)

    def drips(x0, x1, y0, y1, count, wide):
        h = y1 - y0
        reg = np.zeros((h, x1 - x0), np.float32)
        lx = np.arange(x1 - x0, dtype=np.float32)[None, :]
        ly = np.arange(h, dtype=np.float32)[:, None]
        if wide:
            cx = (x1 - x0) / 2
            reg += 0.55 * np.exp(-((lx - cx) / ((x1 - x0) * 0.28)) ** 2) * np.clip(1 - ly / (h * 0.45), 0, 1) ** 1.5
        for _ in range(count):
            c = rng.uniform(0.1, 0.9) * (x1 - x0)
            sg = rng.uniform(2.0, 9.0)
            ln = rng.uniform(0.3, 1.0) * h
            reg += rng.uniform(0.25, 0.6) * np.exp(-((lx - c) / sg) ** 2) * np.clip(1 - ly / ln, 0, 1) ** 0.8
        A[y0:y1, x0:x1] = np.maximum(A[y0:y1, x0:x1], np.clip(reg, 0, 1))
    for k in range(4):
        drips(k * 256 + 8, (k + 1) * 256 - 8, 0, 512, 9, True)
        if k == 3:
            C[0:512, 768:1024] = (0.33, 0.19, 0.1)
    drips(0, 512, 512, 1024, 40, False)
    # plinth splash: dense at the bottom, ragged top edge
    band = np.clip((yy[512:, 512:] - 512 - 120 - 90 * noise[512:, 512:]) / 260, 0, 1) ** 1.2
    A[512:, 512:] = 0.75 * band
    A = A * noise * 0.85
    # soften the region borders so neighbours never bleed through mip levels
    for x in (0, 256, 512, 768):            # sill streaks: 4 cells
        A[:512, x:x + 4] = 0
        A[:512, x + 252:x + 256] = 0
    for x in (0, 512):                      # roof drips and splash: 2 cells
        A[512:, x:x + 4] = 0
        A[512:, x + 508:x + 512] = 0
    rgba = np.dstack([C, A])
    Image.fromarray(np.clip(rgba * 255 + 0.5, 0, 255).astype(np.uint8), 'RGBA').save(path)


PALETTE['grime'] = dict(tex='grime', decal=True, tile=1.0, color=(1, 1, 1), rough=1.0, metal=0.0)


def grime(m, x0, x1, y_top, y_bot, z, region, k=0):
    """Decal quad on a wall face at z (canonical frame: wall in the XY plane, +Z outwards). region:
    'sill' (variant k 0..3), 'roof' or 'plinth'."""
    if y_bot >= y_top or x1 <= x0:
        return
    if region == 'sill':
        u0, u1, v0, v1 = k / 4, (k + 1) / 4, 0.0, 0.5
    elif region == 'roof':
        u0, u1, v0, v1 = 0.0, 0.5, 0.5, 1.0
    else:
        u0, u1, v0, v1 = 0.5, 1.0, 0.5, 1.0
    m.poly('grime', [(x0, y_bot, z), (x1, y_bot, z), (x1, y_top, z), (x0, y_top, z)],
           uvs=[(u0, -v1), (u1, -v1), (u1, -v0), (u0, -v0)])


def facade_grime(m, x0, x1, top, z, rng, plinth=None):
    """Roof-edge drips along a wall from x0 to x1 (canonical frame, face at z) and, with plinth=(y0, y1, z),
    splash dirt on the plinth."""
    x = x0
    while x < x1 - 0.5:
        w = min(rng.uniform(1.6, 3.4), x1 - x)
        if rng.random() < 0.7:
            grime(m, x, x + w, top, top - rng.uniform(1.0, 3.2), z, 'roof')
        if plinth:
            grime(m, x, x + w, plinth[1], plinth[0], plinth[2], 'plinth')
        x += w


# --------------------------------------------------------------------------- facade details
def _frame(m, rot, cx, z_out, y_lo, y_hi, w, rng, kind, frame_mat='wood', door=False):
    """Window/door filling in an opening of width w between y_lo..y_hi. The outer wall face is at
    z_out in the frame given by rot (rot maps canonical +Z = outwards). kind: 'glass', 'broken',
    'empty', 'boarded'."""
    m.push((cx, 0, z_out), rot)
    if door and kind == 'boarded':
        kind = 'empty'           # doorways stay passable
    hw = w / 2
    zf0, zf1 = -0.17, -0.10          # frame depth inside the reveal
    fw = 0.065
    if kind != 'empty':
        m.box(frame_mat, (-hw, y_lo, zf0), (-hw + fw, y_hi, zf1))
        m.box(frame_mat, (hw - fw, y_lo, zf0), (hw, y_hi, zf1))
        m.box(frame_mat, (-hw + fw, y_hi - fw, zf0), (hw - fw, y_hi, zf1))
        if not door:
            m.box(frame_mat, (-hw + fw, y_lo, zf0), (hw - fw, y_lo + fw, zf1))
            if w > 1.0:
                m.box(frame_mat, (-0.03, y_lo + fw, zf0), (0.03, y_hi - fw, zf1))      # mullion
            ty = y_lo + (y_hi - y_lo) * 0.72
            m.box(frame_mat, (-hw + fw, ty - 0.03, zf0), (hw - fw, ty + 0.03, zf1))     # transom
    if not door:
        m.box('rust', (-hw - 0.04, y_lo - 0.035, -0.12), (hw + 0.04, y_lo, 0.07))       # tin sill
        if rng.random() < 0.85:      # rain streak washed down from the sill
            sw = w * rng.uniform(0.8, 1.15)
            grime(m, -sw / 2, sw / 2, y_lo - 0.035, max(0.05, y_lo - rng.uniform(0.7, 2.4)), 0.025, 'sill',
                  int(rng.integers(0, 4)))
    zg = (zf0 + zf1) / 2
    if kind == 'glass' and not door:
        m.poly('glass', [(-hw + fw, y_lo + fw, zg), (hw - fw, y_lo + fw, zg), (hw - fw, y_hi - fw, zg), (-hw + fw, y_hi - fw, zg)])
    elif kind == 'broken' and not door:
        # a few shards stuck in the corners of the panes
        for sx in (-1, 1):
            if rng.random() < 0.7:
                xa = sx * (hw - fw)
                ya = y_lo + fw if rng.random() < 0.6 else y_hi - fw
                dy = (y_hi - y_lo) * rng.uniform(0.15, 0.4) * (1 if ya < (y_lo + y_hi) / 2 else -1)
                dx = -sx * hw * rng.uniform(0.3, 0.8)
                m.poly('glass', [(xa, ya, zg), (xa + dx, ya, zg), (xa, ya + dy, zg)][::(1 if sx * dy > 0 else -1)])
    elif kind == 'boarded':
        n = int((y_hi - y_lo) / 0.22)
        for i in range(n):
            if rng.random() < 0.18:
                continue
            y = y_lo + 0.05 + i * (y_hi - y_lo - 0.1) / max(1, n)
            tilt = rng.uniform(-0.04, 0.04)
            m.box('wood', (-hw - 0.08, y + tilt, -0.02), (hw + 0.08, y + 0.17 + tilt, 0.01))
    m.pop()


def _rot_for(side):
    """Rotation mapping canonical +Z (outwards) to side: '+z', '-z', '+x', '-x'."""
    return {'+z': rot_y(0), '-z': rot_y(180), '+x': rot_y(90), '-x': rot_y(-90)}[side]


def _window_kind(rng, floor, p_glass=0.3):
    """Skrunda-1 photos: almost every window is a black hole without glass, frames often gone too;
    a few ground-floor openings boarded."""
    r = rng.random()
    if floor == 0 and r < 0.12:
        return 'boarded'
    if r < p_glass * 0.2:
        return 'glass'
    if r < 0.5:
        return 'broken'
    return 'empty'


def _drainpipe(m, x, z, top, side_z):
    m.cylinder('rust', (x, -0.1, z), (x, top - 0.25, z), 0.055, 6)
    m.cylinder('rust', (x, top - 0.25, z), (x, top + 0.1, z - side_z * 0.25), 0.055, 6)


def _antenna(m, x, y, z, rng):
    h = rng.uniform(2.0, 3.5)
    m.cylinder('rust', (x, y, z), (x + rng.uniform(-0.2, 0.2), y + h, z + rng.uniform(-0.2, 0.2)), 0.025, 5, caps=False)
    for k in range(rng.integers(2, 5)):
        yy = y + h * (0.55 + 0.12 * k)
        ln = 1.2 - 0.18 * k
        a = rng.uniform(0, math.pi)
        dx, dz = math.cos(a) * ln / 2, math.sin(a) * ln / 2
        m.cylinder('rust', (x - dx, yy, z - dz), (x + dx, yy, z + dz), 0.012, 4, caps=False)


def _junk(m, rng, x0, x1, z0, z1, y, n):
    """Furniture remains and debris on a floor (boxes, mattresses, fallen cupboard, plaster heaps)."""
    mx, mz = min(0.5, (x1 - x0) * 0.3), min(0.5, (z1 - z0) * 0.3)
    if x1 - x0 < 0.6 or z1 - z0 < 0.6:
        return
    for _ in range(n):
        x, z = rng.uniform(x0 + mx, x1 - mx), rng.uniform(z0 + mz, z1 - mz)
        k = rng.random()
        m.push((x, y, z), rot_y(rng.uniform(0, 180)))
        if k < 0.3:
            m.box('wood_dark', (-0.5, 0, -0.25), (0.5, rng.uniform(0.8, 1.9), 0.25))          # cupboard
        elif k < 0.5:
            m.box('wood_dark', (-0.9, 0, -0.3), (0.9, 0.35, 0.3))                             # fallen cupboard
        elif k < 0.7:
            m.box('paint_cream', (-0.45, 0, -0.95), (0.45, 0.15, 0.95))                       # mattress
        elif k < 0.85:
            m.box('wood', (-0.4, 0.42, -0.35), (0.4, 0.46, 0.35))                              # table
            for lx in (-0.35, 0.35):
                for lz in (-0.3, 0.3):
                    m.box_c('wood', (lx, 0.21, lz), (0.04, 0.42, 0.04))
        else:
            rng.uniform(0.4, 0.9)       # was a 3-sided debris pyramid (read as fake); real rubble clutter is placed by the level
        m.pop()


# --------------------------------------------------------------------------- 5-storey block
def block(length=47.4, sections=4, floors=5, depth=12.0, seed=0, lod=0, ext='silicate'):
    """Five-storey Khrushchev-era brick block (series 1-447 type) like the 13 blocks of Skrunda-1.
    Entrances on +Z, balconies on -Z. Each section: stairwell + two flats per floor."""
    rng = np.random.default_rng(seed)
    m = Mesh(PALETTE)
    SW, D = length / sections, depth
    k = SW / 13.0
    hx, hz = length / 2, D / 2
    t = 0.45
    top = floors * STOREY
    inn, stair = 'wallpaper', 'stair_paint'
    m.push((0, BASE - 0.05, 0))
    holes_all = []
    for s in range(sections):
        cx = -hx + SW * (s + 0.5)
        m.push((cx, 0, hz - 6.2))
        stairwell(m, floors, ext, lod)
        m.pop()
        holes_all.append((cx - 1.32, hz - 6.2 + 0.4, cx + 1.32, hz - 6.2 + 4.3))
    for f in range(floors + 1):
        y = 0.05 + f * STOREY
        holes = holes_all if 0 < f < floors else []
        fm = 'roof_tar' if f == floors else ('concrete_floor' if f == 0 else 'wood_floor')
        slab(m, (-hx + t / 2, -hz + t / 2, hx - t / 2, hz - t / 2), holes, y, SLAB if f else 0.3, fm, 'plaster', inn)
    win = []    # (side, x_or_z centre, width, y0, y1, floor, door)
    for f in range(floors):
        y0 = 0.05 + f * STOREY - (SLAB if f else 0.3)
        y1 = 0.05 + (f + 1) * STOREY - SLAB if f < floors - 1 else top + 0.75
        yf = 0.05 + f * STOREY
        front, back, left, right = [], [], [], []
        for s in range(sections):
            cx = -hx + SW * (s + 0.5)
            if f == 0:
                front.append((cx, 1.2, yf, yf + DOOR_H + 0.1))
                win.append(('+z', cx, 1.2, yf, yf + DOOR_H + 0.1, f, True))
            elif f < floors - 1:
                # stairwell window at the half landing between this floor and the next one
                front.append((cx, 1.0, yf + 1.55, yf + 2.65))
                win.append(('+z', cx, 1.0, yf + 1.55, yf + 2.65, f, False))
            for wx in (-4.6, -2.5, 2.5, 4.6):
                ww = 1.5 if abs(wx) > 3 else 1.2
                front.append((cx + wx * k, ww, yf + 0.85, yf + 2.3))
                win.append(('+z', cx + wx * k, ww, yf + 0.85, yf + 2.3, f, False))
            for wx in (-4.3, -1.6, 1.6, 4.3):
                if f > 0 and abs(wx) == 4.3:
                    back.append((cx + wx * k, 0.8, yf, yf + DOOR_H + 0.15))
                    win.append(('-z', cx + wx * k, 0.8, yf, yf + DOOR_H + 0.15, f, True))
                    bwx = cx + wx * k + (1.0 if wx > 0 else -1.0)
                    back.append((bwx, 1.0, yf + 0.85, yf + 2.3))
                    win.append(('-z', bwx, 1.0, yf + 0.85, yf + 2.3, f, False))
                else:
                    back.append((cx + wx * k, 1.5, yf + 0.85, yf + 2.3))
                    win.append(('-z', cx + wx * k, 1.5, yf + 0.85, yf + 2.3, f, False))
        for wz in (-3.0, 3.0):
            left.append((wz, 1.2, yf + 0.85, yf + 2.3))
            right.append((wz, 1.2, yf + 0.85, yf + 2.3))
            win.append(('-x', wz, 1.2, yf + 0.85, yf + 2.3, f, False))
            win.append(('+x', wz, 1.2, yf + 0.85, yf + 2.3, f, False))
        wall_x(m, -hx, hx, hz - t / 2, y0, y1, t, ext, inn, front, out_side=1)
        wall_x(m, -hx, hx, -hz + t / 2, y0, y1, t, ext, inn, back, out_side=-1)
        wall_z(m, -hz + t, hz - t, -hx + t / 2, y0, y1, t, ext, inn, left, out_side=-1)
        wall_z(m, -hz + t, hz - t, hx - t / 2, y0, y1, t, ext, inn, right, out_side=1)
        if lod == 0:
            pi = 0.12
            ya, yb = yf, yf + STOREY - SLAB
            for s in range(sections):
                cx = -hx + SW * (s + 0.5)
                zl0 = hz - 6.2 + 0.4
                for sx in (-1, 1):
                    wall_z(m, zl0, hz - t, cx + sx * 1.38, ya, yb, pi, stair, inn, [(hz - 1.2, 1.0, ya, ya + DOOR_H)],
                           out_side=-sx)
                wall_x(m, cx - 1.44, cx + 1.44, zl0 - pi / 2, ya, yb, pi, inn, stair, out_side=-1)
                for sx in (-1, 1):
                    xa, xb = (cx - SW / 2 + t / 2, cx - 1.44) if sx < 0 else (cx + 1.44, cx + SW / 2 - t / 2)
                    wall_x(m, xa, xb, -0.5, ya, yb, pi, inn, inn, [((xa + xb) / 2 + sx * -1.4 * k, 1.0, ya, ya + DOOR_H)])
                    wall_z(m, -0.5, hz - t, cx + sx * 4.0 * k, ya, yb, pi, inn, inn, [(1.6, 1.0, ya, ya + DOOR_H)])
                wall_z(m, -hz + t, -0.5, cx, ya, yb, pi, inn, inn)
                if f > 0:
                    for wx in (-4.3, 4.3):
                        bx = cx + wx * k + (0.5 if wx > 0 else -0.5)
                        m.box('concrete', (bx - 1.6, yf - 0.16, -hz - 1.15), (bx + 1.6, yf, -hz))
                        enclosed = rng.random() < 0.3
                        front_mat = 'corr_rust' if rng.random() < 0.5 else 'panel'
                        m.box(front_mat, (bx - 1.6, yf, -hz - 1.15), (bx + 1.6, yf + 1.0, -hz - 1.1))
                        for sx in (-1, 1):
                            m.box(front_mat, (bx + sx * 1.6 - 0.025, yf, -hz - 1.1), (bx + sx * 1.6 + 0.025, yf + 1.0, -hz))
                        m.box('rust', (bx - 1.62, yf + 1.0, -hz - 1.17), (bx + 1.62, yf + 1.05, -hz - 1.08))
                        if enclosed:      # glazed-in balcony, typical self-built wooden frames
                            for i in range(5):
                                x = bx - 1.6 + i * 0.8
                                m.box('wood', (x - 0.03, yf + 1.05, -hz - 1.15), (x + 0.03, yf + 2.6, -hz - 1.09))
                            m.box('wood', (bx - 1.6, yf + 2.6, -hz - 1.18), (bx + 1.6, yf + 2.7, -hz))
                            if rng.random() < 0.5:
                                m.poly('glass', [(bx - 1.57, yf + 1.08, -hz - 1.12), (bx - 0.83, yf + 1.08, -hz - 1.12),
                                                 (bx - 0.83, yf + 2.57, -hz - 1.12), (bx - 1.57, yf + 2.57, -hz - 1.12)])
                        if rng.random() < 0.3:
                            _junk(m, rng, bx - 1.5, bx + 1.5, -hz - 1.05, -hz - 0.1, yf, 1)
                _junk(m, rng, cx - SW / 2 + 0.6, cx - 1.6, -hz + 0.6, -0.7, ya, 2)
                _junk(m, rng, cx + 1.6, cx + SW / 2 - 0.6, -hz + 0.6, -0.7, ya, 2)
                if rng.random() < 0.5:
                    _junk(m, rng, cx + 1.6, cx + SW / 2 - 0.6, -0.3, hz - 0.8, ya, 1)
    # windows, doors, glass
    if lod == 0:
        for side, c, w, ylo, yhi, f, door in win:
            if side in ('+z', '-z'):
                zo = hz if side == '+z' else -hz
                m.push((c, 0, zo), _rot_for(side))
                _frame(m, np.eye(3), 0, 0, ylo, yhi, w, rng, _window_kind(rng, f), door=door)
                m.pop()
            else:
                xo = hx if side == '+x' else -hx
                m.push((xo, 0, c), _rot_for(side))
                _frame(m, np.eye(3), 0, 0, ylo, yhi, w, rng, _window_kind(rng, f))
                m.pop()
        # entrance doors: one leaf hanging open, sometimes missing
        for s in range(sections):
            cx = -hx + SW * (s + 0.5)
            if rng.random() < 0.75:
                a = rng.uniform(25, 110)
                m.push((cx - 0.55, 0.05, hz - 0.12), rot_y(-a))
                m.box('wood_dark', (0, 0, -0.03), (1.08, DOOR_H, 0.03))
                m.pop()
    if lod == 0:                # rain and splash dirt on all four walls
        for side, half, other in (('+z', hx, hz), ('-z', hx, hz), ('+x', hz, hx), ('-x', hz, hx)):
            m.push({'+z': (0, 0, hz), '-z': (0, 0, -hz), '+x': (hx, 0, 0), '-x': (-hx, 0, 0)}[side], _rot_for(side))
            facade_grime(m, -half, half, top + 0.75, 0.025, rng, plinth=(-BASE + 0.05, -0.02, 0.065))
            m.pop()
    # plinth band (concrete) around the building, down into the ground
    for (a, b, c, d, faces) in ((-hx - 0.04, hz, hx + 0.04, hz + 0.04, 'Z'), (-hx - 0.04, -hz - 0.04, hx + 0.04, -hz, 'z'),
                                (-hx - 0.04, -hz, -hx, hz, 'x'), (hx, -hz, hx + 0.04, hz, 'X')):
        m.box('panel', (a, -BASE - 0.6, b), (c, 0.05 - 0.3 + 0.25, d), faces=faces + 'Y')
    # roof parapet, canopies, porches, drainpipes, roof furniture
    for (a, b, c, d) in ((-hx, hz - t, hx, hz), (-hx, -hz, hx, -hz + t), (-hx, -hz, -hx + t, hz), (hx - t, -hz, hx, hz)):
        m.box('concrete', (a - 0.05, top + 0.75, b - 0.05), (c + 0.05, top + 0.9, d + 0.05))
    for s in range(sections):
        cx = -hx + SW * (s + 0.5)
        m.box('concrete', (cx - 1.5, 2.65, hz), (cx + 1.5, 2.8, hz + 1.5))
        # porch: platform at floor level and steps down to the ground in front
        m.box('concrete_floor', (cx - 1.4, -BASE - 0.3, hz), (cx + 1.4, 0.05, hz + 1.3), faces='xXYZ')
        nsteps = 3
        for i in range(nsteps):
            ytop = 0.05 - (i + 1) * (BASE / (nsteps + 0.0))
            m.box('concrete_floor', (cx - 1.4, -BASE - 0.3, hz + 1.3 + i * 0.32), (cx + 1.4, ytop + BASE / nsteps, hz + 1.3 + (i + 1) * 0.32),
                  faces='xXYZ')
        if lod == 0:
            railing_z(m, hz + 0.1, hz + 1.3, cx + 1.38, 0.05, 0.9)
        if lod == 0 and s % 2 == 0:
            m.box(ext, (cx - 1.4, top + 0.75, hz - 5.6), (cx + 1.4, top + 2.6, hz - 2.0))     # roof exit
            m.box('concrete', (cx - 1.5, top + 2.6, hz - 5.7), (cx + 1.5, top + 2.75, hz - 1.9))
        if lod == 0:
            for vx in (-3.5, 3.5):
                m.box(ext, (cx + vx * k - 0.35, top + 0.75, -1.5), (cx + vx * k + 0.35, top + 1.6, -0.8))
            if rng.random() < 0.7:
                _antenna(m, cx + rng.uniform(-4, 4), top + 0.75, rng.uniform(-3, 3), rng)
    if lod == 0:
        for x in (-hx + 0.25, hx - 0.25):
            for z, sz in ((hz + 0.12, 1), (-hz - 0.12, -1)):
                _drainpipe(m, x, z, top + 0.75, sz)
        for s in range(1, sections):
            x = -hx + SW * s
            _drainpipe(m, x, -hz - 0.12, top + 0.75, -1)
    m.pop()
    return m


# --------------------------------------------------------------------------- corridor building
def corridor(length=56.0, depth=13.4, floors=3, seed=0, lod=0, ext='plaster_mossy', inn='plaster_peel',
             floor_mat='tiles_floor', sign_row=0, fire_escape=True, beds=True):
    """Corridor-type public building: the garrison hospital (Госпиталь) and the HQ (Штаб).
    Central corridor along X, rooms both sides, stairwells at both ends, main entrance in the middle
    of +Z with canopy and sign, fire escape on -X, back door on -Z."""
    rng = np.random.default_rng(seed)
    m = Mesh(PALETTE)
    L, D = length, depth
    hx, hz = L / 2, D / 2
    t = 0.5
    top = floors * STOREY
    cw = 2.6
    m.push((0, BASE - 0.05, 0))
    holes = []
    for side in (-1, 1):
        ox = side * (hx + 0.2 - t)
        m.push((ox, 0, 0), rot_y(-90 * side))
        stairwell(m, floors, ext, lod, top_exit=False)
        m.pop()
        x_in, x_out = side * (hx - t - 4.3 + 0.2), side * (hx - t - 0.4 + 0.2)
        holes.append((min(x_in, x_out), -1.32, max(x_in, x_out), 1.32))
    for f in range(floors + 1):
        y = 0.05 + f * STOREY
        fm = 'roof_tar' if f == floors else floor_mat
        slab(m, (-hx + t / 2, -hz + t / 2, hx - t / 2, hz - t / 2), holes if 0 < f < floors else [], y,
             SLAB if f else 0.3, fm, 'plaster', inn)
    room = 4.6
    nrooms = int((L - 2 * 5.0) / room)
    room = (L - 2 * 5.0) / nrooms
    x_rooms0 = -nrooms * room / 2
    mid = nrooms // 2
    win = []
    for f in range(floors):
        yf = 0.05 + f * STOREY
        y0 = yf - (SLAB if f else 0.3)
        y1 = yf + STOREY - SLAB if f < floors - 1 else top + 0.9
        front, back = [], []
        for i in range(nrooms):
            xc = x_rooms0 + room * (i + 0.5)
            for o in (-1.05, 1.05):
                if not (f == 0 and i == mid):
                    front.append((xc + o, 1.3, yf + 0.9, yf + 2.4))
                    win.append(('+z', xc + o, 1.3, yf + 0.9, yf + 2.4, f, False))
                if not (f == 0 and i == 1 and o < 0):
                    back.append((xc + o, 1.3, yf + 0.9, yf + 2.4))
                    win.append(('-z', xc + o, 1.3, yf + 0.9, yf + 2.4, f, False))
        if f == 0:
            front.append((x_rooms0 + room * (mid + 0.5), 2.2, yf, yf + 2.5))
            win.append(('+z', x_rooms0 + room * (mid + 0.5), 2.2, yf, yf + 2.5, f, True))
            back.append((x_rooms0 + room * 1.5 - 1.05, 1.0, yf, yf + DOOR_H))
            win.append(('-z', x_rooms0 + room * 1.5 - 1.05, 1.0, yf, yf + DOOR_H, f, True))
        ends = [(-3.6, 1.2, yf + 0.9, yf + 2.4), (3.6, 1.2, yf + 0.9, yf + 2.4)]
        fire = [(-4.6, 1.0, yf, yf + DOOR_H)] if (f > 0 and fire_escape) else []
        wall_x(m, -hx, hx, hz - t / 2, y0, y1, t, ext, inn, front, out_side=1)
        wall_x(m, -hx, hx, -hz + t / 2, y0, y1, t, ext, inn, back, out_side=-1)
        wall_z(m, -hz + t, hz - t, -hx + t / 2, y0, y1, t, ext, inn, ends + fire, out_side=-1)
        wall_z(m, -hz + t, hz - t, hx - t / 2, y0, y1, t, ext, inn, ends, out_side=1)
        for zc in (-3.6, 3.6):
            win.append(('-x', zc, 1.2, yf + 0.9, yf + 2.4, f, False))
            win.append(('+x', zc, 1.2, yf + 0.9, yf + 2.4, f, False))
        if fire_escape and f > 0:
            win.append(('-x', -4.6, 1.0, yf, yf + DOOR_H, f, True))
        if lod == 0:
            ya, yb = yf, yf + STOREY - SLAB
            pi = 0.14
            xa, xb = -hx + t + 4.6, hx - t - 4.6
            for zc, osd in ((-cw / 2 - pi / 2, 1), (cw / 2 + pi / 2, -1)):
                ops = []
                for i in range(nrooms):
                    xc = x_rooms0 + room * (i + 0.5)
                    if f == 0 and zc > 0 and i == mid:
                        ops.append((xc, room - 0.4, ya, ya + 2.6))
                        continue
                    ops.append((xc + 1.4, 1.1, ya, ya + DOOR_H))
                wall_x(m, xa, xb, zc, ya, yb, pi, inn, inn, ops, out_side=osd)
                # ward doors: some hang open, some lie on the floor
                for cxo, wd, *_ in ops:
                    if wd > 1.5 or rng.random() < 0.35:
                        continue
                    zz = zc + (pi / 2 + 0.03) * (1 if zc > 0 else -1)
                    if rng.random() < 0.7:
                        a = rng.uniform(75, 105) * (1 if zc > 0 else -1)     # wide open: doorway stays passable
                        m.push((cxo - 0.52, ya, zz), rot_y(a))
                        m.box('wood_dark', (0, 0, -0.025), (1.0, DOOR_H - 0.02, 0.025))
                        m.pop()
                    else:
                        m.push((cxo, ya, zz + (0.7 if zc > 0 else -0.7)), rot_y(rng.uniform(-20, 20)))
                        m.box('wood_dark', (-0.5, 0, -1.05), (0.5, 0.05, 1.05))
                        m.pop()
            if f == 0:
                xm = x_rooms0 + room * (mid + 0.5)
                m.box('wood_dark', (xm - 1.6, ya, 2.6), (xm - 0.4, ya + 1.1, 4.6))
            for i in range(nrooms + 1):
                xw = x_rooms0 + room * i
                for z0, z1 in ((cw / 2 + pi, hz - t), (-hz + t, -cw / 2 - pi)):
                    if f == 0 and i in (mid, mid + 1) and z0 > 0:
                        continue
                    wall_z(m, z0, z1, xw, ya, yb, pi, inn, inn)
            for side in (-1, 1):
                xw = side * (hx - t - 4.6)
                for z0, z1 in ((cw / 2 + pi, hz - t), (-hz + t, -cw / 2 - pi)):
                    wall_z(m, z0, z1, xw, ya, yb, pi, inn, inn, [((z0 + z1) / 2, 1.0, ya, ya + DOOR_H)])
                for zc in (-1.36 - 0.06, 1.36 + 0.06):
                    wall_x(m, min(xw, side * (hx - t)), max(xw, side * (hx - t)), zc, ya, yb, 0.12, inn, inn)
            for i in range(nrooms):
                xc = x_rooms0 + room * (i + 0.5)
                for zs in (1, -1):
                    if f == 0 and zs > 0 and i == mid:
                        continue
                    if beds:
                        for b in range(2):
                            if rng.random() < 0.35 or (f == 0 and zs < 0 and i == 1):
                                continue          # keep the way from the back door free
                            bx = xc - 1.3 + b * 2.2 + rng.uniform(-0.2, 0.2)
                            bz = zs * (hz - t - 1.3)
                            m.push((bx, ya, bz), rot_y(rng.uniform(-25, 25)))
                            m.box('rust', (-0.45, 0.45, -1.0), (0.45, 0.55, 1.0))
                            if rng.random() < 0.7:
                                m.box('paint_cream', (-0.42, 0.55, -0.95), (0.42, 0.65, 0.95))
                            for lx in (-0.42, 0.42):
                                for lz in (-0.95, 0.95):
                                    m.box_c('rust', (lx, 0.25, lz), (0.04, 0.5, 0.04))
                            m.box('rust', (-0.45, 0.55, -1.0), (0.45, 1.05, -0.96))
                            m.pop()
                    else:
                        _junk(m, rng, xc - room / 2 + 0.3, xc - 0.2, min(zs * 1.6, zs * (hz - t - 0.3)),
                              max(zs * 1.6, zs * (hz - t - 0.3)), ya, 2)
                    if rng.random() < 0.4:          # junk stays on the half of the room away from the door (xc + 1.4)
                        _junk(m, rng, xc - room / 2 + 0.3, xc - 0.2, min(zs * 1.6, zs * (hz - t - 0.3)),
                              max(zs * 1.6, zs * (hz - t - 0.3)), ya, 1)
    if lod == 0:
        for side, c, w, ylo, yhi, f, door in win:
            if side in ('+z', '-z'):
                m.push((c, 0, hz if side == '+z' else -hz), _rot_for(side))
            else:
                m.push((hx if side == '+x' else -hx, 0, c), _rot_for(side))
            _frame(m, np.eye(3), 0, 0, ylo, yhi, w, rng, _window_kind(rng, f, 0.2), door=door)
            m.pop()
    for (a, b, c, d, faces) in ((-hx - 0.04, hz, hx + 0.04, hz + 0.04, 'Z'), (-hx - 0.04, -hz - 0.04, hx + 0.04, -hz, 'z'),
                                (-hx - 0.04, -hz, -hx, hz, 'x'), (hx, -hz, hx + 0.04, hz, 'X')):
        m.box('panel', (a, -BASE - 0.6, b), (c, 0.0, d), faces=faces + 'Y')
    for (a, b, c, d) in ((-hx, hz - t, hx, hz), (-hx, -hz, hx, -hz + t), (-hx, -hz, -hx + t, hz), (hx - t, -hz, hx, hz)):
        m.box('concrete', (a - 0.05, top + 0.9, b - 0.05), (c + 0.05, top + 1.05, d + 0.05))
    # main entrance: canopy on pillars, porch with steps, sign
    xm = x_rooms0 + room * (mid + 0.5)
    m.box('concrete', (xm - 3.5, 3.0, hz), (xm + 3.5, 3.25, hz + 4.0))
    for x in (xm - 3.2, xm + 3.2):
        m.box_c('concrete', (x, (3.0 - BASE - 0.3) / 2 - 0.0, hz + 3.7), (0.3, 3.0 + BASE + 0.3, 0.3))
    m.box('concrete_floor', (xm - 3.5, -BASE - 0.3, hz), (xm + 3.5, 0.05, hz + 2.6), faces='xXYZ')
    for i in range(3):
        m.box('concrete_floor', (xm - 3.5, -BASE - 0.3, hz + 2.6 + i * 0.32), (xm + 3.5, 0.05 - (i + 1) * BASE / 3 + BASE / 3, hz + 2.6 + (i + 1) * 0.32),
              faces='xXYZ')
    m.box('concrete', (xm - 3.6, 3.25, hz + 3.85), (xm + 3.6, 4.1, hz + 4.0))
    sign(m, sign_row, xm - 3.5, xm + 3.5, 3.27, 4.08, hz + 4.01)
    # back door steps
    xb = x_rooms0 + room * 1.5 - 1.05
    m.box('concrete_floor', (xb - 0.8, -BASE - 0.3, -hz - 1.2), (xb + 0.8, 0.05, -hz), faces='xXYz')
    if fire_escape and lod == 0:
        fx = -hx - 1.4
        for f in range(1, floors):
            yf = 0.05 + f * STOREY
            m.box('metal_plate', (fx - 1.0, yf - 0.08, -6.1), (fx + 1.3, yf, -3.5))
            railing_z(m, -6.1, -3.5, fx - 1.0, yf, 1.0)
        for f in range(floors - 1):
            ya = 0.05 + f * STOREY
            z0 = -0.7
            stair_flight(m, fx - 0.95, fx + 0.1, z0, -3.5, ya - (BASE if f == 0 else 0), ya + STOREY, 'metal_plate', 'rust')
        for z in (-6.1, -3.5):
            m.box_c('rust', (fx - 0.95, top / 2 - BASE / 2, z), (0.12, top + BASE, 0.12))
    if lod == 0:
        for x in (-hx + 0.25, hx - 0.25):
            for z, sz in ((hz + 0.12, 1), (-hz - 0.12, -1)):
                _drainpipe(m, x, z, top + 0.9, sz)
    m.pop()
    return m


# --------------------------------------------------------------------------- one-storey hall (shop, canteen)
def shop(length=37.0, width=21.2, seed=0, lod=0, ext='silicate_dirty', sign_row=1, kind='shop'):
    """One-storey trade/canteen building: shop windows along +Z, columns grid in the sales hall,
    storage rooms and offices along -Z, loading dock with a rolling shutter on +X."""
    rng = np.random.default_rng(seed)
    m = Mesh(PALETTE)
    L, D = length, width
    hx, hz = L / 2, D / 2
    t = 0.45
    H = 4.4
    inn = 'plaster_green'      # painted shop walls (photos), not the blotchy peel
    m.push((0, BASE - 0.05, 0))
    slab(m, (-hx + t / 2, -hz + t / 2, hx - t / 2, hz - t / 2), [], 0.05, 0.3, 'tiles_floor', 'plaster', inn)
    slab(m, (-hx + t / 2, -hz + t / 2, hx - t / 2, hz - t / 2), [], H, 0.3, 'roof_tar', 'plaster', inn)
    # shop windows: big openings with low parapet, entrance in the middle
    front, back, right, left = [], [], [], []
    win = []
    n = int(L / 4.0)
    step = (L - 2) / n
    for i in range(n):
        x = -hx + 1 + step * (i + 0.5)
        if i == n // 2:
            front.append((x, 2.4, 0.05, 2.8))
            win.append(('+z', x, 2.4, 0.05, 2.8, 0, True))
        else:
            front.append((x, step - 0.7, 0.75, 3.5))
            win.append(('+z', x, step - 0.7, 0.75, 3.5, 0, False))
    for i in range(int(L / 6)):
        x = -hx + 3 + 6 * i
        if x < hx - 3:
            back.append((x, 1.2, 2.2, 3.4))
            win.append(('-z', x, 1.2, 2.2, 3.4, 0, False))
    right.append((-2.0, 3.2, 0.05, 3.3))       # loading dock opening on +X
    left.append((hz - 3.0, 1.0, 0.05, DOOR_H))
    win.append(('-x', hz - 3.0, 1.0, 0.05, DOOR_H, 0, True))
    wall_x(m, -hx, hx, hz - t / 2, -0.3, H + 0.9, t, ext, inn, front, out_side=1)
    wall_x(m, -hx, hx, -hz + t / 2, -0.3, H + 0.9, t, ext, inn, back, out_side=-1)
    wall_z(m, -hz + t, hz - t, -hx + t / 2, -0.3, H + 0.9, t, ext, inn, left, out_side=-1)
    wall_z(m, -hz + t, hz - t, hx - t / 2, -0.3, H + 0.9, t, ext, inn, right, out_side=1)
    # back rooms (storage, offices) along -Z, 5 m deep
    zr = -hz + t + 5.0
    if lod == 0:
        ops = []
        for i in range(int((L - 1) / 6)):
            ops.append((-hx + 3.2 + 6 * i, 1.0, 0.05, DOOR_H))
        wall_x(m, -hx + t, hx - t, zr, 0.05, H - 0.3, 0.14, inn, inn, ops)
        for i in range(1, int((L - 1) / 6)):
            wall_z(m, -hz + t, zr - 0.07, -hx + 0.5 + 6 * i, 0.05, H - 0.3, 0.14, inn, inn)
        # columns in the sales hall
        for x in np.arange(-hx + 6, hx - 3, 6):
            for z in (zr + 5.0,):
                m.box_c('concrete', (x, H / 2, z), (0.4, H, 0.4))
        # counters and fallen shelving
        for i in range(int(L / 5)):
            x = -hx + 3 + i * 5 + rng.uniform(-0.5, 0.5)
            if kind == 'shop':
                if rng.random() < 0.55:
                    m.push((x, 0.05, zr + 2.5 + rng.uniform(-0.3, 0.3)), rot_y(rng.uniform(-8, 8)))
                    m.box('metal_plate', (-1.8, 0, -0.3), (1.8, 2.0, 0.3))
                    m.pop()
                else:
                    m.push((x, 0.05, zr + 2.5), rot_y(rng.uniform(-30, 30)))
                    m.box('metal_plate', (-1.8, 0, -1.0), (1.8, 0.6, 0.0))
                    m.pop()
            else:
                m.push((x, 0.05, zr + 3 + rng.uniform(-0.5, 0.5)), rot_y(rng.uniform(-15, 15)))
                m.box('wood', (-1.2, 0.72, -0.45), (1.2, 0.78, 0.45))
                for lx in (-1.1, 1.1):
                    for lz in (-0.38, 0.38):
                        m.box_c('rust', (lx, 0.36, lz), (0.05, 0.72, 0.05))
                m.pop()
        m.box('wood_dark', (-6, 0.05, hz - 3.5), (2, 1.05, hz - 2.8))       # counter near the entrance
        _junk(m, rng, -hx + 1, hx - 1, -hz + 1, zr - 0.5, 0.05, int(L / 4))
        _junk(m, rng, -hx + 1, hx - 1, zr + 0.5, hz - 1.5, 0.05, int(L / 6))
        for side, c, w, ylo, yhi, f, door in win:
            if side in ('+z', '-z'):
                m.push((c, 0, hz if side == '+z' else -hz), _rot_for(side))
            else:
                m.push((hx if side == '+x' else -hx, 0, c), _rot_for(side))
            kind_w = 'boarded' if (side == '+z' and not door and rng.random() < 0.25) else _window_kind(rng, 1, 0.15)
            _frame(m, np.eye(3), 0, 0, ylo, yhi, w, rng, kind_w, frame_mat='rust', door=door)
            m.pop()
        # rolling shutter half open on the dock
        m.box('shutter', (hx - 0.1, 1.8, -3.6), (hx + 0.02, 3.3, -0.4))
        m.box('concrete_floor', (hx, -BASE - 0.3, -4.0), (hx + 3.0, 0.05, 0.0), faces='xXYZz')
    m.box('panel', (-hx - 0.04, -BASE - 0.6, hz), (hx + 0.04, 0.0, hz + 0.04), faces='ZY')
    m.box('panel', (-hx - 0.04, -BASE - 0.6, -hz - 0.04), (hx + 0.04, 0.0, -hz), faces='zY')
    m.box('panel', (-hx - 0.04, -BASE - 0.6, -hz), (-hx, 0.0, hz), faces='xY')
    m.box('panel', (hx, -BASE - 0.6, -hz), (hx + 0.04, 0.0, hz), faces='XY')
    # entrance porch and steps, fascia with the sign along the whole front
    xe = -hx + 1 + step * (n // 2 + 0.5)
    m.box('concrete_floor', (xe - 2.5, -BASE - 0.3, hz), (xe + 2.5, 0.05, hz + 2.0), faces='xXYZ')
    for i in range(3):
        m.box('concrete_floor', (xe - 2.5, -BASE - 0.3, hz + 2.0 + i * 0.32), (xe + 2.5, 0.05 - (i + 1) * BASE / 3 + BASE / 3, hz + 2.0 + (i + 1) * 0.32), faces='xXYZ')
    m.box('concrete', (-hx, H + 0.9, hz - 0.1), (hx, H + 1.05, hz + 0.6))
    m.box('panel', (-hx, 3.6, hz), (hx, H + 0.9, hz + 0.08))
    sign(m, sign_row, xe - 5.5, xe + 5.5, 3.7, 4.95, hz + 0.09)
    m.pop()
    return m


# --------------------------------------------------------------------------- small structures
def water_tower(seed=0, lod=0):
    """Soviet brick water tower (as at the west edge of Skrunda-1): 5.9 m brick shaft, riveted steel
    tank on top, conical roof. Ground-floor door; the shaft is a small enterable room."""
    m = Mesh(PALETTE)
    r = 2.95
    hs = 11.0
    seg = 20 if lod == 0 else 12
    # shaft with a door gap facing +Z (angle 90 deg in lathe coords is +Z)
    m.lathe('silicate_dirty', [(r, -1.0), (r, hs)], seg=seg, arc=(105, 435), inside=False)
    m.lathe('plaster_peel', [(r - 0.45, 0.0), (r - 0.45, hs)], seg=seg, arc=(105, 435), outside=False)
    m.lathe('silicate_dirty', [(r, 2.3), (r, hs)], seg=seg, arc=(75, 105), inside=False)
    m.lathe('plaster_peel', [(r - 0.45, 2.3), (r - 0.45, hs)], seg=seg, arc=(75, 105), outside=False)
    m.box('concrete', (-0.8, 2.25, r - 0.5), (0.8, 2.4, r + 0.05))
    m.cylinder('concrete_floor', (0, -0.2, 0), (0, 0.05, 0), r - 0.4, seg)
    # tank
    m.lathe('concrete', [(r + 0.2, hs), (r + 0.6, hs + 0.4)], seg=seg)
    m.lathe('rust', [(r + 0.6, hs + 0.4), (r + 0.6, hs + 3.4), (r + 0.75, hs + 3.5)], seg=seg)
    m.lathe('corr_rust', [(r + 0.85, hs + 3.5), (0.6, hs + 5.2)], seg=seg, inside=False)
    m.cylinder('rust', (0, hs + 5.2, 0), (0, hs + 5.6, 0), 0.6, 8)
    m.cylinder('concrete', (0, hs, 0), (0, hs + 0.15, 0), r - 0.4, seg)
    if lod == 0:
        # rusty access ladder up the shaft (decoration) and a gallery ring
        for y in np.arange(0.5, hs, 0.3):
            m.box_c('rust', (-r - 0.25, y, 0), (0.04, 0.03, 0.5))
        for zz in (-0.25, 0.25):
            m.box_c('rust', (-r - 0.25, hs / 2 + 0.25, zz), (0.04, hs - 0.5, 0.04))
        n = 24
        for i in range(n):
            a = 2 * math.pi * i / n
            m.box_c('rust', ((r + 1.4) * math.cos(a), hs + 1.0, (r + 1.4) * math.sin(a)), (0.04, 1.0, 0.04))
        m.lathe('metal_plate', [(r + 0.6, hs + 0.48), (r + 1.45, hs + 0.48)], seg=seg, inside=True)
        m.lathe('rust', [(r + 1.4, hs + 1.5), (r + 1.45, hs + 1.5)], seg=seg)
    return m


def kiosk(length=7.5, width=6.2, height=3.4, seed=0, lod=0, ext='silicate_dirty', sign_row=5):
    """Small one-room brick building (transformer substation / pump house) with a steel door."""
    rng = np.random.default_rng(seed)
    m = Mesh(PALETTE)
    hx, hz = length / 2, width / 2
    t = 0.38
    slab(m, (-hx + t / 2, -hz + t / 2, hx - t / 2, hz - t / 2), [], 0.15, 0.4, 'concrete_floor', 'plaster', 'plaster_peel')
    slab(m, (-hx - 0.2, -hz - 0.2, hx + 0.2, hz + 0.2), [], height + 0.25, 0.25, 'roof_tar', 'plaster', 'concrete')
    wall_x(m, -hx, hx, hz - t / 2, -0.6, height, t, ext, 'plaster_peel', [(0.0, 1.2, 0.15, 0.15 + DOOR_H + 0.1)], out_side=1)
    wall_x(m, -hx, hx, -hz + t / 2, -0.6, height, t, ext, 'plaster_peel', [(0.0, 1.2, 2.3, 3.0)], out_side=-1)
    wall_z(m, -hz + t, hz - t, -hx + t / 2, -0.6, height, t, ext, 'plaster_peel', [], out_side=-1)
    wall_z(m, -hz + t, hz - t, hx - t / 2, -0.6, height, t, ext, 'plaster_peel', [], out_side=1)
    m.push((-0.6, 0.15, hz - 0.05), rot_y(-rng.uniform(40, 100)))
    m.box('shutter', (0, 0, -0.03), (1.2, DOOR_H, 0.03))
    m.pop()
    sign(m, sign_row, -0.45, 0.45, 2.45, 2.75, hz + 0.01, frac=0.5)
    if lod == 0:
        for i in range(3):
            m.box('rust', (-hx + 0.8 + i * 1.6, 0.15, -hz + 0.6), (-hx + 1.9 + i * 1.6, 1.9, -hz + 1.5))     # cabinets
    return m


def shed(length=5.0, width=4.6, seed=0, lod=0):
    """Corrugated-iron shed on a timber frame, door on +Z."""
    m = Mesh(PALETTE)
    hx, hz = length / 2, width / 2
    h = 2.5
    m.box('concrete_floor', (-hx, -0.3, -hz), (hx, 0.1, hz))
    t = 0.05
    wall_x(m, -hx, hx, hz - t / 2, 0.1, h, t, 'corr_rust', 'wood', [(0.6, 1.0, 0.1, 0.1 + DOOR_H)], out_side=1)
    wall_x(m, -hx, hx, -hz + t / 2, 0.1, h + 0.3, t, 'corr_rust', 'wood', [], out_side=-1)
    wall_z(m, -hz + t, hz - t, -hx + t / 2, 0.1, h, t, 'corr_rust', 'wood', [], out_side=-1)
    wall_z(m, -hz + t, hz - t, hx - t / 2, 0.1, h, t, 'corr_rust', 'wood', [], out_side=1)
    m.oriented_box('corr_rust', (0, h + 0.18, hz + 0.3), (0, h + 0.18, -hz - 0.3), length + 0.4, 0.06)
    return m


def fence_po2(n=1, lod=0, broken=False, seed=0):
    """Soviet PO-2 concrete fence panel(s) 4.0 x 2.5 m with the rhombus relief on posts, along X."""
    rng = np.random.default_rng(seed)
    m = Mesh(PALETTE)
    for i in range(n):
        x0 = i * 4.0
        if broken and i == n // 2:
            m.push((x0 + 2.0, 0.15, 0.9), rot_y(rng.uniform(-10, 10)))
            m.box('concrete_moss', (-1.98, 0, -0.08), (1.98, 0.16, 2.4))   # fallen panel
            m.pop()
        else:
            m.box('concrete_moss', (x0 + 0.06, 0.1, -0.08), (x0 + 3.94, 2.5, 0.08))
            if lod == 0:
                # rhombus relief: diamond ridges on both faces
                for zz in (0.08, -0.08):
                    s = 1 if zz > 0 else -1
                    for k in range(4):
                        cx = x0 + 0.55 + k * 0.97
                        for j in range(3):
                            cy = 0.55 + j * 0.75
                            for (ax, ay, bx, by) in ((-0.4, 0, 0, 0.32), (0, 0.32, 0.4, 0), (0.4, 0, 0, -0.32), (0, -0.32, -0.4, 0)):
                                m.oriented_box('concrete_moss', (cx + ax, cy + ay, zz + s * 0.02), (cx + bx, cy + by, zz + s * 0.02), 0.05, 0.04)
        m.box('concrete', (x0 - 0.12, -0.6, -0.14), (x0 + 0.12, 2.65, 0.14))
    m.box('concrete', (n * 4.0 - 0.12, -0.6, -0.14), (n * 4.0 + 0.12, 2.65, 0.14))
    return m


PALETTE['burlap'] = dict(tex='mud', tile=0.8, color=(0.95, 0.86, 0.66), rough=1.0, metal=1.0)


def sandbags(length=3.0, rows=4, seed=0, lod=0, curve=0.0):
    """Wall of sandbags (bags 0.55 x 0.14 x 0.32 m) along X, centred, rows stacked in a running bond."""
    rng = np.random.default_rng(seed)
    m = Mesh(PALETTE)
    bl, bh, bd = 0.55, 0.14, 0.32
    for r in range(rows):
        n = int(length / bl) - (r % 2)
        x0 = -n * bl / 2
        for i in range(n):
            x = x0 + (i + 0.5) * bl + rng.uniform(-0.03, 0.03)
            z = curve * (x / max(length, 0.1)) ** 2 * 4 + rng.uniform(-0.03, 0.03)
            y = r * bh * 0.92
            m.push((x, y, z), rot_y(rng.uniform(-6, 6)))
            # pillow shape: core box + bulge
            m.box('burlap', (-bl / 2 + 0.03, 0, -bd / 2), (bl / 2 - 0.03, bh, bd / 2))
            m.box('burlap', (-bl / 2, 0.025, -bd / 2 + 0.03), (bl / 2, bh - 0.025, bd / 2 - 0.03))
            m.pop()
    return m
