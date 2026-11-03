#!/bin/sh
# Twig atlases for treegen2.py (textures/twigs_{birch,broad,pinnate,lance}.png + _nor.png), needs Blender's bpy module
# (pip install bpy; run it with python3 -I), numpy, scipy and Pillow. Sources: Poly Haven tree_small_02 and island_tree_02
# (CC0), 2k glTF downloads with their textures in <dir>/textures/:
#   sh twigs.sh /tmp/twigwork   (expects /tmp/twigwork/dl/tree_small_02/tree_small_02_2k.gltf and dl/island_tree_02/...)
set -e
H=$(cd "$(dirname "$0")" && pwd); W=${1:-/tmp/twigwork}; cd "$W"
TS=dl/tree_small_02; IT=dl/island_tree_02
# leaf outlines from the UV layout of each leaf material, cut out of the scanned leaf texture
python3 -I $H/leafmask.py $TS/tree_small_02_2k.gltf tree_small_02_leaves mask_ts.png
python3 -I $H/leafmask.py $IT/island_tree_02_2k.gltf island_tree_02_leaves mask_it.png
python3 -I $H/leaflib.py $TS/textures/tree_small_02_leaves_diff_2k.jpg - mask_ts.png lib_ts
python3 -I $H/leaflib.py $IT/textures/island_tree_02_leaves_diff_2k.jpg - mask_it.png lib_it
python3 -I -c "from PIL import Image; Image.open('$TS/textures/tree_small_02_leaves_nor_gl_2k.jpg').save('lib_ts_nor.png'); Image.open('$IT/textures/island_tree_02_leaves_nor_gl_2k.jpg').save('lib_it_nor.png')"
# twigs: stems with leaves set on them like on a real shoot, rendered from above (Cycles): albedo, normal, AO
python3 -I $H/twigbatch.py
python3 -I $H/twigatlas.py $H/../textures
