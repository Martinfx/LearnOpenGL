#!/usr/bin/env python3
"""Draws the annotated plan of skrunda1.level: OSM base map (buildings, roads, water) plus what the level file
actually contains (trees, fences, wrecks, bandits, stalkers, objectives) and the routes from skrunda1_plan.json.
Re-run after editing the level in the editor.

    python3 -I assets/levels/source/draw_skrunda_plan.py [out.png]
"""
import json
import os
import re
import shlex
import sys

from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
OSM = os.path.join(HERE, 'osm', 'skrunda1.json')
LEVEL = os.path.join(HERE, '..', 'skrunda1.level')
PLAN = os.path.join(HERE, 'skrunda1_plan.json')
OUT = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, 'skrunda1_plan.png')
X0, X1, Z0, Z1 = -320, 175, -300, 215
S = 3
FONT = '/usr/share/fonts/truetype/dejavu/DejaVuSans'
F = ImageFont.truetype(FONT + '-Bold.ttf', 15)
Fs = ImageFont.truetype(FONT + '.ttf', 12)

d = json.load(open(OSM))
plan = json.load(open(PLAN))
W, H = int((X1 - X0) * S), int((Z1 - Z0) * S)
im = Image.new('RGB', (W, H), (176, 186, 150))
dr = ImageDraw.Draw(im)


def P(x, z):
    return ((x - X0) * S, (z - Z0) * S)


for a in d['areas']:
    k = a['tags'].get('landuse') or a['tags'].get('natural')
    if k in ('wetland', 'water'):
        dr.polygon([P(*p) for p in a['pts']], fill=(130, 150, 140))
for r in d['roads']:
    hw = r['tags'].get('highway')
    w = {'unclassified': 6.4, 'service': 4.6, 'track': 3.0, 'footway': 1.7}.get(hw)
    if not w:
        continue
    c = (92, 92, 92) if hw in ('unclassified', 'service') else (150, 128, 96)
    dr.line([P(*p) for p in r['pts']], fill=c, width=int(w * S), joint='curve')

# level contents
objs = []
for line in open(LEVEL, encoding='utf-8'):
    if line.startswith(('model', 'entity')):
        t = shlex.split(line)
        objs.append({'kind': t[0], 't': t, 'parent': ''})
    elif line.startswith('  parent') and objs:
        objs[-1]['parent'] = shlex.split(line)[1]
for o in objs:
    t = o['t']
    if o['kind'] == 'model' and o['parent'] in ('Trees', 'Forest'):
        x, z = float(t[3]), float(t[5])
        r = 2.2 * S
        a, b = P(x, z)
        dr.ellipse([a - r, b - r, a + r, b + r], fill=(96, 122, 78) if o['parent'] == 'Trees' else (70, 98, 62))
for b in d['buildings']:
    o = b['obb']
    if X0 < o['cx'] < X1 and Z0 < o['cz'] < Z1:
        nm = b['tags'].get('note', '')
        col = (110, 104, 100)
        if b['id'] == '184415688':
            col = (150, 70, 60)
        elif b['id'] == '184415682':
            col = (120, 55, 55)
        elif b['id'] == '184415657':
            col = (190, 140, 40)
        dr.polygon([P(*p) for p in b['pts']], fill=col, outline=(30, 30, 30))
for o in objs:
    t = o['t']
    if o['kind'] == 'model' and 'fence_po2' in t[2]:
        x, z = float(t[3]), float(t[5])
        dr.ellipse([P(x, z)[0] - 2, P(x, z)[1] - 2, P(x, z)[0] + 2, P(x, z)[1] + 2], fill=(200, 200, 190))
    if o['kind'] == 'model' and o['parent'] == 'Wrecks':
        a, b = P(float(t[3]), float(t[5]))
        dr.rectangle([a - 5, b - 5, a + 5, b + 5], fill=(150, 72, 40), outline=(0, 0, 0))
for name, (pts, col) in (('main', (plan['routes']['main'], (40, 90, 220))), ('flank', (plan['routes']['flank'], (40, 170, 80))),
                         ('tower', (plan['routes']['tower'], (230, 140, 40)))):
    dr.line([P(*p) for p in pts], fill=col, width=5, joint='curve')
    a, b = P(*pts[-1])
    dr.ellipse([a - 7, b - 7, a + 7, b + 7], fill=col)
for o in objs:
    t = o['t']
    if o['kind'] != 'entity':
        continue
    x, z = float(t[3]), float(t[5])
    a, b = P(x, z)
    if t[2] == 'enemy':
        dr.polygon([(a, b - 9), (a + 8, b + 7), (a - 8, b + 7)], fill=(205, 30, 30), outline=(0, 0, 0))
    elif t[2] == 'objective':
        dr.regular_polygon((a, b, 10), 5, fill=(250, 220, 60), outline=(0, 0, 0))
    elif t[2] == 'empty':
        dr.ellipse([a - 6, b - 6, a + 6, b + 6], fill=(60, 200, 90), outline=(0, 0, 0))
for x, z, text in plan['labels']:
    a, b = P(x, z)
    tw = dr.textlength(text, font=F)
    dr.rectangle([a - 3, b - 2, a + tw + 3, b + 18], fill=(255, 255, 255))
    dr.text((a, b), text, fill=(20, 20, 20), font=F)
a, b = P(X0 + 15, Z1 - 15)
dr.line([a, b, a + 100 * S, b], fill=(0, 0, 0), width=4)
dr.text((a, b + 6), '100 m', fill=(0, 0, 0), font=F)
dr.text((W - 80, 10), 'sever ↑', fill=(0, 0, 0), font=F)

legend = [
    '#Skrunda-1 (Mežaine), Lotyšsko',
    'Opuštěné sovětské radarové městečko.',
    'Budovy, ulice a voda 1:1 podle OSM',
    '(© OpenStreetMap contributors, ODbL).',
    '',
    '#Úkol',
    'Stalker Ljocha šel do nemocnice pro',
    'léky a nevrátil se. Najdi jeho PDA',
    've 3. patře nemocnice (jižní konec,',
    'zadní trakt) a vrať se do Štábu.',
    'Volitelně: jeho skrýš ve vodárně.',
    '',
    '#Trasy',
    'modrá: hlavní, po Nové ulici',
    '  (rychlá, ale pod odstřelovačem)',
    'zelená: boční, dvory mezi domy',
    '  k zadnímu vchodu nemocnice',
    'oranžová: k vodárně kolem Venty',
    '',
    '#Značky',
    'červený trojúhelník: bandita',
    'zelený kruh: stalker (tábor)',
    'žlutá hvězda: cíl úkolu',
    'hnědý čtverec: vrak auta',
    'tmavě zelené: stromy a les',
    '',
    '#Ovládání v editoru',
    'File > Open assets/levels/skrunda1.level',
    'F5 spustí level ve hře.',
]
big = Image.new('RGB', (W + 330, H), (232, 230, 220))
big.paste(im, (0, 0))
dr = ImageDraw.Draw(big)
y = 15
for line in legend:
    if line.startswith('#'):
        dr.text((W + 15, y), line[1:], fill=(0, 0, 0), font=F)
        y += 24
    else:
        dr.text((W + 15, y), line, fill=(30, 30, 30), font=Fs)
        y += 17
big.save(OUT)
print('wrote', OUT)
