# Particle textures

All PNGs are RGBA with **straight (not premultiplied) alpha**, sRGB colour. Flipbooks carry `cols x rows` in
the name; frame 0 is the top-left tile, frames run left to right, then top to bottom (row-major).

| File | Size | Frames | What it is |
|---|---|---|---|
| `smoke_8x8.png` | 2048² (256 px tiles) | 64, one-shot | Billowing grey-white puff: dense at birth, grows, rises a little, thins out to nothing |
| `fire_8x8.png` | 2048² | 64, **seamless loop** (63 → 0) | Flame tongue, blackbody colours, rounded base at the bottom of the tile |
| `explosion_8x8.png` | 2048² | 64, one-shot | Hot fireball that burns out over the first half and leaves dark rising smoke |
| `muzzle_flash_2x2.png` | 1024² (512 px tiles) | 4 variants | Front view of a rifle muzzle blast: white core, 3–5 turbulent flame lobes |
| `spark.png` | 64×512 | 1 | Thin bright streak, head at the top; stretch along velocity |
| `dust.png` | 512² | 1 | Soft brown-grey dust puff, lit from above |
| `debris_4x4.png` | 1024² (256 px tiles) | 16 variants | Small chips: 12 concrete, 4 wood splinters (tiles 3, 7, 11, 15) |
| `raindrop.png` | 32×512 | 1 | Thin vertical streak, ~55% opacity at the core |
| `arc.png` | 64×256 | 1 | Lightning strip: white core with a soft glow across, full length (anomaly arcs) |
| `acid_sheen.png` | 512² | 1 | Bright wandering veins and oily blotches, round fade: glossy highlights on acid |
| `anomaly_marks_2x2.png` | 1024² (512 px tiles) | 4 | Ground marks: scorch with cracks, acid sludge, dry dead ground, pressed swirled soil |

The emissive sheets (fire, explosion, muzzle flash, spark) use alpha as light coverage, so they work with
either alpha blending or additive blending (`src * alpha`). Smoke, dust and debris are lit (sun from the
upper left, sky fill) and want normal alpha blending.

Made for this project by `source/particles.py`: smoke, fire, explosion and dust are real volumetric renders in
Cycles (noise-shaped density, blackbody emission), muzzle flash, spark and raindrop are drawn procedurally, and
debris chips are rendered with the Poly Haven CC0 scans `concrete_wall_007` and `rough_wood` from
`../pbr/`. **CC0 1.0**. The anomaly textures (arc, acid sheen, marks) are procedural noise from `source/anomaly_textures.py` (numpy + Pillow, seconds). Rebuild: `python3 -I assets/textures/particles/source/particles.py [smoke fire ...]`
(needs `pip install bpy numpy`, about 30 min on 4 CPU cores for everything).
