# First-person view models (arms + weapon)

Each file is one skinned glTF (GLB): realistic arms (dirty bare hands, dark green knitted sweater sleeves, S.T.A.L.K.E.R.-style look) and the weapon share one skeleton.

| File | Weapon | Clips (name: length) |
|---|---|---|
| `fp_ak74.glb` | AK-74M, professional arms + animations by Cransh (CC-BY 4.0), gloves and jacket | `idle` 1.96 s loop, `fire` 0.29 s, `reload` 2.63 s, `reload_empty` 3.46 s (with bolt pull), `reload_alt` 2.79 s, `draw` 0.63 s, `walk` 0.96 s loop, `run` 0.38 s loop |
| `fp_pistol.glb` | Service pistol (Walther P38-style) | `idle` 2.0 s loop, `fire` 0.27 s, `reload` 2.0 s, `draw` 0.5 s, `holster` 0.4 s |
| `fp_smg.glb` | Compact SMG (olive drab) | `idle` 2.0 s loop, `fire` 0.13 s (repeat for full auto), `reload` 2.5 s, `draw` 0.5 s, `holster` 0.4 s |
| `fp_knife.glb` | M9 combat knife | `idle` 2.0 s loop, `slash` 0.6 s, `stab` 0.5 s, `draw` 0.4 s, `holster` 0.33 s |

`fp_ak74.glb` is the quality reference. `fp_pistol.glb`, `fp_smg.glb` and `fp_knife.glb` are hand-keyed placeholders
until the matching Cransh rigs (Makarov PM, SMG) are added.

`fp_ak74.glb` notes: converted with `source/convert_fps.py` (eye = the rig's `Head_Cam_014` bone at idle frame 0,
scaled so the rifle is 0.943 m). The weapon is held almost centred (muzzle about `0.03 -0.06 -0.68`).
Bones: `PBody_058` (weapon body), `Pmag_062` (magazine), `Pmag2_063` (spare magazine used in reloads),
`Trigger_060`, `Hand_R_037` / `Hand_L_016`, `Head_Cam_014` (the animator's camera; its motion can drive camera shake).

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
