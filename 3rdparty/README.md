# Third-party libraries

Everything in this folder is third-party code, copied **unmodified** from the
upstream release listed below (only the files the build needs: no tests,
samples or docs). Do not edit these files. Project-specific settings live in
[`cmake/Dependencies.cmake`](../cmake/Dependencies.cmake) (build options and
small wrapper targets). To update a library, replace its folder with the new
release and update this table.

When `ENGINE_USE_SYSTEM_DEPS` is ON (default), GLM, GLFW and Jolt are taken
from the system if installed (FreeBSD pkg, Linux distro, vcpkg) and these
copies are used only as a fallback.

| Folder | Library | Version | License | Upstream |
| --- | --- | --- | --- | --- |
| `glad/` | glad loader, OpenGL 4.6 core | glad 2.0.8 (generated) | WTFPL / CC0, Khronos Apache-2.0 | https://github.com/Dav1dde/glad |
| `cgltf/` | cgltf (glTF 2.0 parser) | 1.15 | MIT (`cgltf/LICENSE`) | https://github.com/jkuhlmann/cgltf |
| `stb/` | stb_image 2.30, stb_truetype 1.26 | MIT or public domain (end of each header) | https://github.com/nothings/stb |
| `glm/` | GLM (math) | 1.0.1 | MIT / Happy Bunny (`glm/copying.txt`) | https://github.com/g-truc/glm |
| `glfw/` | GLFW (window, input) | 3.4 | zlib (`glfw/LICENSE.md`) | https://github.com/glfw/glfw |
| `jolt/` | Jolt Physics | 5.6.0 | MIT (`jolt/LICENSE`) | https://github.com/jrouwe/JoltPhysics |
| `lua/` | Lua | 5.4.7 | MIT (in `lua/lua.h`) | https://github.com/lua/lua |
| `sol2/` | sol2 (C++ ↔ Lua binding) | 3.5.0 | MIT (`sol2/LICENSE.txt`) | https://github.com/ThePhD/sol2 |

Not in this folder: Qt 6 (editor), which is installed from the system or the Qt installer.
