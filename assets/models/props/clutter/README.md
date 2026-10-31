# Clutter props

Post-apocalyptic junk for dressing levels. Two sources:

* `clutter/*.glb`: modelled for this project (`source/clutter.py`, CC0): `wooden_pallet`, `wooden_pallet_broken`,
  `cinder_block`, `cinder_block_broken`, `brick`, `brick_pile`, `rubble_pile_small`, `rubble_pile_large`, `rag_a`, `rag_b`,
  `newspaper_sheet_a`, `newspaper_sheet_b`, `newspaper_stack`, `cardboard_box_small`, `cardboard_box_large`,
  `cardboard_box_crushed`, `cardboard_box_open`, `cardboard_flattened`, `planks_scrap_pile`.
* Poly Haven CC0 props one folder up (`../<id>/<id>.gltf`), e.g. crates, plastic crates, cans, bottles, jerrycans,
  barrels, buckets, tyres, bags, toolboxes, food, paper, boots, stools, suitcase.

Scale: real-world sizes, 1 unit = 1 m, origin at the bottom centre, +Y up (checked with `assets/tools/scale_audit.py`).

`clutter.json` lists, per prop, its path, size (x, y, z in m), and a physics suggestion: `dynamic` props have a mass (kg)
and a simple collider shape (`box` or `cylinder` fitted to the bounds); `static` ones (piles, rags, paper sheets) use
their low-poly mesh as collision or none. The generated meshes are low poly (12-2400 triangles).
