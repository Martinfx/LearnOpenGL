#!/bin/sh
# Release package for Linux and FreeBSD: LTO build with debug info, assets packed into
# game.pak, debug info split into a separate symbols archive (the game binaries keep a
# .gnu_debuglink to it, so gdb/lldb find the symbols when the archives are unpacked together).
#   tools/package.sh            -> build-release/package/FpsEngine-<git describe>-<OS>.tar.gz
#   BUILD_DIR=dir PAK=OFF tools/package.sh   (loose assets and the editor instead of game.pak)
set -eu
src=$(cd "$(dirname "$0")/.." && pwd)
build=${BUILD_DIR:-$src/build-release}
pak=${PAK:-ON}
cmake -S "$src" -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DENGINE_LTO=ON -DENGINE_PACKAGE_PAK="$pak" -DBUILD_TESTING=OFF
cmake --build "$build"
version=$(git -C "$src" describe --always --dirty --tags 2>/dev/null || echo dev)
name="FpsEngine-$version-$(uname -s)"
out="$build/package"
rm -rf "$out"
cmake --install "$build" --component game --prefix "$out/$name"
mkdir -p "$out/$name-symbols"
for exe in fps_game fps_editor fps_pack; do
    [ -f "$out/$name/$exe" ] || continue
    objcopy --only-keep-debug "$out/$name/$exe" "$out/$name-symbols/$exe.debug"
    objcopy --strip-debug --add-gnu-debuglink="$out/$name-symbols/$exe.debug" "$out/$name/$exe"
done
tar -C "$out" -czf "$out/$name.tar.gz" "$name"
tar -C "$out" -czf "$out/$name-symbols.tar.gz" "$name-symbols"
echo "package: $out/$name.tar.gz"
echo "symbols: $out/$name-symbols.tar.gz"
