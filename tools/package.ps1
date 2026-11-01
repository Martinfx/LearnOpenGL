# Release package for Windows: LTO build with debug info (.pdb), assets packed into game.pak,
# CPack makes FpsEngine-<version>-Windows-game.zip and -symbols.zip (the .pdb files).
#   powershell -File tools\package.ps1
$ErrorActionPreference = "Stop"
$src = Split-Path -Parent $PSScriptRoot
$build = if ($env:BUILD_DIR) { $env:BUILD_DIR } else { Join-Path $src "build-release" }
cmake -S $src -B $build -A x64 -DENGINE_LTO=ON -DENGINE_PACKAGE_PAK=ON -DBUILD_TESTING=OFF
if ($LASTEXITCODE) { exit $LASTEXITCODE }
cmake --build $build --config RelWithDebInfo
if ($LASTEXITCODE) { exit $LASTEXITCODE }
Push-Location $build
cpack -C RelWithDebInfo -B (Join-Path $build "package")
$code = $LASTEXITCODE
Pop-Location
exit $code
