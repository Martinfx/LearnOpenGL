# Sounds

Real recordings, downloaded and cut by [`source/build_sounds.py`](source/build_sounds.py), which records every source URL.
One-shot effects are **mono** 16-bit 48 kHz WAV, because OpenAL only spatialises mono buffers. Ambience loops are
stereo Ogg Vorbis (`stream 1`, `spatial 0`) with a crossfaded seamless loop point.

Levels: gunshots are soft-clipped to a dense body (about -18 LUFS over the first 0.4 s, true peak <= 0 dBTP); ambience
beds sit 10 to 20 dB below that (wind about -30 LUFS, drones about -38 LUFS with rumble under 70 Hz removed).

`.sound` files group the variations of one effect with its playback settings (volume, random pitch, 3D distance range,
loop, stream, mixer group); edit them in the editor's sound editor.

Still synthesized placeholders (CC0, from `../tools/make_sounds.py`): `weapons/swing_*.wav` (knife) and
`ambient/geiger_loop.ogg`. Do not rerun `make_sounds.py` without deleting its other outputs from it, or it will overwrite the
recordings with placeholders.

## Sources

| Our files | Source | Author | License |
|---|---|---|---|
| `weapons/rifle_shot_*` (AK-47), `smg_shot_*` (Carl Gustav M45 9 mm), `pistol_shot_*` (Bersa .380, Walther PPQ 9 mm), `heavy_pistol_shot_*` (M1911 .45), `sniper_shot_*` (Mosin-Nagant): raw 192 kHz *behind the shooter* master tracks; `reload*`, `mag_in/out`, `bolt*`, `dry_fire*`, `draw`: real handling recordings of the same guns (mag out/in, slide, bolt) assembled into sequences; rifle and sniper shots are layered with the prepared mid-distance takes (AK C_31P, Mosin M_26P) for the room/field tail, each weapon has its own EQ, length and loudness; `rifle_shot_far_*`: prepared mid-distance track | [The Free Firearm Sound Effects Library](https://archive.org/details/the-free-firearm-sound-effects-library-by-still-north-media) ([OpenGameArt](https://opengameart.org/content/the-free-firearm-sound-library)) | Ben Jaszczak, Brian Nelson, Kevin Heras, Matthew Nanney (Still North Media) | CC0 |
| `weapons/casing_concrete_*`, `casing_metal_*`, `casing_dirt_*` (built by `source/build_casings.py`: hit grains pitched up to casing size and laid out as a bouncing case) | [Coin Drop](https://opengameart.org/content/coin-drop) | Vinrax | CC0 |
| `weapons/casing_*` (extra clinks; the metal-floor answer) | [Metal Impact Sounds](https://opengameart.org/content/metal-impact-sounds) | BMacZero | CC0 |
| `weapons/casing_dirt_*` (soil contact) | [Fantozzi's Footsteps (Grass/Sand & Stone)](https://opengameart.org/content/fantozzis-footsteps-grasssand-stone) | Fantozzi (posted by qubodup) | CC0 |
| `weapons/ricochet_*`, `ricochet_metal_*` (built by `source/build_ricochets.py`: real metal pings, whine = the ring of struck metal bent down in pitch like a receding bullet) | [27 Metal Audio Samples](https://opengameart.org/content/27-metal-audio-samples-sfx) | blacklodgegames | CC0 |
| `weapons/ricochet_*` (pings) | [Metal Impact Sounds](https://opengameart.org/content/metal-impact-sounds) | BMacZero | CC0 |
| `impacts/impact_concrete_*`, `impact_metal_4..5` | [75 CC0 breaking / falling / hit sfx](https://opengameart.org/content/75-cc0-breaking-falling-hit-sfx) | rubberduck | CC0 |
| `impacts/impact_concrete_2`, `impact_flesh_*` | [Impact](https://opengameart.org/content/impact) | qubodup | CC0 |
| `impacts/impact_metal_1..3` | [Metal Impact Sounds](https://opengameart.org/content/metal-impact-sounds) | BMacZero | CC0 |
| `impacts/impact_metal_6` | [100 CC0 metal and wood SFX](https://opengameart.org/content/100-cc0-metal-and-wood-sfx) | rubberduck | CC0 |
| `impacts/impact_wood_*` | [35 wooden cracks/hits/destructions](https://opengameart.org/content/35-wooden-crackshitsdestructions) | Independent.nu (posted by qubodup) | CC0 |
| `player/step_concrete_*`, `step_dirt_*`, `impacts/impact_dirt_*` | [Fantozzi's Footsteps (Grass/Sand & Stone)](https://opengameart.org/content/fantozzis-footsteps-grasssand-stone) | Fantozzi (posted by qubodup) | CC0 |
| `player/step_metal_*` | [Metal footsteps on concrete](https://opengameart.org/content/metal-footsteps-on-concrete) | Thimras | CC0 |
| `player/step_wood_1..3` | [Different steps on wood, stone, leaves, gravel and mud](https://opengameart.org/content/different-steps-on-wood-stone-leaves-gravel-and-mud) | TinyWorlds | CC0 |
| `player/step_gravel_*` | [Footsteps on different surfaces](https://opengameart.org/content/footsteps-on-different-surfaces) (from [Gravel Footsteps](https://freesound.org/people/Ali_6868/packs/21608/) by Ali_6868) | congusbongus | CC0 |
| `player/step_grass_*`, `step_wood_4..6` | [Footsteps on different surfaces](https://opengameart.org/content/footsteps-on-different-surfaces) (from [footstep-grass](https://freesound.org/people/swuing/sounds/38874/) and [footstep-wood](https://freesound.org/people/swuing/sounds/38876/) by swuing) | congusbongus, swuing | CC-BY 3.0 |
| `player/step_water_*` | same pack (from [Water Footsteps](https://freesound.org/people/EminYILDIRIM/sounds/608663/) by EminYILDIRIM) | congusbongus, EminYILDIRIM | CC-BY 3.0 |
| `player/land.wav` | [Metal Impact Sounds](https://opengameart.org/content/metal-impact-sounds) (`thud3`) | BMacZero | CC0 |
| `player/hurt_*` | [15 vocal male strain/hurt/pain/jump sounds](https://opengameart.org/content/15-vocal-male-strainhurtpainjump-sounds) | qubodup | CC0 |
| `player/death_*` | [11 male human pain/death sounds](https://opengameart.org/content/11-male-human-paindeath-sounds) | Michel Baradari (posted by qubodup) | CC-BY 3.0 |
| `ambient/wind_loop.ogg` | [windy ambient noise](https://opengameart.org/content/windy-ambient-noise) | some-weirdo | CC0 |
| `ambient/wind_storm_loop.ogg` | [Storm Arwen 2022](https://opengameart.org/content/storm-arwen-2022) | tsorthan-grove | CC-BY 4.0 |
| `ambient/zone_drone_loop.ogg`, `zone_drone_2_loop.ogg`, `zone_drone_3_loop.ogg` | [Droning Sound Effects](https://opengameart.org/content/droning-sound-effects) (drone60, drone70, drone53) | Bret Bernhoft | CC0 |
| `ambient/birds_loop.ogg` | [Park ambiences](https://opengameart.org/content/park-ambiences) | Thimras | CC0 |
| `ambient/rain_loop.ogg` | [AMB Rain Loop 1](https://opengameart.org/content/amb-rain-loop-1) | Kresiek the Furry | CC0 |

CC-BY items: "This work is based on <title> by <author>, licensed under CC-BY (https://creativecommons.org/licenses/by/3.0/
or /4.0/)". Changes: trimmed, converted to mono 48 kHz, normalised, and filtered; loops were crossfaded.
