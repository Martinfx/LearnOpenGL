#!/usr/bin/env python3
"""Feature test levels rebuilt as real zone places (no grey boxes): each one still shows its engine
feature, but in a location that makes sense, with terrain, grass, bushes and junk that belong there.

    water_test    drainage canal through a meadow hollow: flowing water, steps, a footbridge, a pump shed
    physics_test  the yard in front of a row of garages: stacked crates, jerrycans, gas bottles, tyres
    anomaly_test  an abandoned collective-farm yard: one anomaly of each kind where it would be in the zone
    lights_test   a canteen at night: stalkers' camp inside, bulb, spot, flickering tube, fires, a flashlight
    io_test       a shop at dusk: entering trips the power, tubes come on, an alarm and a bandit (entity I/O)

Run from anywhere: python3 -I assets/levels/source/make_test_levels.py [name ...]
Then open each level in the editor and save it (adds model bounds for streaming), or run the resave tool.
Names the unit tests rely on (physics_test: Crate 1-12, Tyre ...) are kept.
"""
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from zone_place import (Place, SK, V, Z, NIGHT_ENV, DUSK_ENV, OVERCAST_ENV, SPRING_ENV)

BASE = 0.6          # floor of the Skrunda-style buildings above their ground (raised plinth)


# =====================================================================================================================
def water_test():
    p = Place('water_test', 'Test: voda – odvodňovací kanál',
              'Betonový kanál s tekoucí vodou v louce. Vlez do vody po schodech, ponoř se, střílej do hladiny.',
              seed=21, env=SPRING_ENV, start=(-3.0, 7.0))
    p.weapons = ['ak74', 'pistol']
    p.hills(amp=2.2, scale=60, rim=7, rim_from=45)
    h0 = p.pad(-80, -9, 80, 9, blend=14)
    p.ditch([(-100, 0), (100, 0)], width=4.8, depth=3.1, flat=4.4)
    p.blocks.append((-100, -2.5, 100, 2.5))
    p.free_mask &= abs(p.Z) > 2.5
    p.track([(-90, -7.5), (-20, -6.8), (30, -7.2), (90, -6.0)], width=2.8)       # service track on the north bank
    p.pad(16, 4, 22, 9, blend=4, block=True, bare=0.6)                           # pump shed
    for g in ('Canal', 'Pump shed', 'Junk', 'Water'):
        p.group(g)
    # concrete lining: bed, two walls with a coping, a stair down on the south side, a broken section
    bed = h0 - 2.6
    p.box('Canal bed', 0, bed - 0.25, 0, 200, 0.5, 4.4, 'Canal', 'pbr/concrete_floor_worn_001')
    for s, nm in ((-1, 'north'), (1, 'south')):
        p.box(f'Canal wall {nm}', 0, (bed + h0 + 0.12) / 2 - 0.25, s * 2.2, 200, h0 + 0.12 - bed + 0.5, 0.4, 'Canal',
              'pbr/concrete_wall_007')
        p.wall((-60, s * 2.7), (60, s * 2.7), weeds=('grass_tall_a', 'grass_tall_b', 'weed_patch'), every=0.9, bushes=False)
    p.box('Fallen slab', 3, bed + 0.5, 0.6, 1.6, 2.2, 1.1, 'Canal', 'pbr/concrete_wall_008', rot=(0, 18, 28))
    stair_x = -6.0          # stair down to the water, set against the south wall
    for k in range(6):
        y = h0 - 0.45 * (k + 1)
        z = 1.8 - 0.32 * k
        p.box(f'Step {k + 1}', stair_x, y - 0.6 + 0.225, z, 1.6, 1.2, 0.34, 'Canal', 'pbr/concrete_floor_02')
    p.box('Stair landing', stair_x, bed + 0.1, 0.0, 1.6, 0.3, 1.2, 'Canal', 'pbr/concrete_floor_02')
    # footbridge of rails and planks across the canal
    bx = 14.0
    for dx in (-0.6, 0.6):
        p.box('Bridge beam', bx + dx, h0 + 0.05, 0, 0.18, 0.22, 6.0, 'Canal', 'pbr/rusty_metal_04')
    p.box('Bridge deck', bx, h0 + 0.2, 0, 1.6, 0.08, 5.6, 'Canal', 'pbr/weathered_planks')
    p.box('Bridge rail', bx + 0.75, h0 + 1.05, 0, 0.06, 0.06, 5.6, 'Canal', 'pbr/rusty_metal_04')
    for dz in (-2.6, 0, 2.6):
        p.box('Bridge post', bx + 0.75, h0 + 0.62, dz, 0.06, 0.85, 0.06, 'Canal', 'pbr/rusty_metal_04')
    # the water itself
    p.entity('Canal water', 'water', 0, bed, 0, 'Water', size=(200, 2.0, 4.0), color=(0.13, 0.16, 0.12),
             props=(('clarity', '1.2'), ('flow', '0.6'), ('waves', '0.35'), ('foam', '0.6')))
    # pump shed with its fence, junk where the canal workers and later scavengers left it
    p.zone('Pump shed', SK + 'shed.glb', 19, 6.5, 'Pump shed', yaw=180)
    p.wall((16.5, 4.2), (21.5, 4.2))
    p.wall((16.5, 8.8), (21.5, 8.8))
    p.prop('Shed barrel', 'barrel_03', 22.4, 5.2, 'Pump shed')
    p.prop('Shed barrel', 'Barrel_02', 22.9, 6.0, 'Pump shed')
    p.junk('Shed planks', 'planks_scrap_pile', 15.6, 8.2, 'Pump shed')
    p.prop('Old tyre in the water', 'old_tyre', 6.5, -0.9, 'Junk', y=bed + 0.05, rx=70)
    p.prop('Trash bag', 'trashbag', -12.0, 3.4, 'Junk')
    p.prop('Trash bag', 'trashbag', -11.3, 3.9, 'Junk')
    p.junk('Bricks on the bank', 'brick_pile', 5.0, -3.4, 'Junk')
    p.prop('Crate', 'wooden_crate_01', -2.0, -4.0, 'Junk', yaw=12)
    p.zone('Burnt car', Z + 'gen/car_burnt.glb', -28, -10.5, 'Junk', yaw=74)
    p.junk('Cardboard', 'cardboard_flattened', -26.5, -8.6, 'Junk')
    p.zone('Fallen lamp post', Z + 'gen/lamp_post_leaning.glb', 34, 5.0, 'Junk', yaw=200)
    p.zone('Warning sign', Z + 'gen/warning_sign.glb', -8.2, 3.2, 'Canal', yaw=0)
    p.vegetate(core=48, grass_step=1.45)
    p.write()


# =====================================================================================================================
def physics_test():
    p = Place('physics_test', 'Test: fyzikální předměty – dvůr u garáží',
              'Bedny, sudy, kanystry a pneumatiky ve dvoře u garáží padají, dají se odstrčit a rozstřílet.',
              seed=33, env=OVERCAST_ENV, start=(0.0, 6.0))
    p.weapons = ['ak74']
    p.hills(amp=2.5, scale=55, rim=6, rim_from=40)
    h0 = p.pad(-14, -14, 14, 9, blend=10, height=0.0)     # yard at y = 0 (the engine tests check absolute heights)
    for g in ('Yard', 'Garages', 'Props', 'Junk'):
        p.group(g)
    # concrete yard in front of a row of garages, a loading ramp by the last garage
    p.slabs(-13, -13, 13, 8, h0, 'Yard', level=((-6, -7, 7, 3),))
    p.free_mask &= ~((abs(p.X) < 13) & (p.Z > -13) & (p.Z < 8))
    for i, gx in enumerate((-9.0, -1.0, 7.0)):
        p.zone(f'Garages {i + 1}', Z + 'garage_block.glb', gx, -11.5, 'Garages', yaw=-90, dy=0.0)
        p.blocks.append((gx - 8.2, -15.4, gx + 8.2, -7.6))
    p.wall((-17, -7.4), (15, -7.4))
    p.box('Back wall', 0, h0 + 1.25, -16.2, 34, 2.5, 0.25, 'Yard', 'pbr/concrete_wall_008')
    p.box('Ramp', 4.5, h0 + 0.25, -3, 3, 0.3, 4, 'Yard', 'pbr/concrete_floor_02', rot=(6, 0, 0))
    p.wall((-14, 9), (14, 9))
    p.wall((14, -14), (14, 9))
    p.wall((-14, -14), (-14, 9))
    # the physics props (names used by the engine tests)
    y = h0

    def crate(i, x, yy, z, f='wooden_crate_01', mass=18):
        p.model(f'Crate {i}', f'../models/props/{f}/{f}.gltf', x, y + yy, z, 'Props', physics=(mass, 'box'), cull=80)
    for i, (x, yy) in enumerate(((-1.29, 0), (-0.43, 0), (0.43, 0), (1.29, 0), (-0.86, 0.36), (0, 0.36), (0.86, 0.36),
                                 (-0.43, 0.72), (0.43, 0.72), (0, 1.08))):
        crate(i + 1, x, yy, -4)
    crate(11, -2.6, 0, -4.2, 'wooden_crate_02', 25)
    crate(12, -2.6, 0.48, -4.2, 'wooden_crate_02', 25)
    for i, (x, z, yaw) in enumerate(((-4, -2.0, 0), (-3.3, -1.7, 20), (-3.65, -1.4, 40))):
        p.model(f'Jerrycan {i + 1}', '../models/props/metal_jerrycan/metal_jerrycan.gltf', x, y, z, 'Props', yaw,
                physics=(4, 'box'), cull=60)
    for i, (x, yy) in enumerate(((3.0, 0.2), (3.6, 0.5), (4.2, 0.8))):
        p.model(f'Cardboard {i + 1}', '../models/props/clutter/cardboard_box_small.glb', x, y + yy + 0.3, 1, 'Props',
                physics=(2, 'box'), cull=60)
    p.model('Gas tank', '../models/props/propane_tank/propane_tank.gltf', -2, y, 0, 'Props', physics=(12, 'cylinder'))
    p.model('Tyre', '../models/props/old_tyre/old_tyre.gltf', 2, y + 1.5, -1, 'Props', physics=(9, 'cylinder'))
    p.model('Tyre 2', '../models/props/old_tyre/old_tyre.gltf', 8.6, y, -5.6, 'Props', 30, physics=(9, 'cylinder'))
    p.model('Tyre 3', '../models/props/old_tyre/old_tyre.gltf', 8.8, y + 0.25, -5.4, 'Props', 75, physics=(9, 'cylinder'))
    p.model('Barrel 1', '../models/props/Barrel_01/Barrel_01.gltf', -9.5, y, -5.8, 'Props', physics=(30, 'cylinder'))
    p.model('Barrel 2', '../models/props/barrel_03/barrel_03.gltf', -8.7, y, -6.1, 'Props', physics=(30, 'cylinder'))
    p.model('Bucket', '../models/props/wooden_bucket_01/wooden_bucket_01.gltf', -6.0, y, 1.5, 'Props', physics=(2, 'cylinder'))
    # static junk: a car on blocks, pallets, a workbench in an open garage, scrap by the fence
    p.zone('Car on blocks', Z + 'gen/car_burnt.glb', 9.5, 3.5, 'Junk', yaw=-15, dy=0.12)
    for dx in (-0.6, 0.6):
        for dz in (-1.4, 1.4):
            p.junk('Car block', 'cinder_block', 9.5 + dx * math.cos(0.26) + dz * 0.26, 3.5 + dz - dx * 0.26, 'Junk', y=y)
    p.junk('Pallets', 'wooden_pallet', -11.0, 1.0, 'Junk', yaw=10, y=y)
    p.junk('Pallets', 'wooden_pallet', -11.1, 1.0, 'Junk', yaw=14, y=y + 0.14)
    p.junk('Broken pallet', 'wooden_pallet_broken', -10.6, 3.2, 'Junk', y=y)
    p.prop('Workbench vice', 'bench_vice_01', -1.0, -8.2, 'Junk', yaw=180, y=y + 0.8, collision=False)
    p.prop('Tool chest', 'metal_tool_chest', 0.6, -8.4, 'Junk', yaw=180, y=y)
    p.prop('Wheel rim', 'rusted_wheel_rim_01', 12.0, -6.5, 'Junk', y=y)
    p.junk('Scrap planks', 'planks_scrap_pile', -13.0, 6.5, 'Junk')
    p.junk('Rubble', 'rubble_pile_small', 13.2, -7.0, 'Junk', y=y)
    p.prop('Trash bag', 'trashbag', -7.4, -6.6, 'Junk', y=y)
    p.prop('Trash bag', 'trashbag', -6.8, -6.9, 'Junk', y=y)
    p.zone('Lamp post', Z + 'gen/lamp_post.glb', -13.2, 8.4, 'Yard', yaw=0)
    p.track([(-2, 9), (-4, 30), (-12, 60), (-20, 95)], width=3.2)
    p.cracks(-13, -7.2, 13, 8, 250, y=h0)
    for k, (x, z) in enumerate(((-6.5, 4.0), (5.5, 6.2), (-2.0, -6.2))):
        p.model('Puddle', Z + ('gen/puddle_a.glb' if k % 2 else 'gen/puddle_b.glb'), x, h0 + 0.005, z, 'Yard', k * 70,
                collision=False)
    p.vegetate(core=45, grass_step=1.45)
    p.write()


# =====================================================================================================================
def anomaly_test():
    p = Place('anomaly_test', 'Zóna: anomálie na opuštěném statku',
              'G hází šroub. Detektor pípá u anomálií, geiger cvaká v radiaci. Každá anomálie je tam, kde by v zóně vznikla.',
              seed=47, env=OVERCAST_ENV, start=(0.0, 30.0))
    p.hills(amp=3.0, scale=65, rim=8, rim_from=50)
    for g, par in (('Farm', ''), ('Junk', ''), ('Anomalies', ''), ('Enemies', ''), ('Map markers', ''), ('Ambience', '')):
        p.group(g, par)
    # farm yard: concrete apron in the middle, a machine hall, a cowshed ruin, tracks between them
    h0 = p.pad(-20, -14, 20, 12, blend=12, bare=0.7)
    p.slabs(-15, -11, 11, 7, h0, 'Farm', missing=0.08)
    p.free_mask &= ~((abs(p.X + 2) < 13) & (abs(p.Z + 2) < 9))
    p.pad(-36, -30, -6, -18, blend=6, block=True)
    p.zone('Barn', Z + 'gen/factory_hall.glb', -21, -24, 'Farm', yaw=0)
    p.blocks.append((-39.5, -33.5, -2.5, -14.5))
    for a, b in (((-39.5, -14.5), (-2.5, -14.5)), ((-39.5, -33.5), (-39.5, -14.5)), ((-2.5, -33.5), (-2.5, -14.5))):
        p.wall(a, b)
    p.track([(0, 40), (0, 12)], width=3.2)
    p.track([(20, -2), (45, 8), (80, 20)], width=3.0)
    p.track([(-20, 2), (-55, 10), (-90, 6)], width=2.8)
    p.ditch([(30, -40), (34, -18), (26, -6)], width=7.0, depth=1.2)     # drainage ditch: the acid pools in it
    # anomalies where they belong
    def an(name, kind, x, z, size, color, props=(), y=None):
        yy = p.h(x, z) if y is None else y
        p.entity(name, 'anomaly', x, yy, z, 'Anomalies', size=size, color=color, props=(('kind', kind),) + tuple(props))
    # discharge around a fallen power pole across the apron
    p.zone('Fallen pole', Z + 'gen/lamp_post_leaning.glb', 2, 2, 'Junk', yaw=160)
    an('Discharge 1', 'discharge', 3.0, 1.0, (3.5, 3, 3.5), (0.35, 0.6, 1))
    # crusher among car wrecks it has already squashed
    an('Crusher 1', 'crusher', -12, 4, (5, 3, 5), (0.55, 0.5, 0.75))
    p.zone('Crushed car', Z + 'gen/car_burnt.glb', -12.5, 4.5, 'Junk', yaw=35, dy=-0.25)
    p.junk('Crushed bricks', 'rubble_pile_large', -10.6, 2.5, 'Junk')
    # vent: heat from below through the cracked concrete by the barn (old boiler house)
    an('Vent 1', 'vent', 12, -8, (2, 5, 2), (1, 0.45, 0.15), (('period', '6'),))
    for k in range(4):
        ang = k * 90 + 20
        p.box(f'Vent slab {k + 1}', 12 + math.cos(math.radians(ang)) * 1.6, h0 + 0.02, -8 + math.sin(math.radians(ang)) * 1.6,
              1.4, 0.12, 0.9, 'Junk', 'pbr/concrete_floor_worn_001', rot=(4, ang, 3))
    # acid pooled in the drainage ditch below the leaking barrels
    an('Acid pool 1', 'acid', 31, -20, (6, 1, 6), (0.45, 0.85, 0.2))
    for k, (x, z) in enumerate(((27.5, -23.5), (28.2, -24.3), (27.0, -24.8))):
        p.prop('Leaking barrel', 'barrel_03' if k else 'Barrel_01', x, z, 'Junk', rx=(80 if k == 2 else 0))
    # rift in the gap of the broken wall, its exit on the barn roof
    an('Rift 1', 'rift', -24, 6, (2.5, 2.5, 2.5), (0.75, 0.4, 0.95), (('link', 'Rift exit'),))
    p.zone('Broken wall', SK + 'fence_po2_x5_broken.glb', -34, 7.5, 'Farm', yaw=0)
    p.wall((-34, 8), (-14, 8))
    p.entity('Rift exit', 'particle', -21, p.h(-21, -24) + 14.6, -24, 'Anomalies', size=(0.4, 0.4, 0.4),
             color=(0.75, 0.4, 0.95), props=(('effect', 'particles/anomaly_rift_idle.particle'),))
    # radiation: the hotspot is the old fertiliser/fuel store, the area a hollow downwind of it
    an('Radiation hotspot', 'radiation', -26, -40, (8, 4, 8), (0.9, 0.85, 0.2), (('intensity', '30'),))
    p.zone('Fuel store container', Z + 'shipping_container.glb', -26, -41.5, 'Junk', yaw=8)
    p.prop('Rad barrel', 'barrel_03', -22.5, -38.5, 'Junk')
    p.prop('Rad barrel', 'Barrel_02', -23.2, -37.8, 'Junk')
    an('Radiation area', 'radiation', 2, -36, (12, 4, 10), (0.9, 0.85, 0.2), (('area', '1'), ('intensity', '4'), ('fade', '3')))
    p.zone('Warning sign', Z + 'gen/warning_sign.glb', -1, -28.5, 'Junk', yaw=10)
    # farm junk
    p.zone('Old bus', Z + 'gen/bus_small_cream.glb', 18, 10, 'Junk', yaw=-60)
    p.zone('Hay rubble', Z + 'gen/rubble_a.glb', 22, -14, 'Junk', yaw=30)
    p.junk('Pallets', 'wooden_pallet_broken', -4, -10, 'Junk')
    p.prop('Tyre', 'old_tyre', 6.5, 7.5, 'Junk', rx=90)
    p.prop('Tyre', 'old_tyre', 7.2, 7.1, 'Junk', rx=90, dy=0.25)
    p.junk('Bricks', 'brick_pile', -15.5, -12.6, 'Junk')
    p.person('Guard', 'enemy', 0, -6, 'Enemies', yaw=90)
    for n, x, z in (('Drtič', -12, 4), ('Výboj', 3, 1), ('Výheň', 12, -8), ('Žíravina', 31, -20), ('Zlom', -24, 6),
                    ('Radiace', -26, -40)):
        p.entity(n, 'map_marker', x, p.h(x, z), z, 'Map markers', size=(1, 1, 1), color=(0.85, 0.3, 0.2))
    p.entity('Zone drone', 'sound', 0, -50, 0, 'Ambience', size=(400, 200, 400), color=(0.25, 0.55, 0.95),
             props=(('sound', 'sounds/ambient/zone_drone.sound'), ('zone', '1'), ('fade', '20')))
    p.vegetate(core=50, grass_step=1.45)
    p.write()


# =====================================================================================================================
def _hall(p, file, L, D, h0, name):
    """A one-storey Skrunda hall (canteen/shop, zonegen.skrunda.shop) at the origin, entrance at +Z."""
    p.pad(-L / 2 - 1, -D / 2 - 1, L / 2 + 1, D / 2 + 1, blend=8, height=h0, block=True)
    p.zone(name, SK + file, 0, 0, 'Building', yaw=0)
    for a, b in (((-L / 2, D / 2 + 0.2), (L / 2, D / 2 + 0.2)), ((-L / 2, -D / 2 - 0.2), (L / 2, -D / 2 - 0.2)),
                 ((-L / 2 - 0.2, -D / 2), (-L / 2 - 0.2, D / 2)), ((L / 2 + 0.2, -D / 2), (L / 2 + 0.2, D / 2))):
        p.wall(a, b)
    p.interior(f'Interior {name}', 0, h0 + BASE - 0.15, 0, L, 4.4, D, 'Building')
    return h0 + BASE


def lights_test():
    p = Place('lights_test', 'Test: lokální světla – jídelna v noci',
              'Stalkeři přespávají ve staré jídelně: žárovka, reflektor, blikající zářivka, oheň v sudu a u táboráku, '
              'baterka hlídky.', seed=58, env=NIGHT_ENV, start=(0.0, 4.0))
    p.weapons = ['pistol']
    p.hills(amp=2.0, scale=60, rim=6, rim_from=40)
    for g in ('Building', 'Lights', 'Campfire', 'Camp', 'Junk'):
        p.group(g)
    L, D = 29.3, 15.1
    h0 = p.h(0, 0)
    fy = _hall(p, 'canteen.glb', L, D, h0, 'Canteen')
    p.track([(0, 12), (6, 30), (20, 70)], width=3.0)
    zr = -D / 2 + 0.45 + 5.0           # back rooms wall
    # lights, the same set as before (names kept), placed where people would put them
    p.light('Bulb', -2.5, fy + 3.4, 1.0, 'Lights', (1, 0.72, 0.42), (('intensity', '18'), ('range', '9'), ('shadows', '1')))
    p.light('Spot', 9.5, fy + 3.6, 6.2, 'Lights', (0.75, 0.85, 1), (('light', 'spot'), ('intensity', '60'), ('range', '12'),
                                                                   ('pitch', '-55'), ('inner', '18'), ('outer', '30'),
                                                                   ('shadows', '1')), yaw=225)
    p.prop('Searchlight', 'portable_searchlight', 9.5, 6.2, 'Camp', yaw=225, y=fy)
    p.light('Tube', -9, fy + 3.9, -0.5, 'Lights', (0.8, 1, 0.85), (('intensity', '10'), ('range', '7'),
                                                                   ('flicker', 'fluorescent')))
    p.light('Barrel fire', 6.0, fy + 0.95, -0.5, 'Lights', (1, 0.5, 0.15), (('intensity', '14'), ('range', '7'),
                                                                           ('flicker', 'fire')))
    p.prop('Fire barrel', 'barrel_stove', 6.0, -0.5, 'Camp', y=fy)
    p.entity('Barrel flames', 'particle', 6.0, fy + 0.9, -0.5, 'Lights', size=(0.4, 0.4, 0.4), color=(0.95, 0.55, 0.2),
             props=(('effect', 'particles/fire.particle'),))
    p.person('Guard', 'npc', 8.5, 1.5, 'Lights', yaw=200, char='stalker', y=fy, script='', props=(('flashlight', '1'),))
    # campfire outside by the entrance, where the smoke can go
    cx, cz = -5.0, D / 2 + 5.0
    p.trodden(cx, cz, 2.6)
    p.track([(0, D / 2 + 1.5), (cx, cz)], width=1.4, sink=0.02)
    p.prop('stone_fire_pit', 'stone_fire_pit', cx, cz, 'Campfire', yaw=0)
    p.light('Campfire light', cx, p.h(cx, cz) + 0.6, cz, 'Campfire', (1, 0.55, 0.25),
            (('intensity', '22'), ('range', '10'), ('shadows', '1'), ('flicker', 'fire')))
    p.entity('Campfire flames', 'particle', cx, p.h(cx, cz) + 0.15, cz, 'Campfire', size=(0.4, 0.4, 0.4),
             color=(0.95, 0.55, 0.2), props=(('effect', 'particles/fire.particle'),))
    p.entity('Campfire crackle', 'sound', cx, p.h(cx, cz) + 0.3, cz, 'Campfire', size=(1, 1, 1), color=(0.25, 0.55, 0.95),
             props=(('sound', 'sounds/ambient/campfire.sound'),))
    for dx, dz, yaw in ((-1.6, 0.4, 80), (1.5, -0.6, 260)):
        p.prop('Seat crate', 'wooden_military_crate', cx + dx, cz + dz, 'Campfire', yaw=yaw)
    # the camp inside: sleeping places, supplies, a table under the bulb
    p.prop('Camp table', 'WoodenTable_01', -2.5, 1.0, 'Camp', yaw=5, y=fy, cull=40)
    p.prop('Radio', 'vintage_radio_transceiver', -2.6, 1.0, 'Camp', yaw=185, y=fy + 0.55, collision=False)
    p.prop('Lantern', 'vintage_oil_lamp', -2.0, 1.2, 'Camp', y=fy + 0.55, collision=False)
    for k, (x, z) in enumerate(((-6.5, 4.5), (-4.5, 5.2), (2.5, 4.8))):
        p.junk('Bedroll', 'cardboard_flattened', x, z, 'Camp', y=fy + 0.01, yaw=p.rng.uniform(-15, 15) + 90)
        p.junk('Rag', 'rag_a', x + 0.4, z - 0.3, 'Camp', y=fy + 0.02)
    p.prop('Supplies', 'plastic_crate_01', 11.0, 1.2, 'Camp', y=fy)
    p.prop('Supplies', 'russian_food_cans_01', 11.2, 1.9, 'Camp', y=fy)
    p.prop('Jerrycan', 'metal_jerrycan', 10.4, 2.2, 'Camp', y=fy)
    p.prop('Generator', 'portable_generator', 12.5, 5.5, 'Camp', y=fy)
    p.junk('Paper', 'newspaper_sheet_a', 0.5, 3.0, 'Junk', y=fy + 0.01)
    p.junk('Paper', 'newspaper_sheet_b', -8.0, 2.0, 'Junk', y=fy + 0.01)
    p.junk('Boxes', 'cardboard_box_crushed', -11.5, 5.5, 'Junk', y=fy)
    p.junk('Rubble', 'rubble_pile_small', 13.0, -1.5, 'Junk', y=fy)
    # outside junk
    p.zone('Burnt car', Z + 'gen/car_burnt.glb', 14, 14, 'Junk', yaw=-25)
    p.prop('Trash bag', 'trashbag', -12, D / 2 + 1.6, 'Junk')
    p.junk('Pallets', 'wooden_pallet_broken', L / 2 + 2.5, -4, 'Junk')
    p.vegetate(core=40, grass_step=1.45)
    p.write()
    # the player starts inside: fix the start height to the hall floor
    _fix_start(p, fy)


def io_test():
    p = Place('io_test', 'Test: propojení entit – obchod za soumraku',
              'Vejdi do obchodu: trigger nahodí proud, rozsvítí zářivky, spustí poplach a pošle na tebe banditu. '
              'Jeho smrt poplach vypne.', seed=69, env=DUSK_ENV, start=(0.0, 24.0))
    p.weapons = ['ak74']
    p.extra_header = ['ambience "../sounds/ambience/zone_outdoor.ambience"']
    p.hills(amp=2.0, scale=60, rim=6, rim_from=42)
    for g in ('Building', 'Logic', 'Hall lights', 'Cover', 'Junk'):
        p.group(g)
    L, D = 36.1, 20.3
    h0 = p.h(0, 0)
    fy = _hall(p, 'shop_venta.glb', L, D, h0, 'Shop')
    p.track([(0, D / 2 + 3), (0, 30), (-10, 60), (-30, 95)], width=3.4)
    p.trodden(0, D / 2 + 1.5, 2.0)
    hz = D / 2
    zr = -hz + 0.45 + 5.0
    p.light('Porch bulb', 0, fy + 3.2, hz + 1.6, 'Building', (1, 0.72, 0.42),
            (('intensity', '6'), ('range', '7'), ('flicker', 'broken'), ('flicker_speed', '0.6')))
    p.entity('Hall entrance', 'trigger', 0, fy, hz - 1.5, 'Logic', size=(3, 1.5, 1), color=(1, 0.55, 0.15),
             props=(('once', '1'),),
             outputs=(('OnTrigger', 'Power relay', 'Trigger', '', 0, 0),
                      ('OnTrigger', '!player', 'ShowMessage', 'Proud naskočil. Někdo tu je.', 2.5, 0)))
    p.entity('Power relay', 'relay', -L / 2 + 1.5, fy + 2.5, hz - 1.0, 'Logic', size=(0.4, 0.4, 0.4), color=(0.7, 0.4, 1),
             outputs=(('OnTrigger', 'Hall tube 1', 'TurnOn', '', 0, 0), ('OnTrigger', 'Hall tube 2', 'TurnOn', '', 0.7, 0),
                      ('OnTrigger', 'Hall tube 3', 'TurnOn', '', 1.4, 0), ('OnTrigger', 'Alarm', 'Enable', '', 1.8, 0),
                      ('OnTrigger', 'Alarm light', 'TurnOn', '', 1.8, 0), ('OnTrigger', 'Bandit', 'Alert', '', 2.2, 0)))
    p.prop('Fuse box', 'power_box_01', -L / 2 + 0.6, hz - 1.0, 'Logic', yaw=90, y=fy + 1.2, collision=False)
    for i, x in enumerate((-9.0, 0.0, 9.0)):
        props = [('intensity', '14'), ('range', '10'), ('flicker', 'fluorescent'), ('enabled', '0')]
        if i == 1:
            props.append(('shadows', '1'))
        p.light(f'Hall tube {i + 1}', x, fy + 3.9, zr + 4.5, 'Hall lights', (0.8, 1, 0.85), props)
    p.light('Alarm light', L / 2 - 1.2, fy + 3.2, zr + 2.0, 'Hall lights', (1, 0.08, 0.04),
            (('intensity', '25'), ('range', '14'), ('flicker', 'pulse'), ('flicker_speed', '1.6'), ('enabled', '0')))
    p.entity('Alarm', 'sound', L / 2 - 1.2, fy + 2.8, zr + 2.0, 'Logic', size=(0.6, 0.6, 0.6), color=(0.25, 0.55, 0.95),
             props=(('sound', 'sounds/ambient/alarm.sound'), ('enabled', '0')))
    p.person('Bandit', 'enemy', 12.0, zr + 1.5, 'Logic', yaw=135, y=fy,
             props=(('patrol_radius', '1.5'), ('view_range', '8')),
             outputs=(('OnDeath', 'Alarm', 'Disable', '', 1.5, 0), ('OnDeath', 'Alarm light', 'TurnOff', '', 1.5, 0),
                      ('OnDeath', '!player', 'ShowMessage', 'Poplach utichl.', 2, 0)))
    p.entity('Hall reverb', 'reverb', 0, fy, zr + 2.4, 'Logic', size=(L - 1, 4, D - 6.5), color=(0.3, 0.75, 0.9),
             props=(('preset', 'factory_hall'), ('amount', '0.9'), ('fade', '1.5')))
    p.entity('Storeroom reverb', 'reverb', 0, fy, -hz + 2.9, 'Logic', size=(L - 1, 4, 4.8), color=(0.3, 0.75, 0.9),
             props=(('preset', 'stonecorridor'), ('fade', '1')))
    p.entity('Building ambience', 'ambience', 0, fy, 0, 'Logic', size=(L, 5, D), color=(0.4, 0.85, 0.5),
             props=(('set', 'sounds/ambience/interior.ambience'), ('fade', '3'), ('outside', '0.15')))
    # cover in the hall: the bandits' barricade of crates and a counter
    p.prop('Crate stack', 'wooden_crate_02', -1.5, zr + 3.0, 'Cover', y=fy, yaw=10)
    p.prop('Crate stack', 'wooden_crate_02', -1.5, zr + 3.0, 'Cover', y=fy + 0.48, yaw=-5)
    p.zone('Sandbags', SK + 'sandbags_3m.glb', 4.0, zr + 5.5, 'Cover', yaw=0, dy=fy - h0)
    p.prop('Loot', 'plastic_crate_01', 14.0, zr - 0.8, 'Junk', y=fy)
    p.prop('Loot', 'long_life_food', 14.6, zr - 0.6, 'Junk', y=fy)
    p.junk('Paper', 'newspaper_stack', -6.0, zr + 6.0, 'Junk', y=fy + 0.01)
    p.junk('Boxes', 'cardboard_box_open', -12.0, zr + 1.5, 'Junk', y=fy)
    p.junk('Rubble', 'rubble_pile_small', 6.5, hz - 1.2, 'Junk', y=fy)
    # outside: the shop's car park
    p.slabs(-15, hz + 2, 15, hz + 11, h0, 'Building', along_x=False)
    p.free_mask &= ~((abs(p.X) < 15) & (p.Z > hz + 2) & (p.Z < hz + 11))
    p.zone('Abandoned car', Z + 'gen/car_sedan_green.glb', -9, hz + 6.5, 'Junk', yaw=80)
    p.zone('Burnt car', Z + 'gen/car_burnt.glb', 10, hz + 7.5, 'Junk', yaw=-95)
    p.prop('Trash can', 'metal_trash_can', 4.5, hz + 2.6, 'Junk', rz=85)
    p.prop('Trash bag', 'trashbag', 5.4, hz + 3.0, 'Junk')
    p.zone('Lamp post', Z + 'gen/lamp_post_leaning.glb', -14, hz + 10.5, 'Junk', yaw=120)
    p.vegetate(core=44, grass_step=1.45)
    p.write()


def _fix_start(p, y):
    path = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', p.name + '.level')
    s = open(path, encoding='utf-8').read().split('\n')
    for i, l in enumerate(s):
        if l.startswith('player_start '):
            sx, sz = p.start
            s[i] = f'player_start {sx:.2f} {y + 0.05:.2f} {sz:.2f}'
    open(path, 'w', encoding='utf-8').write('\n'.join(s))


LEVELS = {f.__name__: f for f in (water_test, physics_test, anomaly_test, lights_test, io_test)}
if __name__ == '__main__':
    for n in (sys.argv[1:] or LEVELS):
        LEVELS[n]()
