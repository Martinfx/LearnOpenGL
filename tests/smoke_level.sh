#!/bin/sh
# Level smoke test: runs the game on one level for a number of frames and fails
# when it crashes, trips an assert or logs an error (missing assets, shader or GL
# errors, script errors). Uses xvfb-run when there is no display.
#   smoke_level.sh <fps_game> <level> [frames]
game="$1"
level="$2"
frames="${3:-120}"
user_dir="$(mktemp -d)"
trap 'rm -rf "$user_dir"' EXIT
cd "$(dirname "$game")" || exit 2
export FPS_ENGINE_FRAMES="$frames"
export FPS_ENGINE_USER_DIR="$user_dir"
if [ -z "$DISPLAY" ] && command -v xvfb-run >/dev/null 2>&1; then
    exec xvfb-run -a -s "-screen 0 640x360x24" "$game" "$level"
fi
exec "$game" "$level"
