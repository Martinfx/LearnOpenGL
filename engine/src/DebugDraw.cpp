#include "engine/DebugDraw.h"

#include <glm/gtc/constants.hpp>

#include <algorithm>

namespace engine {

namespace {
constexpr std::size_t kMaxLines = 200000; // a runaway script cannot eat all memory
}

DebugDraw& DebugDraw::global()
{
    static DebugDraw instance;
    return instance;
}

void DebugDraw::line(const glm::vec3& a, const glm::vec3& b, const glm::vec4& color, float seconds)
{
    std::lock_guard lock(mutex_);
    if (lines_.size() < kMaxLines)
        lines_.push_back({a, b, color, seconds});
}

void DebugDraw::arrow(const glm::vec3& from, const glm::vec3& to, const glm::vec4& color, float seconds)
{
    const glm::vec3 d = to - from;
    const float length = glm::length(d);
    line(from, to, color, seconds);
    if (length < 1e-4f)
        return;
    const glm::vec3 dir = d / length;
    const glm::vec3 side = glm::normalize(glm::abs(dir.y) > 0.95f ? glm::cross(dir, glm::vec3(1, 0, 0))
                                                                     : glm::cross(dir, glm::vec3(0, 1, 0)));
    const glm::vec3 up = glm::cross(side, dir);
    const float head = std::min(0.25f, length * 0.3f);
    for (const glm::vec3& s : {side, -side, up, -up})
        line(to, to - dir * head + s * head * 0.4f, color, seconds);
}

void DebugDraw::box(const glm::vec3& center, const glm::vec3& halfExtents, const glm::vec4& color, float seconds)
{
    glm::mat4 t(1.0f);
    t[3] = glm::vec4(center, 1.0f);
    box(t, halfExtents, color, seconds);
}

void DebugDraw::box(const glm::mat4& transform, const glm::vec3& h, const glm::vec4& color, float seconds)
{
    glm::vec3 c[8];
    for (int i = 0; i < 8; ++i)
        c[i] = glm::vec3(transform * glm::vec4((i & 1) ? h.x : -h.x, (i & 2) ? h.y : -h.y, (i & 4) ? h.z : -h.z, 1.0f));
    static constexpr int kEdges[12][2] = {{0, 1}, {2, 3}, {4, 5}, {6, 7}, {0, 2}, {1, 3},
                                          {4, 6}, {5, 7}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};
    for (const auto& e : kEdges)
        line(c[e[0]], c[e[1]], color, seconds);
}

void DebugDraw::sphere(const glm::vec3& center, float radius, const glm::vec4& color, float seconds)
{
    constexpr int kSegments = 24;
    for (int axis = 0; axis < 3; ++axis) {
        glm::vec3 prev{};
        for (int i = 0; i <= kSegments; ++i) {
            const float a = glm::two_pi<float>() * static_cast<float>(i) / kSegments;
            const float s = std::sin(a) * radius, k = std::cos(a) * radius;
            const glm::vec3 p = center + (axis == 0 ? glm::vec3(0, s, k) : axis == 1 ? glm::vec3(s, 0, k) : glm::vec3(s, k, 0));
            if (i > 0)
                line(prev, p, color, seconds);
            prev = p;
        }
    }
}

void DebugDraw::cross(const glm::vec3& p, float size, const glm::vec4& color, float seconds)
{
    const float h = size * 0.5f;
    line(p - glm::vec3(h, 0, 0), p + glm::vec3(h, 0, 0), color, seconds);
    line(p - glm::vec3(0, h, 0), p + glm::vec3(0, h, 0), color, seconds);
    line(p - glm::vec3(0, 0, h), p + glm::vec3(0, 0, h), color, seconds);
}

void DebugDraw::text(const glm::vec3& position, const std::string& text, const glm::vec4& color, float seconds)
{
    std::lock_guard lock(mutex_);
    if (texts_.size() < 4096)
        texts_.push_back({position, text, color, seconds});
}

void DebugDraw::collect(DebugGeometry& out, std::vector<Text>* texts) const
{
    std::lock_guard lock(mutex_);
    out.lines.reserve(out.lines.size() + lines_.size() * 2);
    out.lineColors.reserve(out.lineColors.size() + lines_.size() * 2);
    // Earlier lines of `out` (if any) keep the shared lineColor.
    out.lineColors.resize(out.lines.size(), out.lineColor);
    for (const Line& l : lines_) {
        out.lines.push_back(l.a);
        out.lines.push_back(l.b);
        out.lineColors.push_back(l.color);
        out.lineColors.push_back(l.color);
    }
    if (texts)
        texts->insert(texts->end(), texts_.begin(), texts_.end());
}

void DebugDraw::tick(float dt)
{
    std::lock_guard lock(mutex_);
    auto expired = [dt](auto& item) {
        item.remaining -= dt;
        return item.remaining <= 0.0f;
    };
    lines_.erase(std::remove_if(lines_.begin(), lines_.end(), expired), lines_.end());
    texts_.erase(std::remove_if(texts_.begin(), texts_.end(), expired), texts_.end());
}

void DebugDraw::clear()
{
    std::lock_guard lock(mutex_);
    lines_.clear();
    texts_.clear();
}

std::size_t DebugDraw::lineCount() const
{
    std::lock_guard lock(mutex_);
    return lines_.size();
}

} // namespace engine
