#!/usr/bin/env python3
"""Pickup world models: ammo can, pistol cartridge box, first-aid pouch, bandage roll (headless Blender, CC0).

    python3 -I assets/models/props/pickups/source/pickups.py /tmp/clutter/tex

Reuses the helpers of ../../clutter/source/clutter.py (materials from <tex>/<name>_{albedo,normal,roughness}.jpg,
glb export with the origin at the bottom centre, 1 unit = 1 m). Textures: Poly Haven CC0 green_metal_rust (can),
hessian_230 tinted olive / off-white (pouch, bandage); the first-aid patch (green cross, not the protected red cross), labels and cardboard are procedural.
"""
import os,sys,math
HERE=os.path.dirname(os.path.abspath(__file__)); OUT=os.path.normpath(os.path.join(HERE,'..'))
src=open(os.path.join(HERE,'..','..','clutter','source','clutter.py')).read().split('# ------------------------------------------------------------------ props')[0]
g={'__file__':os.path.join(HERE,'..','..','clutter','source','clutter.py'),'__name__':'pickups'}; exec(src,g)
g['OUT']=OUT; bpy=g['bpy']; box=g['box']; mat=g['mat']; reset=g['reset']; export=g['export']; cube_uv=g['cube_uv']

def ammo_can():   # US/NATO-style .30 cal can: body, lid with a flat handle, side latch
    reset(); m=mat('can')
    box(0.27,0.10,0.165,(0,0,0.0825),m=m,uv=0.3)
    box(0.275,0.105,0.02,(0,0,0.175),m=m,uv=0.3)                       # lid
    box(0.12,0.03,0.008,(0,0,0.193),m=m,uv=0.3)                        # folded handle
    box(0.012,0.035,0.06,(0.141,0,0.14),m=m,uv=0.3)                    # latch lever
    export('ammo_can_rifle',mass=3.5,collider='box',kind='dynamic')
def pistol_box():  # 50-round cartridge box, card
    reset(); box(0.10,0.06,0.042,(0,0,0.021),m=mat('ammobox'),uv=0.1); export('ammo_box_pistol',mass=0.6,collider='box',kind='dynamic')
def medkit():      # canvas first-aid pouch with a flap and a belt loop
    reset(); m=mat('canvas')
    o=box(0.24,0.08,0.16,(0,0,0.08),m=m,uv=0.24)
    bpy.ops.object.modifier_add(type='BEVEL'); o.modifiers[-1].width=0.015; o.modifiers[-1].segments=2; bpy.ops.object.modifier_apply(modifier=o.modifiers[-1].name)
    cube_uv(o,0.08)
    box(0.245,0.006,0.07,(0,-0.043,0.13),rot=(math.radians(4),0,0),m=m,uv=0.08)   # flap over the front
    box(0.06,0.012,0.10,(0,0.046,0.09),m=m,uv=0.08)                               # belt loop on the back
    bpy.ops.mesh.primitive_plane_add(size=0.06,location=(0,-0.0475,0.128),rotation=(math.radians(94),0,0))   # sewn-on patch
    pa=bpy.context.object; pa.data.materials.append(mat('patch'))
    export('medkit_pouch',mass=0.7,collider='box',kind='dynamic')
def bandage():
    reset(); m=mat('bandage')
    bpy.ops.mesh.primitive_cylinder_add(vertices=20,radius=0.032,depth=0.08,location=(0,0,0.032),rotation=(0,math.radians(90),0))
    o=bpy.context.object; cube_uv(o,0.1); o.data.materials.append(m)
    box(0.07,0.12,0.0015,(0,-0.07,0.0008),rot=(0,0,0.15),m=m,uv=0.1)               # loose unrolled end
    export('bandage_roll',mass=0.05,collider='box',kind='dynamic')
for f in (ammo_can,pistol_box,medkit,bandage): f()
import json; json.dump(g['MAN'],open(os.path.join(OUT,'pickups.json'),'w'),indent=1)
