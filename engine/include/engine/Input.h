#pragma once
#include <array>
#include <cstddef>
#include <glm/vec2.hpp>

namespace engine {

// Game actions. Platforms (GLFW in the game, Qt in the editor) map their
// keys onto these, so gameplay code never sees a physical key.
enum class Action : std::size_t {
    MoveForward,
    MoveBack,
    MoveLeft,
    MoveRight,
    Jump,
    Sprint,
    Fire,
    Reload,
    Count
};

struct InputState {
    std::array<bool, static_cast<std::size_t>(Action::Count)> down{};
    glm::vec2 lookDelta{0.0f}; // mouse movement in pixels since the last frame

    bool isDown(Action a) const { return down[static_cast<std::size_t>(a)]; }
    void set(Action a, bool value) { down[static_cast<std::size_t>(a)] = value; }
};

} // namespace engine
