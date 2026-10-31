#!/usr/bin/env python3
"""Converts an OpenStreetMap extract (.osm XML from api.openstreetmap.org/api/0.6/map) into local
level coordinates in metres: +X = east, +Z = south (north is up on the plan), origin at LAT0/LON0.

    python3 -I osm_to_local.py skrunda1.osm skrunda1.json 56.7272 21.9935

Output: buildings (polygon, tags, oriented bounding rectangle), roads/paths (polylines),
areas (landuse/natural polygons), barriers. Map data (c) OpenStreetMap contributors, ODbL 1.0.
"""
import json
import math
import sys
import xml.etree.ElementTree as ET

src, out, LAT0, LON0 = sys.argv[1], sys.argv[2], float(sys.argv[3]), float(sys.argv[4])
R = 6378137.0
root = ET.parse(src).getroot()
nodes = {n.get('id'): (float(n.get('lat')), float(n.get('lon'))) for n in root.iter('node')}


def xz(ll):
    return (round((ll[1] - LON0) * math.pi / 180 * R * math.cos(LAT0 * math.pi / 180), 2),
            round(-(ll[0] - LAT0) * math.pi / 180 * R, 2))


def obb(pts):
    """Minimum-area bounding rectangle: centre, length (long side), width, yaw of the long side
    in degrees (engine convention: yaw 0 = +X, 90 = +Z)."""
    best = None
    n = len(pts)
    for i in range(n):
        (ax, az), (bx, bz) = pts[i], pts[(i + 1) % n]
        a = math.atan2(bz - az, bx - ax)
        c, s = math.cos(a), math.sin(a)
        us = [x * c + z * s for x, z in pts]
        vs = [-x * s + z * c for x, z in pts]
        area = (max(us) - min(us)) * (max(vs) - min(vs))
        if best is None or area < best[0]:
            best = (area, a, min(us), max(us), min(vs), max(vs))
    _, a, u0, u1, v0, v1 = best
    c, s = math.cos(a), math.sin(a)
    uc, vc = (u0 + u1) / 2, (v0 + v1) / 2
    cx, cz = uc * c - vc * s, uc * s + vc * c
    L, W = u1 - u0, v1 - v0
    yaw = math.degrees(a)
    if W > L:
        L, W, yaw = W, L, yaw + 90
    yaw = (yaw + 90) % 180 - 90
    return {'cx': round(cx, 2), 'cz': round(cz, 2), 'length': round(L, 2), 'width': round(W, 2), 'yaw': round(yaw, 2)}


res = {'origin': [LAT0, LON0], 'attribution': 'Map data (c) OpenStreetMap contributors, ODbL 1.0',
       'buildings': [], 'roads': [], 'areas': [], 'barriers': []}
for w in root.iter('way'):
    tags = {t.get('k'): t.get('v') for t in w.iter('tag')}
    pts = [xz(nodes[n.get('ref')]) for n in w.iter('nd') if n.get('ref') in nodes]
    if len(pts) < 2:
        continue
    closed = len(pts) > 3 and pts[0] == pts[-1]
    poly = pts[:-1] if closed else pts
    item = {'id': w.get('id'), 'tags': tags, 'pts': poly}
    if 'building' in tags or 'building:part' in tags:
        item['obb'] = obb(poly)
        res['buildings'].append(item)
    elif 'highway' in tags or 'railway' in tags:
        res['roads'].append(item)
    elif 'barrier' in tags:
        res['barriers'].append(item)
    elif closed and any(k in tags for k in ('landuse', 'natural', 'man_made', 'leisure', 'amenity', 'water')):
        res['areas'].append(item)
json.dump(res, open(out, 'w'), indent=0)
print(len(res['buildings']), 'buildings', len(res['roads']), 'roads', len(res['areas']), 'areas', len(res['barriers']), 'barriers')
