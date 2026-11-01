#!/usr/bin/env python3
"""Irradiance probe volumes for the buildings of Skrunda-1 (light from windows and doors bounced around
the rooms, baked into skrunda1.probes).

One volume per storey of every building with an interior volume: the box spans the storey from just
under its floor to just under the ceiling slab, so the grid points sit inside the rooms and the floors,
walls and ceilings all take the baked light. make_skrunda_level.py adds them when the level has none;
run this file to replace them in assets/levels/skrunda1.level in place:

    python3 -I assets/levels/source/skrunda_probes.py

Then bake (editor: Render > Bake lighting probes, or FPS_ENGINE_BAKE_PROBES=1)."""
import os
import shlex

HERE = os.path.dirname(os.path.abspath(__file__))
LEVEL = os.path.join(HERE, '..', 'skrunda1.level')
STOREY = 3.0         # zonegen.buildings.STOREY
SPACING = 3.5        # m between grid points along the floor (2 points per storey in height)
GROUP = 'Light probes'


def volumes(lines):
    """Level lines of the irradiance volumes for the interior entities among `lines`."""
    out = []
    for line in lines:
        if not (line.startswith('entity ') and '" "interior" ' in line):
            continue
        f = shlex.split(line)
        name = f[1].replace('Interior ', '', 1)
        x, y, z, yaw, sx, sy, sz = map(float, f[3:10])
        storeys = max(1, round(sy / STOREY)) if sy >= 2 * STOREY else 1
        height = STOREY - 0.15 if storeys > 1 else sy - 0.3   # under the next ceiling slab
        for s in range(storeys):
            label = f'Probes {name}' + (f' {s + 1}' if storeys > 1 else '')
            out.append(f'entity "{label}" "irradiance_volume" {x:g} {y + s * STOREY - 0.1:.2f} {z:g} {yaw:g} '
                       f'{sx - 0.6:.2f} {height:.2f} {sz - 0.6:.2f} 1 1 1 ""')
            out += [f'  parent "{GROUP}"', f'  prop "spacing" "{SPACING:g}"', '  prop "fade" "0.1"', '  prop "intensity" "2"']
    return out


def replace_in_level(path=LEVEL):
    lines = open(path, encoding='utf-8').read().splitlines()
    kept, skip = [], False
    for line in lines:
        if line.startswith('entity ') and '" "irradiance_volume" ' in line:
            skip = True
            continue
        if skip and line.startswith('  '):
            continue
        skip = False
        kept.append(line)
    if f'group "{GROUP}" "Buildings"' not in kept:
        kept.insert(next(i for i, l in enumerate(kept) if l.startswith('group ')), f'group "{GROUP}" "Buildings"')
    new = volumes(kept)
    open(path, 'w', encoding='utf-8').write('\n'.join(kept + new) + '\n')
    return new


if __name__ == '__main__':
    new = replace_in_level()
    pts = 0
    for line in new:
        if line.startswith('entity '):
            f = shlex.split(line)
            sx, sy, sz = map(float, f[7:10])
            d = [max(2, round(v / s) + 1) for v, s in ((sx, SPACING), (sy, SPACING), (sz, SPACING))]
            pts += d[0] * d[1] * d[2]
    print('wrote', sum(1 for l in new if l.startswith('entity ')), 'irradiance volumes,', pts, 'points')
