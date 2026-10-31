#pragma once
#include <filesystem>

namespace engine {

// Per-user folders for settings, saved games, logs and caches:
//   Windows        %APPDATA%\FpsEngine
//   macOS          ~/Library/Application Support/FpsEngine
//   Linux/FreeBSD  $XDG_DATA_HOME/fps-engine (default ~/.local/share/fps-engine)
// FPS_ENGINE_USER_DIR overrides it (tests, portable installs). Created on first use.
std::filesystem::path userDirectory();
// A sub-folder of userDirectory() ("logs", "saves", "config"), created on first use.
std::filesystem::path userDirectory(const char* sub);

} // namespace engine
