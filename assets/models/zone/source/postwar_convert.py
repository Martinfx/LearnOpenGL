"""Converts Aurelien Martel's "Postwar City - Exterior Scene" (Sketchfab, CC-BY 4.0) for the engine.

    python3 -I assets/models/zone/source/postwar_convert.py /path/to/extracted/scene.gltf

Needs `pip install bpy`. Drops the sky dome and the far photo backdrops (levels provide their own
surroundings), downsizes textures to 2K, converts the specular/glossiness materials to metal/roughness
(Blender importer/exporter) and splits the result into
  postwar/postwar_street.glb         facades, road, sidewalks, barriers, cars, rubble (JPEG textures)
  postwar/postwar_street_alpha.glb   grass, plants, ivy leaves, metal fence (alpha-masked cards)
so the alpha cards can be drawn without back-face culling and excluded from collision.
Axes: street runs along -Z from z = +3 to z = -45, metres, ground at y ~ 0.
"""
import os
import sys
import bpy

SRC = sys.argv[-1]
OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'postwar')
ALPHA = ('grass', 'NaturePlants', 'ivy-leaves', 'MetalFences')
DROP = ('ciel', 'wall', 'wall.001', 'wall.002', 'wall.003', 'wall.buidings', 'wall.buidings.001',
        'wall.buidings.002', 'wall.buidings.003')

bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.gltf(filepath=SRC)
for o in list(bpy.context.scene.objects):
    if o.type == 'MESH' and o.data.materials and o.data.materials[0].name in DROP:
        bpy.data.objects.remove(o, do_unlink=True)
for img in bpy.data.images:
    cap = 1024 if any(k.lower() in img.name.lower() for k in ALPHA) else 2048
    if img.size[0] > cap:
        img.scale(cap, int(cap * img.size[1] / img.size[0]))


def is_alpha(o):
    return o.type == 'MESH' and o.data.materials and any(k in o.data.materials[0].name for k in ALPHA)


def export(name, pred, fmt):
    bpy.ops.object.select_all(action='DESELECT')
    for o in bpy.context.scene.objects:
        if o.type == 'MESH' and pred(o):
            o.select_set(True)
    bpy.ops.export_scene.gltf(filepath=os.path.join(OUT, name), export_format='GLB', use_selection=True,
                              export_image_format=fmt, export_apply=True, export_yup=True)


os.makedirs(OUT, exist_ok=True)
export('postwar_street.glb', lambda o: not is_alpha(o), 'JPEG')
export('postwar_street_alpha.glb', is_alpha, 'AUTO')
