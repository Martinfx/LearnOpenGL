#!/usr/bin/env python3
"""Procedural artifact models (original designs) for assets/models/artifacts/*.gltf.

Each artifact is one small glowing object (10-16 cm, 1 unit = 1 m) written as a glTF with an
embedded buffer and an emissive material. Run from the repository root:
    python3 tools/make_artifacts.py
"""
import base64, json, math, os, struct

OUT = os.path.join("assets", "models", "artifacts")


def normalize(v):
    l = math.sqrt(sum(c * c for c in v)) or 1.0
    return [c / l for c in v]


def sub(a, b): return [a[i] - b[i] for i in range(3)]
def cross(a, b): return [a[1]*b[2]-a[2]*b[1], a[2]*b[0]-a[0]*b[2], a[0]*b[1]-a[1]*b[0]]


def uv_sphere(radius_fn, rings=18, segs=28, center=(0, 0, 0)):
    """Sphere whose radius depends on direction: radius_fn(theta, phi) -> r."""
    pos, idx = [], []
    for i in range(rings + 1):
        th = math.pi * i / rings
        for j in range(segs + 1):
            ph = 2 * math.pi * j / segs
            d = [math.sin(th) * math.cos(ph), math.cos(th), math.sin(th) * math.sin(ph)]
            r = radius_fn(th, ph)
            pos.append([center[k] + d[k] * r for k in range(3)])
    for i in range(rings):
        for j in range(segs):
            a = i * (segs + 1) + j
            b = a + segs + 1
            idx += [a, a + 1, b, b, a + 1, b + 1]
    return pos, idx


def tube(path_fn, radius, steps=160, sides=14):
    """Tube along a closed curve path_fn(t), t in 0..1."""
    pos, idx = [], []
    pts = [path_fn(i / steps) for i in range(steps)]
    for i in range(steps):
        p, n = pts[i], pts[(i + 1) % steps]
        t = normalize(sub(n, p))
        up = [0, 1, 0] if abs(t[1]) < 0.9 else [1, 0, 0]
        b = normalize(cross(t, up))
        nn = cross(b, t)
        for j in range(sides):
            a = 2 * math.pi * j / sides
            r = radius(i / steps) if callable(radius) else radius
            pos.append([p[k] + (math.cos(a) * b[k] + math.sin(a) * nn[k]) * r for k in range(3)])
    for i in range(steps):
        for j in range(sides):
            a = i * sides + j
            b2 = ((i + 1) % steps) * sides + j
            c = i * sides + (j + 1) % sides
            d = ((i + 1) % steps) * sides + (j + 1) % sides
            idx += [a, c, b2, b2, c, d]
    return pos, idx


def merge(parts):
    pos, idx = [], []
    for p, i in parts:
        base = len(pos)
        pos += p
        idx += [k + base for k in i]
    return pos, idx


def normals(pos, idx):
    n = [[0.0, 0.0, 0.0] for _ in pos]
    for t in range(0, len(idx), 3):
        a, b, c = idx[t:t + 3]
        f = cross(sub(pos[b], pos[a]), sub(pos[c], pos[a]))
        for k in (a, b, c):
            for m in range(3):
                n[k][m] += f[m]
    return [normalize(v) for v in n]


def write(name, mesh, base, emissive, metal=0.1, rough=0.35):
    pos, idx = mesh
    # Lift so the object rests on y = 0.
    ymin = min(p[1] for p in pos)
    pos = [[p[0], p[1] - ymin, p[2]] for p in pos]
    nor = normals(pos, idx)
    # Outward-facing check: flip the winding if most normals point inward.
    cx = [sum(p[k] for p in pos) / len(pos) for k in range(3)]
    inward = sum(1 for p, n in zip(pos, nor) if sum((p[k] - cx[k]) * n[k] for k in range(3)) < 0)
    if inward > len(pos) / 2:
        idx = [idx[t + o] for t in range(0, len(idx), 3) for o in (0, 2, 1)]
        nor = [[-c for c in n] for n in nor]
    pb = b"".join(struct.pack("<3f", *p) for p in pos)
    nb = b"".join(struct.pack("<3f", *n) for n in nor)
    ib = b"".join(struct.pack("<I", i) for i in idx)
    buf = pb + nb + ib
    lo = [min(p[k] for p in pos) for k in range(3)]
    hi = [max(p[k] for p in pos) for k in range(3)]
    gltf = {
        "asset": {"version": "2.0", "generator": "tools/make_artifacts.py"},
        "scene": 0, "scenes": [{"nodes": [0]}],
        "nodes": [{"mesh": 0, "name": name}],
        "meshes": [{"name": name, "primitives": [{"attributes": {"POSITION": 0, "NORMAL": 1}, "indices": 2, "material": 0}]}],
        "materials": [{"name": name, "pbrMetallicRoughness": {"baseColorFactor": base + [1.0], "metallicFactor": metal, "roughnessFactor": rough},
                       "emissiveFactor": emissive}],
        "buffers": [{"byteLength": len(buf), "uri": "data:application/octet-stream;base64," + base64.b64encode(buf).decode()}],
        "bufferViews": [
            {"buffer": 0, "byteOffset": 0, "byteLength": len(pb), "target": 34962},
            {"buffer": 0, "byteOffset": len(pb), "byteLength": len(nb), "target": 34962},
            {"buffer": 0, "byteOffset": len(pb) + len(nb), "byteLength": len(ib), "target": 34963}],
        "accessors": [
            {"bufferView": 0, "componentType": 5126, "count": len(pos), "type": "VEC3", "min": lo, "max": hi},
            {"bufferView": 1, "componentType": 5126, "count": len(nor), "type": "VEC3"},
            {"bufferView": 2, "componentType": 5125, "count": len(idx), "type": "SCALAR"}],
    }
    with open(os.path.join(OUT, name + ".gltf"), "w") as f:
        json.dump(gltf, f, separators=(",", ":"))
    print(name, len(pos), "vertices", [round(hi[k] - lo[k], 3) for k in range(3)], "m")


def main():
    os.makedirs(OUT, exist_ok=True)
    # Spring (vent): a warm droplet with ribs running up from the base.
    write("spring", uv_sphere(lambda th, ph: 0.055 * (1 + 0.35 * math.cos(th)) * (1 + 0.06 * math.sin(7 * ph)) * (0.75 + 0.25 * math.sin(th))),
          [0.9, 0.55, 0.2], [1.0, 0.55, 0.15])
    # Spark (discharge): a blue core bristling with spikes.
    def spiky(th, ph):
        s = max(0.0, math.sin(5 * ph) * math.sin(4 * th)) ** 6
        return 0.04 + 0.05 * s
    write("spark", uv_sphere(spiky, rings=40, segs=60), [0.35, 0.6, 1.0], [0.3, 0.6, 1.0], metal=0.6, rough=0.25)
    # Husk (acid): a cluster of fused green bubbles.
    bubbles = [((0, 0, 0), 0.045), ((0.04, 0.02, 0.01), 0.032), ((-0.03, 0.035, 0.02), 0.028),
               ((0.0, 0.05, -0.03), 0.026), ((-0.02, -0.01, -0.04), 0.03), ((0.03, -0.015, -0.03), 0.022)]
    write("husk", merge([uv_sphere(lambda th, ph, r=r: r, 12, 18, c) for c, r in bubbles]), [0.45, 0.85, 0.3], [0.35, 0.8, 0.15], rough=0.6)
    # Knot (crusher): a dark knot squeezed into itself.
    def knot(t):
        a = 2 * math.pi * t
        p, q = 2, 3
        r = 0.04 + 0.018 * math.cos(q * a)
        return [r * math.cos(p * a), 0.018 * math.sin(q * a), r * math.sin(p * a)]
    write("knot", tube(knot, 0.011), [0.35, 0.25, 0.45], [0.45, 0.2, 0.7], metal=0.3, rough=0.4)
    # Shard (rift): a twisted crystal, pale and cold.
    def shard(th, ph):
        h = math.cos(th)
        facet = 1 + 0.18 * abs(math.cos(3 * (ph + 1.2 * h)))
        return 0.03 * facet * (0.6 + 2.2 * abs(h) ** 3)
    write("shard", uv_sphere(shard, rings=30, segs=36), [0.75, 0.95, 1.0], [0.5, 0.85, 1.0], metal=0.2, rough=0.1)


if __name__ == "__main__":
    main()
