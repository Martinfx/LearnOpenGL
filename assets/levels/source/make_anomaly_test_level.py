#!/usr/bin/env python3
"""Writes assets/levels/anomaly_test.level: a walled testing ground with one station per anomaly kind
(crusher, discharge, vent, acid, linked rift, radiation hotspot and area) and a guard who chases you
through them. Run from the repository root: python3 assets/levels/source/make_anomaly_test_level.py"""
import math
L=[]
a=L.append
src=open('assets/levels/test.level').read().splitlines()
a("# FPS engine level")
a("version 5")
a("player_start 0 0 20")
for line in src:
    if line.startswith(('sun ','env ')): a(line)
a('loading_image "../textures/ui/loading_zone.jpg"')
a('loading_title "Zóna: zkušební polygon anomálií"')
a('loading_hint "G hází šroub. Detektor pípá u anomálií, geiger cvaká v radiaci."')
for w in ("ak74","pistol","knife"): a(f'weapon "../weapons/{w}.weapon"')
for g,p in (("Ground",""),("Walls","Ground"),("Props",""),("Anomalies",""),("Enemies",""),("Map markers",""),("Ambience","")):
    a(f'group "{g}" "{p}"')
def box(name,x,y,z,sx,sy,sz,c=(0.6,0.6,0.56),parent="Props",mat=None,rot=None):
    a(f'box "{name}" {x} {y} {z} {sx} {sy} {sz} {c[0]} {c[1]} {c[2]}')
    a(f'  parent "{parent}"')
    if rot: a(f'  rotation {rot[0]} {rot[1]} {rot[2]}')
    if mat: a(f'  material "../materials/{mat}.material"')
box("Floor",0,-0.5,0,56,1,64,(0.5,0.48,0.42),"Ground","pbr/dry_ground_01")
for n,x,z,sx,sz in (("Wall N",0,-32,56,1),("Wall S",0,32,56,1),("Wall W",-28,0,1,64),("Wall E",28,0,1,64)):
    box(n,x,1.5,z,sx,3,sz,(0.5,0.5,0.5),"Walls","concrete")
# low dividers between stations (cover and lanes)
for i,(x,z,sx,sz) in enumerate(((-6,0,1,8),(6,0,1,8),(0,-12,10,1),(-18,-4,8,1),(18,-4,8,1))):
    box(f"Divider {i+1}",x,0.6,z,sx,1.2,sz,(0.55,0.53,0.5),"Walls","concrete")
# props for the stations
box("Crate crusher A",-14.5,0.4,9,0.8,0.8,0.8,(0.6,0.45,0.25),"Props","prototype_orange")
box("Crate crusher B",-9,0.4,4,0.8,0.8,0.8,(0.6,0.45,0.25),"Props","prototype_orange")
box("Pylon base",0,0.15,6,0.6,0.3,0.6,(0.4,0.4,0.42),"Props","metal_plate")
box("Pylon",0,2.3,6,0.2,4,0.2,(0.4,0.4,0.42),"Props","metal_plate")
for k in range(4): # cracked slabs around the vent
    ang=k*90+20
    box(f"Vent slab {k+1}",12+math.cos(math.radians(ang))*1.6,0.05,6+math.sin(math.radians(ang))*1.6,1.4,0.1,0.9,(0.35,0.33,0.3),"Props","concrete",(0,ang,0))
box("Exit platform",16,1,-20,5,2,5,(0.5,0.5,0.5),"Props","concrete")
box("Barrel hot",-20,0.45,-24,0.6,0.9,0.6,(0.45,0.45,0.2),"Props","metal_plate")
box("Barrel hot 2",-19.2,0.45,-24.6,0.6,0.9,0.6,(0.45,0.45,0.2),"Props","metal_plate")
def anomaly(name,kind,x,z,sx,sy,sz,color,props=()):
    a(f'entity "{name}" "anomaly" {x} 0 {z} 0 {sx} {sy} {sz} {color[0]} {color[1]} {color[2]} ""')
    a('  parent "Anomalies"')
    a(f'  prop "kind" "{kind}"')
    for k,v in props: a(f'  prop "{k}" "{v}"')
anomaly("Crusher 1","crusher",-12,6,5,3,5,(0.55,0.5,0.75))
anomaly("Discharge 1","discharge",0,6,3.5,3,3.5,(0.35,0.6,1))
anomaly("Vent 1","vent",12,6,2,5,2,(1,0.45,0.15),(("period","6"),))
anomaly("Acid pool 1","acid",-12,-14,6,1,6,(0.45,0.85,0.2))
anomaly("Rift 1","rift",10,-14,2.5,2.5,2.5,(0.75,0.4,0.95),(("link","Rift exit"),))
a('entity "Rift exit" "particle" 16 2 -20 0 0.4 0.4 0.4 0.75 0.4 0.95 ""')
a('  parent "Anomalies"')
a('  prop "effect" "particles/anomaly_rift_idle.particle"')
anomaly("Radiation hotspot","radiation",-20,-24,8,4,8,(0.9,0.85,0.2),(("intensity","30"),))
anomaly("Radiation area","radiation",0,-24,10,4,8,(0.9,0.85,0.2),(("area","1"),("intensity","4"),("fade","3")))
# a guard behind the discharge: chasing the player he walks through the field
a('entity "Guard" "enemy" 0 0 -4 90 0.6 1.8 0.6 0.55 0.5 0.5 "../scripts/enemy.lua"')
a('  parent "Enemies"')
a('  character "../models/characters/humans/bandit.glb" 1 0 "Idle_Loop"')
for n,x,z in (("Drtič",-12,6),("Výboj",0,6),("Výheň",12,6),("Žíravina",-12,-14),("Zlom",10,-14),("Radiace",-20,-24)):
    a(f'entity "{n}" "map_marker" {x} 0 {z} 0 1 1 1 0.85 0.3 0.2 ""')
    a('  parent "Map markers"')
a('player_sounds "../sounds/player/step_gravel.sound" "../sounds/player/land.sound" "../sounds/player/hurt.sound"')
a('entity "Zone drone" "sound" 0 -50 0 0 400 200 400 0.25 0.55 0.95 ""')
a('  parent "Ambience"')
a('  prop "sound" "sounds/ambient/zone_drone.sound"')
a('  prop "zone" "1"')
a('  prop "fade" "20"')
open('assets/levels/anomaly_test.level','w').write("\n".join(L)+"\n")
