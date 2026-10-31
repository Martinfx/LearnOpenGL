P=/tmp/ph; A=/tmp/veg/atlas
bk(){ echo "\"bark\":\"$P/tex_$1/diff.jpg\",\"bark_nor\":\"$P/tex_$1/nor.jpg\""; }
BIRCH="\"bark\":\"$A/bark_birch.png\",\"bark_nor\":\"$A/bark_birch_nor.png\",\"cards\":\"$A/leaves_birch.png\""
PINE="$(bk pine_bark),\"cards\":\"$A/needles_conifer.png\""
SPR="$(bk bark_brown_02),\"cards\":\"$A/needles_conifer.png\""
BRD="$(bk bark_brown_02),\"cards\":\"$A/leaves_broadleaf.png\""
DEAD="$(bk bark_willow),\"cards\":\"$A/leaves_broadleaf.png\""
gen(){ for L in 0 1; do sfx=""; [ $L = 1 ] && sfx="_lod1"; python3 -I /tmp/tools/treegen.py "{\"species\":\"$1\",\"name\":\"$2\",\"height\":$3,\"seed\":$4,\"lod\":$L,\"assets\":{$5},\"out\":\"/tmp/veg/trees/$2$sfx.glb\"}" 2>&1 | grep -E 'TREE|Error|Trace'; done; }
gen birch birch_a 15 3 "$BIRCH" & gen birch birch_b 11 8 "$BIRCH" & gen birch birch_young 5 5 "$BIRCH" &
gen pine pine_a 18 2 "$PINE" & gen pine pine_b 13 9 "$PINE" & gen pine pine_young 4 4 "$PINE" & wait
gen spruce spruce_a 14 1 "$SPR" & gen spruce spruce_b 9 6 "$SPR" & gen spruce spruce_young 3 2 "$SPR" &
gen broadleaf broadleaf_a 11 4 "$BRD" & gen broadleaf broadleaf_b 8 7 "$BRD" & wait
gen dead dead_a 10 5 "$DEAD" & gen dead dead_b 7 11 "$DEAD" & gen snag snag_a 6 3 "$DEAD" & wait
