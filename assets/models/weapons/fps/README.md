# First-person view models (arms + weapon)

Each file is one skinned glTF (GLB): realistic bare arms and the weapon share one skeleton.

| File | Weapon | Clips (name: length) |
|---|---|---|
| `fp_pistol.glb` | Service pistol (Walther P38-style) | `idle` 2.0 s loop, `fire` 0.27 s, `reload` 2.0 s, `draw` 0.5 s, `holster` 0.4 s |
| `fp_smg.glb` | Compact SMG (olive drab) | `idle` 2.0 s loop, `fire` 0.13 s (repeat for full auto), `reload` 2.5 s, `draw` 0.5 s, `holster` 0.4 s |
| `fp_knife.glb` | M9 combat knife | `idle` 2.0 s loop, `slash` 0.6 s, `stab` 0.5 s, `draw` 0.4 s, `holster` 0.33 s |

## Conventions

- The camera (eye) is at the origin, looking down **-Z**, **+Y** up, units are metres.
  In a `.weapon` file this means `view_offset 0 0 0`, `view_rotation 0 0 0`, `view_scale 1`.
- Bones: `weapon` (the weapon root, pivot at the grip), `magazine` (pistol, SMG), `slide`
  (pistol), `hand.R` / `hand.L` (wrists), `hand.R.control` / `hand.L.control` (former IK targets,
  baked). Attach muzzle flashes or sockets to `weapon`.
- Muzzle position in the rest pose (idle frame 0), eye space: pistol about `0.09 -0.07 -0.53`,
  SMG about `0.13 -0.20 -0.71`.
- The arms are bare skin; the reload hides the dropped magazine by moving it 2 m below the eye.

## Source and rebuilding

The animations are keyframed procedurally in Blender (bpy) by the scripts in `source/`
(`fp_pistol.py`, `smg_cfg.py`, `fp_knife.py`; `fp_base.py`, `fp_build.py`, `fp_gun.py` are shared
helpers). They expect the original downloads listed in `CREDITS.md` and are kept as a reference
for tweaking poses and timings; the GLB files are the result.
