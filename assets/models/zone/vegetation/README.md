# Zone vegetation

Metres, +Y up, base at y = 0, centred on X/Z (see `assets/SCALE.md`). Every model has a `_lod1.glb`
(about 40 to 60 % of the triangles, textures halved). The dense ground cover (grass_clump_a, weed_a/b, nettle_tall_a,
dandelion_a/c, celandine_b) also has a `_far.glb` of about 10 %: `source/vegthin.py` keeps the biggest whole blades
and leaves and widens them about their base, so a clump keeps its coverage instead of collapsing into slivers. Foliage materials are **alpha-tested** (`alphaMode MASK`,
cutoff 0.5, double-sided); render them without back-face culling. Leaf materials have roughness 1 and low specular so cards do not catch sky reflections at grazing angles. Tree foliage cards carry normals that point out
of the crown, so they light like a volume rather than flat planes.

## Trees (procedural, `source/treegen.py`, foliage cards baked from Poly Haven scans)

| File | What | Height (m) | Tris (LOD0) |
|---|---|---|---|
| `birch_a`, `birch_b`, `birch_young` | silver birch, white bark, drooping limbs | 15.9 / 11.7 / 5.6 | 1360 / 1144 / 928 |
| `pine_a`, `pine_b`, `pine_young` | Scots pine, bare lower trunk, flat crown | 19.2 / 14.0 / 4.6 | 1084 / 934 / 718 |
| `spruce_a`, `spruce_b`, `spruce_young` | Norway spruce, conical, branches to the ground | 14.7 / 9.7 / 3.3 | 1234 / 844 / 340 |
| `broadleaf_a`, `broadleaf_b` | alder/oak-like deciduous tree | 10.1 / 7.8 | 944 / 734 |
| `dead_a`, `dead_b` | dead leafless tree | 10.0 / 6.9 | 952 / 864 |
| `snag_a` | broken dead trunk | 3.7 | 320 |
| `sapling_a`, `sapling_b`, `sapling_c` | self-seeded young broadleaf (maple / ash / locust) from cracks, 2-4 slender stems | 4.5 / 5.6 / 2.8 | 1026 / 1224 / 1120 |
| `broadleaf_young` | young deciduous tree | 6.7 | 944 |
| `maple_a`, `maple_b` | middle-aged maple, round full crown (outer leaves light, inner crown darker) | 11.0 / 9.3 | 1964 / 1756 |
| `ash_a`, `ash_b` | middle-aged ash, taller oval crown | 13.1 / 9.4 | 1680 / 1362 |
| `bush_street_a`, `bush_street_b`, `bush_street_c` | dense self-seeded shrub (elder / hazel / lilac gone wild), a mound of leaves on many stems | 3.5 / 2.6 / 4.2 | 982 / 676 / 1264 |
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
