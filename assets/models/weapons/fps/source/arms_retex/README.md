# Arms retexture (one look for every viewmodel)

Makes the arms of `fp_pistol`, `fp_heavy_pistol` and `fp_knife` match `fp_ak74` (brown worn sleeve, brown leather
gloves, no bare skin). The meshes, rigs, animations and material names are untouched; only the arms material's
baseColor and metallicRoughness images (and, for the heavy pistol, the normal map) are replaced inside the .glb.

1. `mask.py` (Blender as a Python module): per UV island, glove vs sleeve from the hand/finger bone weights,
   rasterised into a 2048 px UV mask per model.
2. `retex.py`: per class, Lab colour statistics transfer from the AK arms (glove -> glove, sleeve -> sleeve,
   bare skin -> glove), roughness matched the same way, plus the AK sleeve's wear (high-pass) laid over the sleeve.
   The heavy pistol shares the knife's UV layout, so it is built from the knife's gloved textures.
3. `glbput.py`: writes the new images back into the .glb (re-packs the BIN chunk; JSON otherwise unchanged).

The scripts were run from /tmp/arms; adjust the paths at the top if you re-run them.
