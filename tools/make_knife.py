#!/usr/bin/env python3
"""Placeholder combat knife (CC0, made for this project) until a downloaded model replaces it.

Writes assets/models/weapons/knife.glb: blade along +Z, edge down (-Y), units in metres,
grip centred at the origin. Two PBR materials: steel and rubber grip.
"""
import json, math, struct, sys
from pathlib import Path

def box(cx, cy, cz, sx, sy, sz):
    return prism([(cx - sx/2, cy - sy/2), (cx + sx/2, cy - sy/2), (cx + sx/2, cy + sy/2), (cx - sx/2, cy + sy/2)],
                 cz - sz/2, cz + sz/2, None)

def prism(outline, z0, z1, tip):
    """Extrude a CCW outline (x, y) from z0 to z1; `tip` (x, y) collapses the z1 cap to a point/edge."""
    tris = []
    n = len(outline)
    back = [(x, y, z0) for x, y in outline]
    front = [(x, y, z1) if tip is None else (tip[i][0], tip[i][1], z1) for i, (x, y) in enumerate(outline)]
    for i in range(n):
        j = (i + 1) % n
        tris += [(back[i], back[j], front[j]), (back[i], front[j], front[i])]
    for i in range(1, n - 1):
        tris.append((back[0], back[i + 1], back[i]))
        tris.append((front[0], front[i], front[i + 1]))
    return tris

def blade():
    # Cross-section: thin spine on top, sharp edge below. Tip: point near the spine.
    h, t = 0.032, 0.0045
    outline = [(0, -h/2), (t/2, h/2 - 0.004), (t/2, h/2), (-t/2, h/2), (-t/2, h/2 - 0.004)]
    tip = [(0, h/2 - 0.006), (0.0004, h/2), (0.0004, h/2), (-0.0004, h/2), (-0.0004, h/2 - 0.001)]
    body = prism(outline, 0.065, 0.20, None)
    point = prism([(x, y) for x, y in outline], 0.20, 0.245, tip)
    return body + point

def mesh(tris):
    pos, nrm = [], []
    for a, b, c in tris:
        u = [b[k] - a[k] for k in range(3)]
        v = [c[k] - a[k] for k in range(3)]
        n = [u[1]*v[2] - u[2]*v[1], u[2]*v[0] - u[0]*v[2], u[0]*v[1] - u[1]*v[0]]
        l = math.sqrt(sum(x*x for x in n)) or 1.0
        n = [x / l for x in n]
        for p in (a, b, c):
            pos.append(p); nrm.append(n)
    return pos, nrm

steel = blade() + box(0, 0.0, 0.06, 0.012, 0.05, 0.008) + box(0, 0, -0.065, 0.02, 0.026, 0.01)  # blade, guard, pommel
grip = box(0, 0, 0.0, 0.02, 0.026, 0.12)
parts = [("steel", steel, [0.62, 0.63, 0.65, 1], 1.0, 0.32), ("grip", grip, [0.05, 0.05, 0.045, 1], 0.0, 0.85)]

binary = bytearray()
accessors, views, meshes_prims, materials = [], [], [], []
for idx, (name, tris, color, metal, rough) in enumerate(parts):
    pos, nrm = mesh(tris)
    for data, kind in ((pos, "POSITION"), (nrm, "NORMAL")):
        off = len(binary)
        for p in data:
            binary += struct.pack("<3f", *p)
        views.append({"buffer": 0, "byteOffset": off, "byteLength": len(binary) - off, "target": 34962})
        acc = {"bufferView": len(views) - 1, "componentType": 5126, "count": len(data), "type": "VEC3"}
        if kind == "POSITION":
            acc["min"] = [min(p[k] for p in data) for k in range(3)]
            acc["max"] = [max(p[k] for p in data) for k in range(3)]
        accessors.append(acc)
    meshes_prims.append({"attributes": {"POSITION": len(accessors) - 2, "NORMAL": len(accessors) - 1}, "material": idx})
    materials.append({"name": name, "pbrMetallicRoughness": {"baseColorFactor": color, "metallicFactor": metal,
                                                              "roughnessFactor": rough}})
while len(binary) % 4:
    binary += b"\0"
gltf = {"asset": {"version": "2.0", "generator": "tools/make_knife.py"}, "scene": 0,
        "scenes": [{"nodes": [0]}], "nodes": [{"name": "knife", "mesh": 0}],
        "meshes": [{"name": "knife", "primitives": meshes_prims}], "materials": materials,
        "accessors": accessors, "bufferViews": views, "buffers": [{"byteLength": len(binary)}]}
js = json.dumps(gltf, separators=(",", ":")).encode()
while len(js) % 4:
    js += b" "
out = Path(sys.argv[1] if len(sys.argv) > 1 else "assets/models/weapons/knife.glb")
with open(out, "wb") as f:
    f.write(struct.pack("<III", 0x46546C67, 2, 12 + 8 + len(js) + 8 + len(binary)))
    f.write(struct.pack("<II", len(js), 0x4E4F534A)); f.write(js)
    f.write(struct.pack("<II", len(binary), 0x004E4942)); f.write(binary)
print("wrote", out)
