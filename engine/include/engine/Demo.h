#pragma once
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace engine {

// Seed of the game's random streams (weather, ricochets, jams, Lua math.random, ...). A demo
// stores it, so the replay draws the same numbers. Set before a level starts.
std::uint32_t gameSeed();
void setGameSeed(std::uint32_t seed);
// A seed per named stream, derived from the game seed (streams do not shift each other).
std::uint32_t seedFor(const char* stream);

// One game frame of a demo: what the player did and how long the frame was.
struct DemoFrame {
    float dt = 0.0f;                 // simulated time of the frame (s)
    glm::vec2 mouseDelta{0.0f};      // look movement in pixels
    glm::vec2 cursor{0.0f};          // cursor position (menus, dialogs)
    float scroll = 0.0f;
    std::vector<std::uint16_t> keys; // GLFW key codes held, buttons as kDemoMouseBase + button
    glm::vec3 player{0.0f};          // player position after the frame (desync check)
};
inline constexpr std::uint16_t kDemoMouseBase = 1000;

// A recording of one level session: replayed with the same seed, settings and inputs, the
// game runs the same way (benchmark route, bug reproduction).
struct Demo {
    std::string level;                         // level file, relative to assets/ when inside
    std::uint32_t seed = 0;
    std::map<std::string, std::string> cvars;  // settings that change how input acts (sensitivity, ...)
    std::vector<DemoFrame> frames;

    float duration() const;
};

bool saveDemo(const Demo& demo, const std::filesystem::path& file, std::string* error = nullptr);
bool loadDemo(Demo& demo, const std::filesystem::path& file, std::string* error = nullptr);

// Frame time statistics of a benchmark run.
struct FrameStats {
    int frames = 0;
    double seconds = 0.0;
    double averageFps = 0.0;
    double low1Fps = 0.0;   // average FPS of the slowest 1 % of frames
    double minFps = 0.0;
    double worstMs = 0.0;
    double medianMs = 0.0;
};
FrameStats frameStats(const std::vector<float>& frameSeconds);
std::string formatFrameStats(const FrameStats& stats);

} // namespace engine
