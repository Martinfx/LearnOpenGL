# Generated zone models

Built by `../source/build_zone.py` (CC0): `python3 -I assets/models/zone/source/build_zone.py [name ...]`.
Metres, +Y up, ground at y = 0, centred on X/Z; every model with detail has a `_lod1.glb` (attached automatically
when placed in the editor). Textures are shared files in `../textures/` (referenced by URI, 1K).
Collision is the exact mesh (Jolt), so everything you see you can walk on, climb or stand behind.

| Files | What |
|---|---|
| `car_sedan_*`, `car_estate_*`, `car_van_green`, `car_burnt` | rusted Soviet-style wrecks, 4.3 m, seats inside, some wheels missing |
| `bus_small_*` (7 m), `bus_long_*` (10.7 m) | city buses, enterable through the right-side doors, seats |
| `rail_carriage_green/rust` | 24 m compartment carriage, floor 1.3 m, steps and doors at both ends, corridor + compartments with berths; place at y = 0.33 on `rail_track_25m` |
| `rail_locomotive`, `rail_tender` | steam locomotive with open cab (climbable steps) and tender |
| `rail_track_25m` | sleepers on ballast, rails at 1.52 m, rail top 0.36 m |
| `apartment_khrushchevka` | 4-storey brick apartment block, 2 entrances, switchback stairs to every floor, 2 flats per floor (rooms, doors), back balconies. 26 x 12 m |
| `hospital` | 3-storey brick hospital 48 x 15 m: corridor with wards (beds), reception, stairwells at both ends, fire escape on the west end, entrance canopy with Cyrillic sign |
| `machine_hall` | 48 x 21 x 15 m turbine hall: gates at both ends, side doors, two turbine sets, crane, mezzanine with offices and a stair, partly collapsed roof |
| `factory_hall` | 36 x 18 x 11 m workshop, same structure with machines and shelving |
| `cooling_tower`, `cooling_tower_broken` | 90 m hyperboloid towers (62 m base) on diagonal legs: walk in between the legs; the broken one has a fallen-out section and debris inside |
| `pipe_rack_24m`, `conveyor_fallen` | rusted pipe bridge with a catwalk; inclined conveyor gallery lying on the ground |
| `bridge_broken` | 86 m road bridge (deck 12 m, road level), one span collapsed into the river ravine |
| `river_80m` | water 7.7 m below ground in a 74 m wide ravine with mud banks (tile along X) |
| `skyline_tower_a/b/c` | ruined panel high-rises (48 to 84 m) with collapsed tops, for the city |
| `sign_gantry`, `lamp_post`, `lamp_post_leaning`, `guardrail_24m`, `station_sign`, `warning_sign` | road and rail furniture (Cyrillic signs) |
| `birch_*`, `dead_tree_*`, `bush_*`, `grass_*` | low-poly geometric vegetation (no alpha test needed) |
| `rubble_a/b`, `slab_fallen`, `puddle_a/b` | debris and reflective puddles |
