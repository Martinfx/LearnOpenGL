# Zone vegetation

Metres, +Y up, base at y = 0, centred on X/Z (see `assets/SCALE.md`). Every model has a `_lod1.glb`
(about 40 to 60 % of the triangles, textures halved). The dense ground cover (grass_clump_a, weed_a/b, nettle_tall_a,
dandelion_a/c, celandine_b, grass_short_a) also has a `_far.glb` of about 10 %: `source/vegthin.py` keeps the biggest whole blades
and leaves and widens them about their base, so a clump keeps its coverage instead of collapsing into slivers. `grass_fresh_street*` is the Skrunda `grass_fresh` thinned the same way for the overgrown street (1416 / 583 / 303 tris instead of 6405 / 2561 / 536: it covers the whole street, so it is drawn thousands of times). Foliage materials are **alpha-tested** (`alphaMode MASK`,
cutoff 0.5, double-sided); render them without back-face culling. Leaf materials have roughness 1 and low specular so cards do not catch sky reflections at grazing angles. Tree foliage cards carry normals that point out
of the crown, so they light like a volume rather than flat planes.
The most numerous ones (grass_clump_a, grass_fresh_street, dandelion_a, celandine_b) have a `_far2.glb` of 100 to 250 triangles
(vegthin.py with a lower ratio) for 16 to 22 m and beyond: ground cover was about 70 % of the street's triangles.

## Trees (procedural, `source/treegen2.py`, photographic twig cards)

The street and Dead City trees and shrubs are grown by `source/treegen2.py` (pure Python + numpy, no Blender):
a branch skeleton grown level by level (trunk, limbs, branches, shoots; golden-angle phyllotaxis, gravi- and
phototropism, a crown envelope per species) carries **twig cards** along its outer shoots, turned to the sky and out of the
crown the way real leaves sit. The cards are photographs of real twigs: `source/twigbake.py` builds twigs from the scanned
leaves of Poly Haven `tree_small_02` and `island_tree_02` (CC0; outlines cut from their UV layouts by `leafmask.py` /
`leaflib.py`), with petioles, alternate or opposite leaves, fold and droop, and renders them from above in Cycles:
albedo + alpha, an OpenGL normal map and an AO pass, packed 2x2 per species into `textures/twigs_{birch,broad,pinnate,lance}.png`
(+ `_nor.png`) by `twigatlas.py`. Rebuild: `sh source/twigs.sh <workdir>` (atlases, needs bpy), `sh source/trees2.sh` (models, seconds).

Every vertex carries a custom glTF attribute **`_FOLIAGE`** (RGBA8): R crown occlusion (baked from the card cloud: deep inside
the crown dark, the sunny shell open, the underside darker), G branch flexibility (0 trunk ... 1 outer twigs), B branch phase
(each limb and its shoots swing on their own), A leaf flutter (0 at a twig's base, 1 at its tip). `mesh.vert` swings limbs and
flutters leaves with it; `pbr.frag` darkens the inner crown and lets light through the leaves (deeper, more saturated colour
on the far side, a glow looking into the sun). Models without it render as before.

LODs keep the LOD 0 silhouette (no size pop): `_lod1` about 45 to 60 % of the cards (bigger, the thinnest shoots gone),
`_lod2` about 15 to 25 % (limbs only); the engine's impostor takes over at 120 m. Levels use 30 / 70 m for the big trees and
18 / 40 m for young trees and shrubs.

| File | Species (treegen2) | Height (m) | Tris / cards (LOD 0) |
|---|---|---|---|
| `maple_a`, `maple_b` | Norway maple: short trunk forking into scaffold limbs, round dense crown | 11.3 / 9.3 | 11412 / 828, 7590 / 498 |
| `ash_a`, `ash_b` | ash / tree of heaven: oval open crown, compound (pinnate) leaves | 13.1 / 9.3 | 8100 / 570, 4766 / 289 |
| `broadleaf_a`, `broadleaf_young` | poplar / alder: straight trunk far into an egg-shaped crown | 10.6 / 6.6 | 6528 / 286, 2882 / 92 |
| `broadleaf_b` | crack willow: leaning trunk, wide limbs, narrow leaves | 7.8 | 5436 / 381 |
| `birch_a`, `birch_b`, `birch_young` | silver birch: one trunk to the top, white bark, hanging twigs | 16.4 / 11.7 / 5.5 | 17732 / 1150, 8508 / 444, 2976 / 96 |
| `sapling_a`, `sapling_b` | self-seeded tree of heaven / locust from a crack, 2-4 stems, pinnate leaves | 4.5 / 5.5 | 2396 / 84, 2604 / 98 |
| `sapling_c` | the same with simple leaves (box elder) | 2.9 | 1272 / 34 |
| `bush_street_a`, `_b`, `_c` | wild shrub (elder, hazel, lilac): many stems, a mound of twigs | 4.1 / 3.0 / 4.3 | 8328 / 344, 4876 / 176, 10130 / 507 |

Older procedural trees (`source/treegen.py`, leaf cards baked by `cardbake.py`, the street atlas drawn by `leafcards.py`):

| File | What | Height (m) | Tris (LOD0) |
|---|---|---|---|
| `pine_a`, `pine_b`, `pine_young` | Scots pine, bare lower trunk, flat crown | 19.2 / 14.0 / 4.6 | 1084 / 934 / 718 |
| `spruce_a`, `spruce_b`, `spruce_young` | Norway spruce, conical, branches to the ground | 14.7 / 9.7 / 3.3 | 1234 / 844 / 340 |
| `dead_a`, `dead_b` | dead leafless tree | 10.0 / 6.9 | 952 / 864 |
| `snag_a` | broken dead trunk | 3.7 | 320 |
| `ivy_low`, `ivy_a`, `ivy_b`, `ivy_tall` | ivy / wild vine climbing a wall: vines with side shoots, leaves standing off the wall; the wall face is z = 0 of the glb, leaves toward +Z (place it on the facade, turned like the house) | 2.9 / 6.4 / 9.8 / 16.1 | 682 / 2134 / 5190 / 9256 |
| `log_fallen_a` | fallen dead trunk (Poly Haven scan, decimated) | 4.05 long | 4000 |

## Ground cover (Poly Haven scans, split per variant)

| File | What | Size (m, height) |
|---|---|---|
| `grass_short_a/b` | short lawn grass tuft | 0.15 to 0.2 |
| `grass_clump_a` | dense grass clump | 0.27 |
| `grass_tall_a/b` | tall overgrown grass | 0.9 / 0.76 |
| `grass_dry_a/b/c` | dry yellow grass | 0.4 to 0.8 |
| `nettle_tall_a/b`, `nettle_medium_a` | stinging nettle stands | 1.1 / 1.0 / 0.66 |
| `weed_a/b/c` | broad-leaf weed rosettes | 0.12 to 0.22 |
| `fern_a/b` | fern | 0.43 / 0.52 |
| `bush_a/b/c` | shrub | 1.1 to 1.2 |
| `bush_wide_a` | low spreading shrub, 2.3 m wide | 0.87 |
| `dandelion_a/b/c` | dandelion rosettes, a and b flowering | 0.06 to 0.27 |
| `celandine_a/b` | greater celandine | 0.55 |
| `sapling_shrub_a/b` | willow-like sapling sprouts | 1.7 / 1.5 |
| `grass_patch_tall` | 2.5 m patch of tall and dry grass, ready to scatter (6.4k tris) | 1.0 |
| `weed_patch` | 3 m patch of nettles, weeds and tall grass (8k tris) | 1.2 |
| `bush_dense_a` | dense shrub clump of 7 bushes (6k tris) | 1.3 |

The Poly Haven plant scans are of young plants (a nettle scan is 22 cm). Tall grass, nettles, weeds, ferns and
bushes were scaled 1.5x to 5x up to the size of adult plants in an overgrown zone; short grass is at scan size.

## Rebuilding

The trees and bushes of the overgrown street (`maple_*`, `ash_*`, `broadleaf_*`, `sapling_*`, `birch_a`,
`birch_young`, `bush_street_*`) use the street leaf atlas drawn leaf by leaf by `source/leafcards.py` (maple and lime
sprigs, `cards/leaves_street.png`) and crowns of leaf rosettes facing out of the crown (`crown: out` in
`source/treegen.py`); `source/trees_street.sh` rebuilds them and sets their leaves to alpha-test.

`source/trees.sh` regenerates all trees (seeded, deterministic). `source/cardbake.py` renders a Poly Haven scan to an
unlit RGBA card, `source/atlas.py` packs the cards into the 2x2 atlases in `source/cards/`, `source/birchbark.py`
draws the birch bark, `source/patch.py` scatters variants into the `*_patch` and `bush_dense_a` clumps, `source/vegconv.py` splits a Poly Haven plant set into variants, and `source/glbmask.py` sets
the alpha-test flags on the exported glb.
