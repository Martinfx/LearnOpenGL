# Animals (fauna)

| File | Animal | Size | Triangles | Bones | Clips (30 fps) |
|---|---|---|---|---|---|
| `wolf.glb` | feral dog / wolf, grey-brown dirty coat, amber eyes | 0.60 m at the withers, 1.79 m nose to tail tip | 5358 | 36 | Idle_Loop 3.0 s, Walk_Loop 1.0 s, Run_Loop 0.6 s (gallop), Attack_Bite 0.8 s, Hit 0.4 s, Death 1.5 s |
| `crow.glb` | crow / raven | 0.45 m beak to tail, 1.14 m wingspan | 2380 | 32 | Idle_Loop 3.0 s (on the ground, wings folded), Fly_Loop 0.73 s, Glide_Loop 2.0 s, TakeOff 0.8 s |

Conventions: metres, +Y up, the animal faces **+Z**, origin at the bottom centre (paws / feet at y = 0).
All clips are in place (no root motion). Loops have the same first and last frame.

- Wolf: the root bone is `b_Root`, the head is `b_Head`, the jaw is `b_Jaw` (the bite is at about 0.38 s of Attack_Bite).
  Death ends lying on the right side; hold the last frame.
- Crow: the root bone is `ROOT`. Fly_Loop and Glide_Loop hold the body about 0.3 m above the origin
  (the flight pose of the source rig); TakeOff goes from the folded ground pose to that height and ends in the
  first frame of Fly_Loop, so play TakeOff then Fly_Loop while moving the entity up and forward.

Textures: 1K JPEG embedded (wolf: albedo, normal, roughness; crow: albedo).

Rebuild (`pip install bpy`): `wolf_setup.py dog2.blend base.blend`, `wolf_anim.py base.blend anim.blend`,
`raven_setup.py raven.blend base.blend`, `raven_anim.py base.blend anim.blend raven_albedo.png`,
`export.py anim.blend out.glb`. The clips are authored in the scripts (IK paws for the gaits, then sampled to
plain bone keys); the crow's Fly_Loop is the source file's flight cycle, resampled.
Credits: `CREDITS.md`.
