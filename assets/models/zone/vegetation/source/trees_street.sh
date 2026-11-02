#!/bin/sh
# Street trees and bushes of the overgrown street with the street leaf atlas (leafcards.py) and crowns of
# out-facing leaf rosettes: python3 -I leafcards.py first, then  sh trees_street.sh  (needs Blender's bpy module).
# Bark: Poly Haven bark_brown_02 (CC0), 1k diffuse + OpenGL normal in /tmp/ph/tex_bark_brown_02/{diff,nor}.jpg.
H=$(cd "$(dirname "$0")" && pwd); C=$H/cards; OUT=$H/..; P=/tmp/ph
BRK="\"bark\":\"$P/tex_bark_brown_02/diff.jpg\",\"bark_nor\":\"$P/tex_bark_brown_02/nor.jpg\""
ST="$BRK,\"cards\":\"$C/leaves_street.png\",\"cards_dark\":\"$C/leaves_street_dark.png\""
BIR="\"bark\":\"$C/bark_birch.png\",\"bark_nor\":\"$C/bark_birch_nor.png\",\"cards\":\"$C/leaves_birch.png\""
gen(){ for L in 0 1; do sfx=""; [ $L = 1 ] && sfx="_lod1"; python3 -I $H/treegen.py "{\"species\":\"$1\",\"name\":\"$2\",\"height\":$3,\"seed\":$4,\"lod\":$L,\"crown\":\"out\",$6\"assets\":{$5},\"out\":\"$OUT/$2$sfx.glb\"}" 2>&1 | grep -E 'TREE|Error|Trace'; done; }
gen maple maple_a 11 5 "$ST" & gen maple maple_b 9 13 "$ST" & gen ash ash_a 12 17 "$ST" & gen ash ash_b 8.5 29 "$ST" & wait
gen broadleaf broadleaf_a 11 4 "$ST" & gen broadleaf broadleaf_b 8 7 "$ST" & gen broadleaf broadleaf_young 6.7 9 "$ST" & wait
gen sapling sapling_a 4.5 3 "$ST" & gen sapling sapling_b 5.6 6 "$ST" & gen sapling sapling_c 2.8 11 "$ST" & wait
gen birch birch_a 15 3 "$BIR" & gen birch birch_young 5 5 "$BIR" &
gen bush bush_street_a 2.2 21 "$ST" "\"width\":3.0," & gen bush bush_street_b 1.6 22 "$ST" "\"width\":2.2," & gen bush bush_street_c 2.8 23 "$ST" "\"width\":3.6," & wait
# alpha-tested (MASK) leaves: Blender exports BLEND, which loses the crown shadows and sorts badly
python3 -I $H/glbmask.py --match=leaves $(for n in maple_a maple_b ash_a ash_b broadleaf_a broadleaf_b broadleaf_young sapling_a sapling_b sapling_c birch_a birch_young bush_street_a bush_street_b bush_street_c; do echo $OUT/$n.glb $OUT/${n}_lod1.glb; done)
