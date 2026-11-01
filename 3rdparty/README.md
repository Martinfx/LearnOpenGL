# Third-party libraries

Everything in this folder is third-party code, copied **unmodified** from the
upstream release listed below (only the files the build needs: no tests,
samples or docs). Do not edit these files. Project-specific settings live in
[`cmake/Dependencies.cmake`](../cmake/Dependencies.cmake) (build options and
small wrapper targets). To update a library, replace its folder with the new
release and update this table.

When `ENGINE_USE_SYSTEM_DEPS` is ON (default), GLM and GLFW are taken (Jolt only
with `ENGINE_USE_SYSTEM_JOLT=ON`, because a packaged Jolt exports CPU flags such as
AVX2 to the whole engine)
from the system if installed (FreeBSD pkg, Linux distro, vcpkg) and these
copies are used only as a fallback.

| Folder | Library | Version | License | Upstream |
| --- | --- | --- | --- | --- |
| `glad/` | glad loader, OpenGL 4.6 core | glad 2.0.8 (generated) | WTFPL / CC0, Khronos Apache-2.0 | https://github.com/Dav1dde/glad |
| `cgltf/` | cgltf (glTF 2.0 parser) | 1.15 | MIT (`cgltf/LICENSE`) | https://github.com/jkuhlmann/cgltf |
| `stb/` | stb_image 2.30, stb_truetype 1.26, stb_dxt 1.12, stb_vorbis 1.22 | MIT or public domain (end of each header) | https://github.com/nothings/stb |
| `glm/` | GLM (math) | 1.0.1 | MIT / Happy Bunny (`glm/copying.txt`) | https://github.com/g-truc/glm |
| `glfw/` | GLFW (window, input) | 3.4 | zlib (`glfw/LICENSE.md`) | https://github.com/glfw/glfw |
| `jolt/` | Jolt Physics | 5.6.0 | MIT (`jolt/LICENSE`) | https://github.com/jrouwe/JoltPhysics |
| `lua/` | Lua | 5.4.7 | MIT (in `lua/lua.h`) | https://github.com/lua/lua |
| `miniaudio/` | miniaudio (audio playback, mixing, 3D sound, decoding) | 0.11.26 | Public domain (Unlicense) or MIT-0 (`miniaudio/LICENSE`) | https://github.com/mackron/miniaudio |
| `openal-soft/` | OpenAL Soft (audio output, 3D, HRTF, EFX reverb) | 1.24.3 | LGPL-2.1-or-later (`openal-soft/COPYING`), parts BSD-3-Clause (`BSD-3Clause`, `LICENSE-pffft`) | https://github.com/kcat/openal-soft |
| `recastnavigation/` | Recast (navmesh building) + Detour (path queries) | 1.6.0+ (main, commit 9f4ce64) | zlib (`recastnavigation/License.txt`) | https://github.com/recastnavigation/recastnavigation |
| `sol2/` | sol2 (C++ ↔ Lua binding) | 3.5.0 | MIT (`sol2/LICENSE.txt`) | https://github.com/ThePhD/sol2 |
| `tracy/` | Tracy profiler, client only (`public/` of the release; built with `-DENGINE_TRACY=ON`) | 0.14.1 | BSD-3-Clause (`tracy/LICENSE`) | https://github.com/wolfpld/tracy |

Not in this folder: Qt 6 (editor), which is installed from the system or the Qt installer.

## Licence texts in the game

`cmake/Licenses.cmake` copies the full licence text of every library to
`licenses/<library>.txt` next to the game (build folder `bin/licenses/`, release
package `licenses/`), together with this table as `THIRD_PARTY.md`, which the game's
Credits screen shows. A test checks that every folder here has a row in the table and
a licence entry in `cmake/Licenses.cmake`.

### OpenAL Soft (LGPL)

OpenAL Soft is the only library under a copyleft licence (GNU LGPL 2.1 or later).
The engine meets its terms this way:

- It is always linked as a **shared library** (`libopenal.so`, `OpenAL32.dll`), never
  statically, and the game finds it next to itself (`$ORIGIN` rpath on Linux and FreeBSD).
  A player can replace it with their own build of the same API without relinking the game.
- The folder holds the **unmodified** upstream release 1.24.3; its source is this folder
  and the upstream tag `1.24.3` at https://github.com/kcat/openal-soft.
- The licence text ships as `licenses/openal-soft.txt` and is named on the Credits screen.

When a system OpenAL is used instead (`ENGINE_USE_SYSTEM_DEPS`), the same holds.

### Qt (editor only)

The editor links Qt 6 dynamically under the LGPL 3. The release package does not bundle
Qt; whoever adds the Qt libraries to a package (`windeployqt`, `linuxdeployqt`) must add
the Qt licence texts to `licenses/` as well.
