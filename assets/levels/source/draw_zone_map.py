#!/usr/bin/env python3
"""Draws a top-down plan of a level (default: zone.level) into a PNG, from the level file itself,
so it stays correct after the level is edited in the editor.

    python3 -I assets/levels/source/draw_zone_map.py [level] [out.png]
"""
import json
import math
import os
import shlex
import struct
import sys

from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
LEVEL = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, '..', 'zone.level')
OUT = sys.argv[2] if len(sys.argv) > 2 else os.path.join(HERE, 'zone_map.png')
X0, X1, Z0, Z1 = -280, 280, -480, 270     # metres shown
S = 2.0                                   # pixels per metre
W, Hh = int((X1 - X0) * S), int((Z1 - Z0) * S)

COLORS = {
    'Terrain': None, 'Highway': (70, 70, 72), 'Wrecks': (150, 72, 40), 'Road furniture': (110, 110, 110),
    'Checkpoint': (95, 105, 70), 'Tracks': (95, 80, 70), 'Rolling stock': (120, 55, 35), 'Stalker camp': (230, 160, 40),
    'Street': (80, 80, 80), 'Town buildings': (150, 110, 95), 'Plant buildings': (130, 130, 125),
    'Plant fence': (170, 170, 160), 'River': (45, 60, 70), 'Skyline': (110, 110, 120), 'Vegetation': (70, 105, 55),
    'Debris': (120, 115, 100), 'Rail yard': (140, 120, 90), 'City': (110, 110, 120),
}
GROUND = {'leafy_grass': (96, 108, 70), 'forest_ground_04': (90, 92, 66), 'brown_mud_03': (92, 78, 62),
          'gravel_concrete_03': (118, 116, 106), 'brown_mud_leaves_01': (104, 96, 70), 'asphalt_02': (68, 68, 70),
          'asphalt_01': (72, 72, 74), 'concrete_floor_worn_001': (130, 128, 120), 'dry_ground_01': (140, 125, 95)}

_bounds = {}


def glb_bounds(path):
    if path in _bounds:
        return _bounds[path]
    try:
        if path.endswith('.glb'):
            b = open(path, 'rb').read()
            n = struct.unpack('<I', b[12:16])[0]
            j = json.loads(b[20:20 + n])
        else:
            j = json.load(open(path))
        acc = [j['accessors'][p['attributes']['POSITION']] for m in j['meshes'] for p in m['primitives']]
        mn = [min(a['min'][i] for a in acc) for i in range(3)]
        mx = [max(a['max'][i] for a in acc) for i in range(3)]
        r = (mn, mx)
    except Exception:
        r = ([-0.5, 0, -0.5], [0.5, 1, 0.5])
    _bounds[path] = r
    return r


def px(x, z):
    return ((x - X0) * S, (z - Z0) * S)


def main():
    img = Image.new('RGB', (W, Hh), (88, 98, 66))
    d = ImageDraw.Draw(img)
    base = os.path.dirname(os.path.abspath(LEVEL))
    objs = []
    cur = None
    for raw in open(LEVEL, encoding='utf-8'):
        if not raw.strip() or raw.startswith('#'):
            continue
        t = shlex.split(raw)
        if t[0] in ('box', 'model', 'entity'):
            cur = {'kind': t[0], 't': t, 'parent': '', 'yaw': 0.0, 'mat': ''}
            objs.append(cur)
        elif raw.startswith('  ') and cur is not None:
            if t[0] == 'parent':
                cur['parent'] = t[1]
            elif t[0] == 'transform':
                cur['yaw'] = float(t[2])
            elif t[0] == 'material':
                cur['mat'] = os.path.splitext(os.path.basename(t[1]))[0]
    # ground boxes first (largest first), then models, then entities
    boxes = sorted([o for o in objs if o['kind'] == 'box'], key=lambda o: -float(o['t'][5]) * float(o['t'][7]))
    for o in boxes:
        t = o['t']
        cx, cz, sx, sz = float(t[2]), float(t[4]), float(t[5]), float(t[7])
        col = GROUND.get(o['mat'], (100, 100, 100))
        d.rectangle([px(cx - sx / 2, cz - sz / 2), px(cx + sx / 2, cz + sz / 2)], fill=col)
    order = ['River', 'Tracks', 'Vegetation', 'Debris', 'Road furniture', 'Plant fence', 'Wrecks', 'Rolling stock',
             'Checkpoint', 'Street', 'Stalker camp', 'Town buildings', 'Plant buildings', 'Skyline', 'Highway']
    models = sorted([o for o in objs if o['kind'] == 'model'],
                    key=lambda o: order.index(o['parent']) if o['parent'] in order else 99)
    for o in models:
        t = o['t']
        path = os.path.normpath(os.path.join(base, t[2]))
        x, z = float(t[3]), float(t[5])
        mn, mx = glb_bounds(path)
        a = math.radians(o['yaw'])
        corners = [(mn[0], mn[2]), (mx[0], mn[2]), (mx[0], mx[2]), (mn[0], mx[2])]
        pts = [px(x + cx * math.cos(a) + cz * math.sin(a), z - cx * math.sin(a) + cz * math.cos(a)) for cx, cz in corners]
        col = COLORS.get(o['parent'], (150, 150, 150))
        if o['parent'] == 'Vegetation':
            r = max(1.5, (mx[0] - mn[0]) * 0.3) * S
            cx, cy = px(x, z)
            d.ellipse([cx - r, cy - r, cx + r, cy + r], fill=col if 'grass' not in t[2] else (110, 125, 70))
            continue
        if 'cooling_tower' in t[2]:
            cx, cy = px(x, z)
            r = (mx[0] - mn[0]) / 2 * S
            d.ellipse([cx - r, cy - r, cx + r, cy + r], fill=(125, 125, 120), outline=(30, 30, 30), width=3)
            continue
        if o['parent'] == 'River':
            col = (48, 62, 70)
        d.polygon(pts, fill=col, outline=(30, 30, 30) if o['parent'] not in ('River',) else None)
    for o in objs:
        if o['kind'] != 'entity':
            continue
        t = o['t']
        x, z = float(t[3]), float(t[5])
        col = {'Soldiers': (60, 140, 230), 'Bandits': (220, 50, 40), 'Stalkers': (250, 220, 60)}.get(o['parent'], (255, 255, 255))
        cx, cy = px(x, z)
        d.ellipse([cx - 4, cy - 4, cx + 4, cy + 4], fill=col, outline=(0, 0, 0))
    # player start
    for raw in open(LEVEL, encoding='utf-8'):
        if raw.startswith('player_start'):
            _, x, y, z = raw.split()
            cx, cy = px(float(x), float(z))
            d.polygon([(cx, cy - 9), (cx - 7, cy + 6), (cx + 7, cy + 6)], fill=(255, 255, 255), outline=(0, 0, 0))
    font = ImageFont.truetype('/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf', 22)
    small = ImageFont.truetype('/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf', 16)
    labels = [(-150, 0, 'Železniční hřbitov'), (-118, 44, 'tábor stalkerů (start)'), (-2, 214, 'kontrolní stanoviště'),
              (60, 128, 'Město: nemocnice, paneláky'), (120, -200, 'Elektrárna ГРЭС-3'), (-180, -237, 'řeka'),
              (-40, -330, 'Mrtvé město'), (-75, -70, 'Dálnice E-95'), (24, -237, 'rozbité mosty')]
    for x, z, txt in labels:
        cx, cy = px(x, z)
        d.text((cx + 2, cy + 2), txt, font=font, fill=(0, 0, 0))
        d.text((cx, cy), txt, font=font, fill=(255, 255, 255))
    # scale bar and north arrow
    d.rectangle([20, Hh - 40, 20 + 100 * S, Hh - 32], fill=(255, 255, 255))
    d.text((20, Hh - 64), '100 m', font=small, fill=(255, 255, 255))
    d.polygon([(W - 40, 20), (W - 52, 50), (W - 28, 50)], fill=(255, 255, 255))
    d.text((W - 47, 54), 'S', font=font, fill=(255, 255, 255))
    legend = [((250, 220, 60), 'stalkeři'), ((60, 140, 230), 'vojáci'), ((220, 50, 40), 'bandité'), ((255, 255, 255), 'start hráče')]
    for i, (c, txt) in enumerate(legend):
        d.ellipse([20, 20 + i * 24, 34, 34 + i * 24], fill=c, outline=(0, 0, 0))
        d.text((42, 17 + i * 24), txt, font=small, fill=(255, 255, 255))
    img.save(OUT)
    print('wrote', os.path.normpath(OUT))


if __name__ == '__main__':
    main()
