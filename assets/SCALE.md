# World scale

One scale for every asset, so that things fit together in the game and in the editor.

## Conventions

- **1 unit = 1 metre.** glTF axes: +Y up, ground at y = 0.
- Characters face +Z, feet at y = 0. Viewmodels (`weapons/fps/`) have the eye at the origin and look along -Z.
- Zone buildings and props are centred on X/Z and stand on y = 0.

## Reference sizes

| Thing | Size |
|---|---|
| Character height | 1.8 m (our humans measure 1.81 to 1.85 m) |
| Player eye height | 1.7 m |
| Door opening | 0.9 x 2.1 m |
| Storey (floor to floor) | 3 m (Soviet panel block about 2.8 to 3.1 m) |
| Stair step | 0.2 m high, 0.3 m deep |
| Crate | 0.5 m |
| Pistol | about 0.2 m long |
| Rifle | 0.9 to 1.25 m long |
| Car | about 4.3 m long |
| 20 ft shipping container | 6.06 x 2.44 x 2.59 m |
| Concrete fence panel PO-2 | 4 m panel, 2.5 m high (4.38 m section with posts) |

## Reference scene

`models/zone/scale_reference.glb` (built by `models/zone/source/scale_reference.py`, CC0) contains, from x = 0 to the right:
a 3 m pole striped every 0.5 m with markers at 1.7 m (eye, yellow) and 1.8 m (head, blue), a door frame with a
0.9 x 2.1 m opening, a 1 m cube, a wall slice with yellow floor lines at 0, 3 and 6 m, a 0.5 m crate and five 0.2 m stairs.
Place it next to a human (`characters/humans/*.glb`) in a test level to check any new asset by eye.

## Checking

`python3 tools/scale_audit.py models` prints width, height, depth and lowest Y of every model (LODs skipped).
Run it after adding assets; anything off by more than about 10 % from the table above gets rescaled before commit.

## Audit 2026-10-31

All models were measured. Fixed in this pass: `shipping_container` (was 2.0 m wide and 2.3 m high), `fence_concrete_po2`
(was 3.4 m high), `fence_grate` (was 3 m high). Everything else was already in metres at real size:
humans 1.81 to 1.85 m, UAL mannequin 1.83 m, AK 0.88 m, Mosin 1.23 m, Dragunov 1.22 m, M1911 0.22 m, casings at real
calibre size, panel block 49 m for 16 storeys, garages 2.75 m high, car 4.33 m, An-2 17.9 m wingspan.

Known inconsistency (not scale): world weapons in `weapons/world/` do not share one forward axis (AK and M4 lie along X,
the others along Z). Pickups should set orientation per model until they are unified.
