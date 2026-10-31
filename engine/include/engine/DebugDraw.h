#pragma once
#include "engine/Renderer.h"

#include <glm/glm.hpp>

#include <mutex>
#include <string>
#include <vector>

namespace engine {

// Immediate debug shapes from C++ and Lua (game.debug_*): lines, boxes,
// spheres, arrows and world-space text, each kept for a number of seconds
// (0 = until the next tick: the game ticks once per simulation step). Thread-safe: AI jobs may add shapes too. The game
// copies them into a DebugGeometry every frame (`r_debugdraw 0` hides them).
class DebugDraw {
public:
    struct Text {
        glm::vec3 position;
        std::string text;
        glm::vec4 color;
        float remaining;
    };

    static DebugDraw& global();

    void line(const glm::vec3& a, const glm::vec3& b, const glm::vec4& color = {1, 1, 0, 1}, float seconds = 0.0f);
    void arrow(const glm::vec3& from, const glm::vec3& to, const glm::vec4& color = {1, 1, 0, 1}, float seconds = 0.0f);
    void box(const glm::vec3& center, const glm::vec3& halfExtents, const glm::vec4& color = {0, 1, 1, 1},
             float seconds = 0.0f);
    void box(const glm::mat4& transform, const glm::vec3& halfExtents, const glm::vec4& color, float seconds = 0.0f);
    void sphere(const glm::vec3& center, float radius, const glm::vec4& color = {1, 0.4f, 0.2f, 1}, float seconds = 0.0f);
    void cross(const glm::vec3& point, float size, const glm::vec4& color = {1, 1, 1, 1}, float seconds = 0.0f);
    void text(const glm::vec3& position, const std::string& text, const glm::vec4& color = {1, 1, 1, 1},
              float seconds = 0.0f);

    // Adds the current lines (with per-line colours) to `out` and returns the texts.
    void collect(DebugGeometry& out, std::vector<Text>* texts = nullptr) const;
    // Ages everything by dt; shapes with 0 seconds live until the first tick after they were added.
    void tick(float dt);
    void clear();
    std::size_t lineCount() const;

private:
    struct Line {
        glm::vec3 a, b;
        glm::vec4 color;
        float remaining;
    };
    mutable std::mutex mutex_;
    std::vector<Line> lines_;
    std::vector<Text> texts_;
};

} // namespace engine
