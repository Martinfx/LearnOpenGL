#!/bin/sh
# All street trees and shrubs of the overgrown street and the Dead City (treegen2.py, no Blender): LOD 0, 1 (~45 % of the
# cards, bigger, thinner twigs gone) and 2 (~20 %, limbs only). Twig atlases: twigbake.py + twigatlas.py (see README).
H=$(cd "$(dirname "$0")" && pwd)
g(){ python3 -I "$H/treegen2.py" "$@" & }
g maple maple_a 11 5; g maple maple_b 9.3 13; g ash ash_a 13 17; g ash ash_b 9.4 29; wait
g poplar broadleaf_a 10.5 4; g willow broadleaf_b 7.8 7; g poplar broadleaf_young 6.7 9; g sapling sapling_a 4.5 3; wait
g sapling sapling_b 5.6 6; g sapling_broad sapling_c 2.8 11; g birch birch_a 15.5 3; g birch birch_b 11.7 8; wait
g birch birch_young 5.6 5; g bush bush_street_a 3.4 21; g bush bush_street_b 2.6 22; g bush bush_street_c 4.0 23; wait
