# Human characters (enemies / NPCs)

Realistic rigged people from the Microsoft Rocketbox avatar library (MIT licence), with the
Quaternius Universal Animation Library clips (CC0) retargeted onto their Biped skeleton by
`source/retarget.py`. Credits are in `../CREDITS.md`.

| File | Look | Rocketbox source |
|---|---|---|
| `soldier_heavy.glb` | soldier, helmet, plate carrier, knee pads | Military_Male_03 |
| `soldier.glb` | soldier, patrol cap, uniform | Military_Male_05 |
| `stalker.glb` | older man, utility vest over green shirt | Male_Adult_05 |
| `bandit.glb` | denim jacket, jeans | Male_Adult_12 |
| `worker.glb` | plaid shirt, hard hat, tool belt | Construction_Male_01 |

Conventions: metres, +Y up, the character faces **+Z**, feet at y = 0, about 1.8 m tall.
Skeleton is 3ds Max Biped (`Bip01 Pelvis`, `Bip01 Spine` ... `Bip01 R Hand`); attach a held
weapon to `Bip01 R Hand`, the head socket is `Bip01 Head`.

Clips (same names as `../library/ual_mannequin.glb`, 30 fps source):
Idle_Loop, Idle_Talking_Loop, Walk_Loop, Walk_Formal_Loop, Jog_Fwd_Loop, Sprint_Loop,
Crouch_Fwd_Loop, Crouch_Idle_Loop, Pistol_Idle_Loop, Pistol_Aim_Neutral, Pistol_Aim_Up,
Pistol_Aim_Down, Pistol_Shoot, Pistol_Reload, Punch_Jab, Punch_Cross, Hit_Chest, Hit_Head,
Death01, Interact, PickUp_Table, Fixing_Kneeling, Sitting_Idle_Loop, Jump_Start, Jump_Loop,
Jump_Land, Roll.

Rebuild: `python3 source/retarget.py ual_mannequin.glb <Rocketbox>/Export/<Name>.fbx out.glb <clip,clip,...>`
(with `pip install bpy`). The Rocketbox repository has more avatars (Military_Male_01..06,
Military_Female_*, Police, Fire, Medical, Adults) that convert the same way.
