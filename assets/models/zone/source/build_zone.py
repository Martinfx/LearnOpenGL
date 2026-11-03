#!/usr/bin/env python3
"""Builds the procedurally generated zone models (CC0) into assets/models/zone/gen/.

    python3 -I assets/models/zone/source/build_zone.py [name ...]

Every model gets <name>.glb and, when it has detail worth dropping, <name>_lod1.glb
(the editor attaches foo_lod1.glb automatically when foo.glb is placed).
Textures are packed once into assets/models/zone/textures/ from assets/textures/pbr/.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from zonegen import mats, glb                         # noqa: E402
from zonegen import vehicles, structures, buildings, vegetation   # noqa: E402
from zonegen import panel_tower, square, road_decals   # noqa: E402

OUT = os.path.join(mats.ZONE, 'gen')

# name -> (function, kwargs, has_lod)
MODELS = {
    'car_sedan_green': (vehicles.car, dict(kind='sedan', paint='paint_green', seed=1), True),
    'car_sedan_blue': (vehicles.car, dict(kind='sedan', paint='paint_blue', seed=2), True),
    'car_sedan_cream': (vehicles.car, dict(kind='sedan', paint='paint_cream', seed=3), True),
    'car_estate_red': (vehicles.car, dict(kind='estate', paint='paint_red', seed=4), True),
    'car_estate_rust': (vehicles.car, dict(kind='estate', paint='rust', seed=5), True),
    'car_van_green': (vehicles.car, dict(kind='van', paint='paint_green', seed=6), True),
    'car_burnt': (vehicles.car, dict(kind='sedan', paint='burnt', seed=7, burnt=True), True),
    'bus_small_cream': (vehicles.bus, dict(length=7.0, paint='paint_cream', seed=1), True),
    'bus_small_blue': (vehicles.bus, dict(length=7.0, paint='paint_blue', seed=2), True),
    'bus_long_red': (vehicles.bus, dict(length=10.5, paint='paint_red', seed=3), True),
    'bus_long_rust': (vehicles.bus, dict(length=10.5, paint='rust', seed=4), True),
    'rail_carriage_green': (vehicles.carriage, dict(seed=1, paint='paint_green'), True),
    'rail_carriage_rust': (vehicles.carriage, dict(seed=2, paint='rust'), True),
    'rail_locomotive': (vehicles.locomotive, dict(), True),
    'rail_tender': (vehicles.tender, dict(), True),
    'rail_track_25m': (structures.track, dict(length=25.0), True),
    'guardrail_24m': (structures.guardrail, dict(length=24.0), False),
    'sign_gantry': (structures.sign_gantry, dict(), True),
    'cooling_tower': (structures.cooling_tower, dict(broken=False), True),
    'cooling_tower_broken': (structures.cooling_tower, dict(broken=True, seed=3), True),
    'pipe_rack_24m': (structures.pipe_rack, dict(), True),
    'conveyor_fallen': (structures.conveyor, dict(fallen=True), True),
    'bridge_broken': (structures.bridge, dict(), True),
    'lamp_post': (structures.lamp_post, dict(), False),
    'lamp_post_leaning': (structures.lamp_post, dict(lean=12), False),
    'puddle_a': (structures.puddle, dict(seed=1, size=4.0), False),
    'puddle_b': (structures.puddle, dict(seed=2, size=7.0), False),
    'river_80m': (structures.river, dict(length=80.0, width=44.0), False),
    'skyline_tower_a': (buildings.ruined_tower, dict(floors=22, seed=1), True),
    'skyline_tower_b': (buildings.ruined_tower, dict(floors=16, seed=2, w=30, d=15), True),
    'skyline_tower_c': (buildings.ruined_tower, dict(floors=28, seed=3, w=18, d=18), True),
    'panel_tower_a': (panel_tower.panel_tower, dict(length=24.0, depth=18.0, floors=16, seed=11), True),
    'panel_tower_b': (panel_tower.panel_tower, dict(length=30.0, depth=15.0, floors=12, seed=12), True),
    'panel_tower_c': (panel_tower.panel_tower, dict(length=21.0, depth=21.0, floors=22, seed=13, skin='concrete'), True),
    'apartment_khrushchevka': (buildings.apartment, dict(floors=4, sections=2, seed=1), True),
    'hospital': (buildings.hospital, dict(seed=2), True),
    'machine_hall': (buildings.hall, dict(length=48, width=21, height=15, kind='turbine', seed=3), True),
    'factory_hall': (buildings.hall, dict(length=36, width=18, height=11, kind='workshop', seed=4), True),
    'rubble_a': (structures.rubble, dict(seed=1, size=3.0), True),
    'rubble_b': (structures.rubble, dict(seed=2, size=5.0), True),
    'slab_fallen': (structures.slab_fallen, dict(seed=3), True),
    'station_sign': (structures.station_sign, dict(), False),
    'warning_sign': (structures.warning_sign, dict(), False),
    'birch_a': (vegetation.birch, dict(seed=1, height=11.0), True),
    'birch_b': (vegetation.birch, dict(seed=2, height=8.0), True),
    'birch_c': (vegetation.birch, dict(seed=3, height=13.0), True),
    'birch_dead': (vegetation.birch, dict(seed=4, height=10.0, dead=True), True),
    'dead_tree_a': (vegetation.dead_tree, dict(seed=1, height=9.0), True),
    'dead_tree_b': (vegetation.dead_tree, dict(seed=2, height=7.0), True),
    'bush_a': (vegetation.bush, dict(seed=1, size=1.8), True),
    'bush_b': (vegetation.bush, dict(seed=2, size=2.6), True),
    'grass_patch': (vegetation.grass_patch, dict(seed=1), False),
    'grass_patch_dry': (vegetation.grass_patch, dict(seed=2, dry=True, height=0.8), False),
    'grass_crack': (vegetation.grass_patch, dict(seed=3, size=6.0, tufts=5, height=0.4), False),
    'square_monument': (square.monument, dict(seed=1), True),
    'square_fountain': (square.fountain, dict(seed=2), True),
    'square_bench_a': (square.bench, dict(seed=3), False),
    'square_bench_b': (square.bench, dict(seed=8), False),
    'square_kiosk': (square.kiosk, dict(seed=5), True),
    'road_crack_long_a': (lambda lod=0, n='crack_long_a': road_decals.decal(n, lod), dict(), False),
    'road_crack_long_b': (lambda lod=0, n='crack_long_b': road_decals.decal(n, lod), dict(), False),
    'road_crack_cross': (lambda lod=0, n='crack_cross': road_decals.decal(n, lod), dict(), False),
    'road_crack_branch': (lambda lod=0, n='crack_branch': road_decals.decal(n, lod), dict(), False),
    'road_crack_net_a': (lambda lod=0, n='crack_net_a': road_decals.decal(n, lod), dict(), False),
    'road_crack_net_b': (lambda lod=0, n='crack_net_b': road_decals.decal(n, lod), dict(), False),
    'road_pothole_rim': (lambda lod=0, n='pothole_rim': road_decals.decal(n, lod), dict(), False),
    'road_tar_patch': (lambda lod=0, n='tar_patch': road_decals.decal(n, lod), dict(), False),
}


def build(name):
    fn, kw, has_lod = MODELS[name]
    os.makedirs(OUT, exist_ok=True)
    mesh = fn(**kw)
    glb.write_glb(os.path.join(OUT, f'{name}.glb'), mesh, tex_rel_dir='../textures', name=name)
    lo, hi = mesh.bounds()
    msg = f'{name}: {mesh.triangles()} tris, size {hi[0]-lo[0]:.2f} x {hi[1]-lo[1]:.2f} x {hi[2]-lo[2]:.2f} m (min y {lo[1]:.2f})'
    if has_lod:
        lod = fn(**kw, lod=1)
        glb.write_glb(os.path.join(OUT, f'{name}_lod1.glb'), lod, tex_rel_dir='../textures', name=name + '_lod1')
        msg += f', lod1 {lod.triangles()} tris'
    print(msg)


if __name__ == '__main__':
    mats.ensure_textures()
    names = sys.argv[1:] or list(MODELS)
    for n in names:
        build(n)
