"""Thrown incendiary bottle (world model): blender-less bpy run
    python3 -I build_world.py   ->  ../molotov_bottle.glb
Origin at the bottle's centre of mass (it tumbles about it); Y up after export, metres."""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import bpy
bpy.ops.wm.read_factory_settings(use_empty=True)
import bottle
objs = bottle.build()
for o in objs:
    o.location.z -= 0.11  # centre of mass (fuel in the body)
bpy.ops.object.select_all(action='DESELECT')
for o in objs:
    o.select_set(True)
out = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'molotov_bottle.glb'))
bpy.ops.export_scene.gltf(filepath=out, use_selection=True, export_apply=True, export_yup=True, export_image_format='AUTO')
print('wrote', out)
