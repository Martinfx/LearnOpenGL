# Bullet-hole decals

`bullet_holes_albedo.png` (RGBA, alpha = decal mask) and `bullet_holes_normal.png` (OpenGL convention, +Y up),
1024x1024 atlas of 4x4 cells (256 px each). Rows from the top:

| Row | Surface | Look |
|---|---|---|
| 0 | concrete / brick / stone | dark crater, chipped grey rim, dust |
| 1 | metal | dent with bright scraped ring and soot |
| 2 | wood | splintered fresh wood, stretched vertically along the grain |
| 3 | soft / generic (plaster, dirt, fabric) | small dark hole with soft scorch |

Columns are 4 random variants of the same surface. UV of cell (row r, col c): `u = (c + uv.x) / 4`, `v = (r + uv.y) / 4`
counted from the top of the image. Roughness: concrete 0.95, metal 0.35, wood 0.8, soft 0.9.

Made for this project by `source/decals.py` (procedural), **CC0 1.0**.

`scorch.png` (2x2 atlas of burnt-ground soot marks left by fire): made for this project by `source/scorch.py` (procedural), **CC0 1.0**.
