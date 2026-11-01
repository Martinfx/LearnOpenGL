#!/bin/sh
# Golden image test of the renderer: renders one view of a level (tests/golden/shots.txt) with
# a fixed frame time and compares it with tests/golden/<name>.ppm within a tolerance.
#   golden_image.sh <fps_game> <image_compare> <name> [out_dir]
# GOLDEN_UPDATE=1 writes the new image as the reference instead (check it, then commit it).
# The references come from Mesa llvmpipe (what CI runs under xvfb); a GPU driver differs a
# little more, hence the tolerance in shots.txt.
game="$1"
compare="$2"
name="$3"
out_dir="${4:-$(mktemp -d)}"
here="$(cd "$(dirname "$0")" && pwd)"
line="$(grep "^$name|" "$here/golden/shots.txt")"
[ -n "$line" ] || { echo "golden: no shot '$name' in shots.txt"; exit 2; }
level="$(echo "$line" | cut -d'|' -f2)"
pos="$(echo "$line" | cut -d'|' -f3)"
look="$(echo "$line" | cut -d'|' -f4)"
console="$(echo "$line" | cut -d'|' -f5)"
frames="$(echo "$line" | cut -d'|' -f6)"
tolerance="$(echo "$line" | cut -d'|' -f7)"
user_dir="$(mktemp -d)"
trap 'rm -rf "$user_dir"' EXIT
mkdir -p "$out_dir"
image="$out_dir/$name.ppm"
rm -f "$image"
cd "$(dirname "$game")" || exit 2
export FPS_ENGINE_FRAMES="$frames" FPS_ENGINE_USER_DIR="$user_dir" FPS_ENGINE_FIXED_DT=0.016667
export FPS_ENGINE_POS="$pos" FPS_ENGINE_LOOK="$look" FPS_ENGINE_CONSOLE="$console" FPS_ENGINE_SCREENSHOT="$image"
args="$level +r_width 512 +r_height 288 +r_quality 2"
if [ -z "$DISPLAY" ] && command -v xvfb-run >/dev/null 2>&1; then
    xvfb-run -a -s "-screen 0 800x450x24" "$game" $args > "$out_dir/$name.log" 2>&1
else
    "$game" $args > "$out_dir/$name.log" 2>&1
fi
[ -s "$image" ] || { echo "golden: the game wrote no image (log: $out_dir/$name.log)"; tail -5 "$out_dir/$name.log"; exit 1; }
if [ -n "$GOLDEN_UPDATE" ]; then
    cp "$image" "$here/golden/$name.ppm"
    echo "golden: updated tests/golden/$name.ppm"
    exit 0
fi
# shellcheck disable=SC2086
"$compare" "$here/golden/$name.ppm" "$image" $tolerance --diff "$out_dir/$name.diff.ppm" || {
    echo "golden: $name differs; image $image, red = changed pixels in $out_dir/$name.diff.ppm"
    exit 1
}
