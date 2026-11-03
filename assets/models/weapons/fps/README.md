# First-person view models (arms + weapon)

Each file is one skinned glTF (GLB): realistic arms (dirty bare hands, dark green knitted sweater sleeves, S.T.A.L.K.E.R.-style look) and the weapon share one skeleton.

| File | Weapon | Clips (name: length) |
|---|---|---|
| `fp_ak74.glb` | AK-74M, professional arms + animations by Cransh (CC-BY 4.0), gloves and jacket | `idle` 1.96 s loop, `fire` 0.29 s, `reload` 2.63 s, `reload_empty` 3.46 s (with bolt pull), `reload_alt` 2.79 s, `draw` 0.63 s, `walk` 0.96 s loop, `run` 0.38 s loop |
| `fp_pistol.glb` | Makarov PM (plain, suppressor and red dot removed), Cransh (CC-BY 4.0), black gloves | `idle` 1.96 s loop, `fire` 0.29 s, `reload` 2.21 s, `reload_empty` 2.96 s, `walk` 0.96 s loop (no `draw`) |
| `fp_smg.glb` | Compact SMG (olive), Cransh (CC-BY 4.0), leather gloves | `idle` 1.63 s loop, `fire` 0.29 s, `reload` 3.92 s, `reload_empty` 4.38 s, `draw` 0.63 s, `inspect` 7.25 s, `walk` 0.71 s loop, `run` 0.46 s loop |
| `fp_knife.glb` | Combat knife, DJMaesen (CC-BY 4.0), gloves | `idle` 1.33 s loop, `slash` 0.67 s, `slash2` 0.67 s, `stab` 0.75 s, `slash3` 0.75 s, `draw` 0.33 s |
| `fp_service9.glb` | Classic 9 mm service pistol (Poly Haven, CC0) in the DJMaesen heavy pistol arms (CC-BY 4.0), gloves, two-hand grip | `idle` 0.83 s loop, `fire` 0.42 s, `fire_last` 0.42 s (slide locks back, held by the engine until the reload), `reload` 2.33 s (racks the slide at the end), `reload_empty` 1.75 s (from the locked slide, slide release), `draw` 1.04 s, `inspect` 0.92 s |
| `fp_heavy_pistol.glb` | Heavy .50 pistol, DJMaesen (CC-BY 4.0), bare hands, two-hand grip | `idle` 0.83 s loop, `fire` 0.42 s, `fire_last` 0.42 s (slide locks back), `reload` 2.42 s, `reload_empty` 1.75 s (from locked slide), `draw` 1.08 s, `inspect` 0.92 s |

`fp_ak74.glb`, `fp_pistol.glb` and `fp_smg.glb` are professional Cransh rigs. `fp_knife.glb` and `fp_heavy_pistol.glb` (DJMaesen) have their weapon parts (knife, slide, magazine, ...) baked onto extra `W_*` bones and rigidly skinned, so only skinning is needed. The knife viewmodel shows only the right arm (left arm removed) and sits 4 cm right, 5 cm lower and 6 cm further than the source camera. Heavy pistol muzzle about `0.04 -0.03 -0.50`; both use eye (6, 50, 12) in the source and scale 0.00657.

`fp_pistol.glb` notes: held slightly lower-right than the animator's camera (`post_offset 0.045 -0.02 -0.045` in the converter), muzzle about `0.08 -0.05 -0.50`, the arms mesh in the download had its vertices
in the wrong units (x -1e4); `source/fixgltf.py` repairs that before `source/convert_fps.py` runs.
`fp_smg.glb` notes: muzzle about `0.04 -0.04 -0.42`; the spare magazine (`Pmag_2_062`) is parked below the view outside reloads.
Both use the same bone names as the AK (`Head_Cam_014`, `PBody_058`, `Pmag_*`, `Hand_R_*`/`Hand_L_*`, `Trigger_060`).

`fp_ak74.glb` notes: converted with `source/convert_fps.py` (eye = the rig's `Head_Cam_014` bone at idle frame 0,
scaled so the rifle is 0.943 m). The weapon is held almost centred (muzzle about `0.03 -0.06 -0.68`).
Bones: `PBody_058` (weapon body), `Pmag_062` (magazine), `Pmag2_063` (spare magazine used in reloads),
`Trigger_060`, `Hand_R_037` / `Hand_L_016`, `Head_Cam_014` (the animator's camera; its motion can drive camera shake).

`fp_service9.glb` notes: built from `fp_heavy_pistol.glb` and `../world/service_pistol/` by
`python3 source/build_service9.py fp_heavy_pistol.glb ../world/service_pistol/service_pistol.gltf fp_service9.glb ../world/service9.gltf`
(no Blender needed). Every Talon part is swapped for the matching service pistol part on the same `W_*` bone (`W_base`, `W_slide`,
`W_hammer`, `W_trigger`, `W_clip`, `W_bullet*`; the slide catch is dropped), placed so trigger and back strap sit where the Talon's were.
Muzzle about `0.04 -0.024 -0.49`, sights lined up by `aim_offset -0.0397 0.0262 0` + `aim_rotation -3.47 0 0`.
The same script writes the world model `../world/service9.gltf` (gun alone with the magazine in, lying on its right side, shares
the Poly Haven textures).

## Conventions

- The camera (eye) is at the origin, looking down **-Z**, **+Y** up, units are metres.
  In a `.weapon` file this means `view_offset 0 0 0`, `view_rotation 0 0 0`, `view_scale 1`.
- Bones: `weapon` (the weapon root, pivot at the grip), `magazine` (pistol, SMG), `slide`
  (pistol), `hand.R` / `hand.L` (wrists), `hand.R.control` / `hand.L.control` (former IK targets,
  baked). Attach muzzle flashes or sockets to `weapon`.
- Weapon placement (idle frame 0, eye space): pistol grip at `0.10 -0.12 -0.30` (muzzle about 0.5 m ahead),
  SMG grip at `0.14 -0.27 -0.42`, knife grip at `0.17 -0.13 -0.36`.
- Grip: right hand closed around the grip with the index finger at the trigger; pistol is held with two hands
  (left hand wraps the right), SMG left hand on the vertical foregrip, knife in a full-hand grip.
- The reload hides the dropped magazine by moving it 2 m below the eye.
- Preview renders (eye camera, FOV 60°, 16:9) are produced with `source/` scripts; see the project files.

## Source and rebuilding

The animations are keyframed procedurally in Blender (bpy) by the scripts in `source/`
(`pistol_cfg.py`, `smg_cfg.py`, `fp_knife.py`; `fp_base.py`, `fp_build.py`, `fp_gun.py` are shared
helpers, `textures.py` makes the dirty hand and knit sleeve textures). They expect the original downloads listed in `CREDITS.md` and are kept as a reference
for tweaking poses and timings; the GLB files are the result.
