# Railway pieces

Procedural, made for this project (CC0), by `source/railgen.py`. Textures come from `assets/textures/pbr`
(Poly Haven CC0: `rocks_ground_02` ballast, `weathered_brown_planks` timber sleepers, `rusty_metal_02` rails).

Metres, +Y up. Track runs along +X, centred on the origin; the bottom of the ballast bed is at y = 0, the top of the
rails at y = 0.65. Gauge is the Soviet 1520 mm. Sleepers every 0.55 m.

| File | What | Size (m) | Tris |
|---|---|---|---|
| `rail_straight_12m.glb` | straight section, tile every 12.5 m along X | 12.5 x 4.6 x 0.65 | 980 |
| `rail_straight_3m.glb` | short filler section | 3 x 4.6 x 0.65 | 272 |
| `rail_curve_r150_12m.glb` | 12.5 m of a 150 m radius curve, bends toward +Z (4.77 degrees per piece) | 12.7 x 4.7 x 0.65 | 980 |
| `rail_buffer_stop.glb` | buffer stop on a 3 m ballast bed, the beam faces +X | 3.4 x 4.6 x 1.2 | 72 |

`../sign_gantry_18m.glb` (same script): highway overhead sign gantry, two lattice posts 18 m apart, 5.5 m clearance,
two blank blue panels facing -Z; 2088 tris.
