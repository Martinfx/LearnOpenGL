#include "engine/Paths.h"

#include <cstdlib>
#include <system_error>

namespace engine {

namespace {
std::filesystem::path fromEnv(const char* name)
{
#ifdef _WIN32
    if (const wchar_t* v = _wgetenv(std::filesystem::path(name).wstring().c_str()); v && *v)
        return std::filesystem::path(v);
#else
    if (const char* v = std::getenv(name); v && *v)
        return std::filesystem::path(v);
#endif
    return {};
}
} // namespace

std::filesystem::path userDirectory()
{
    static const std::filesystem::path dir = [] {
        std::filesystem::path d = fromEnv("FPS_ENGINE_USER_DIR");
        if (d.empty()) {
#ifdef _WIN32
            d = fromEnv("APPDATA");
            d = d.empty() ? std::filesystem::current_path() / "user" : d / "FpsEngine";
#elif defined(__APPLE__)
            d = fromEnv("HOME") / "Library/Application Support/FpsEngine";
#else
            const std::filesystem::path xdg = fromEnv("XDG_DATA_HOME");
            const std::filesystem::path home = fromEnv("HOME");
            d = !xdg.empty() ? xdg / "fps-engine"
                : !home.empty() ? home / ".local/share/fps-engine"
                                : std::filesystem::temp_directory_path() / "fps-engine";
#endif
        }
        std::error_code ec;
        std::filesystem::create_directories(d, ec);
        return d;
    }();
    return dir;
}

std::filesystem::path userDirectory(const char* sub)
{
    const std::filesystem::path d = userDirectory() / sub;
    std::error_code ec;
    std::filesystem::create_directories(d, ec);
    return d;
}

} // namespace engine
