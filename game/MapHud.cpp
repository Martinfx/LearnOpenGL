#include "MapHud.h"

#include <algorithm>
#include <vector>
#include <cmath>
#include <cstdio>

namespace {
const glm::vec4 kFrame(0.09f, 0.1f, 0.08f, 0.92f);
const glm::vec4 kLine(0.55f, 0.6f, 0.45f, 0.9f);
const glm::vec4 kText(0.86f, 0.84f, 0.72f, 1.0f);
const glm::vec4 kPlayer(0.95f, 0.78f, 0.25f, 1.0f);
const glm::vec4 kMarker(0.85f, 0.3f, 0.2f, 1.0f);
const glm::vec4 kTint(0.8f, 0.86f, 0.72f, 1.0f); // faded PDA screen

glm::vec2 rotate(glm::vec2 v, glm::vec2 forward)
{
    // Local (x right, y down on screen) -> world X, Z for a view looking along `forward`.
    const glm::vec2 right(-forward.y, forward.x);
    return right * v.x - forward * v.y;
}

void arrow(engine::Ui2D& ui, glm::vec2 at, glm::vec2 dir, float size, glm::vec4 color)
{
    const glm::vec2 side(-dir.y, dir.x);
    ui.triangle(at + dir * size, at - dir * size * 0.6f + side * size * 0.6f, at - dir * size * 0.3f, color);
    ui.triangle(at + dir * size, at - dir * size * 0.3f, at - dir * size * 0.6f - side * size * 0.6f, color);
}
} // namespace

MapHud::~MapHud() { engine::Renderer::deleteTexture(texture_); }

void MapHud::init(engine::Renderer& renderer, const engine::Scene& scene)
{
    engine::mapArea(scene, min_, size_);
    if (!scene.map.image.empty()) {
        texture_ = engine::Ui2D::loadImage(scene.map.image);
        ownImage_ = texture_ != 0;
    }
    if (!texture_)
        texture_ = renderer.renderTopDown(scene, min_, size_, 2048);
    for (const engine::Entity& e : scene.entities)
        if (e.type == "map_marker")
            markers_.push_back({e.name, {e.position.x, e.position.z}});
}

glm::vec2 MapHud::uv(glm::vec2 p) const
{
    const glm::vec2 t = (p - min_) / size_; // 0..1, y grows toward +Z (south, down on the map)
    return ownImage_ ? t : glm::vec2(t.x, 1.0f - t.y);
}

void MapHud::drawMinimap(engine::Ui2D& ui, int width, int height, glm::vec3 player, glm::vec3 forward3) const
{
    const float H = static_cast<float>(height);
    const float side = std::round(std::clamp(H * 0.24f, 140.0f, 320.0f));
    const float margin = std::round(H * 0.025f);
    const glm::vec2 pos(static_cast<float>(width) - side - margin, margin);
    const glm::vec2 center = pos + glm::vec2(side * 0.5f);
    glm::vec2 forward(forward3.x, forward3.z);
    forward = glm::length(forward) > 1e-4f ? glm::normalize(forward) : glm::vec2(0.0f, -1.0f);
    const float radius = 60.0f; // metres from the center to the edge

    ui.rect(pos - glm::vec2(3.0f), glm::vec2(side + 6.0f), kFrame);
    // The map turns so the player always looks up.
    const glm::vec2 local[4] = {{-1, -1}, {1, -1}, {1, 1}, {-1, 1}};
    glm::vec2 corners[4], uvs[4];
    const glm::vec2 me(player.x, player.z);
    for (int i = 0; i < 4; ++i) {
        corners[i] = center + local[i] * side * 0.5f;
        uvs[i] = uv(me + rotate(local[i] * radius, forward));
    }
    ui.imageQuad(texture_, corners, uvs, kTint);
    // Markers inside the square, as dots.
    for (const Marker& m : markers_) {
        const glm::vec2 d = m.position - me;
        const glm::vec2 right(-forward.y, forward.x);
        const glm::vec2 screen(glm::dot(d, right), -glm::dot(d, forward));
        if (std::abs(screen.x) > radius || std::abs(screen.y) > radius)
            continue;
        const glm::vec2 at = center + screen / radius * side * 0.5f;
        ui.rect(at - glm::vec2(4.0f), glm::vec2(8.0f), kMarker);
    }
    arrow(ui, center, {0.0f, -1.0f}, side * 0.045f, kPlayer);
    // North: an N on the edge in the direction of -Z.
    const glm::vec2 right(-forward.y, forward.x);
    glm::vec2 north(glm::dot(glm::vec2(0, -1), right), -glm::dot(glm::vec2(0, -1), forward));
    north /= std::max(std::abs(north.x), std::abs(north.y));
    const float textSize = std::round(side * 0.09f);
    const glm::vec2 n = center + north * (side * 0.5f - textSize * 0.6f);
    ui.text("N", n - glm::vec2(ui.textWidth("N", textSize, true) * 0.5f, textSize * 0.55f), textSize, kText, true);
    char coords[48];
    std::snprintf(coords, sizeof coords, "%.0f / %.0f", player.x, player.z);
    ui.text(coords, pos + glm::vec2(side - ui.textWidth(coords, textSize * 0.8f) - 4.0f, side + 6.0f), textSize * 0.8f, kText);
}

void MapHud::drawFullMap(engine::Ui2D& ui, int width, int height, glm::vec3 player, glm::vec3 forward3) const
{
    const float W = static_cast<float>(width), H = static_cast<float>(height);
    ui.rect({0, 0}, {W, H}, {0.02f, 0.02f, 0.018f, 0.75f});
    const float side = std::round(std::min(W, H) * 0.86f);
    const glm::vec2 pos((W - side) * 0.5f, (H - side) * 0.5f + H * 0.02f);
    // PDA frame.
    ui.rect(pos - glm::vec2(14.0f), glm::vec2(side + 28.0f), {0.13f, 0.13f, 0.11f, 1.0f});
    ui.rect(pos - glm::vec2(3.0f), glm::vec2(side + 6.0f), {0.03f, 0.03f, 0.03f, 1.0f});
    ui.image(texture_, pos, glm::vec2(side), kTint, ownImage_ ? glm::vec4(0, 0, 1, 1) : glm::vec4(0, 1, 1, 0));
    // Grid every 50 m.
    const float metre = side / size_;
    for (float g = std::ceil(min_.x / 50.0f) * 50.0f; g < min_.x + size_; g += 50.0f)
        ui.rect({pos.x + (g - min_.x) * metre, pos.y}, {1.0f, side}, {0.0f, 0.0f, 0.0f, 0.25f});
    for (float g = std::ceil(min_.y / 50.0f) * 50.0f; g < min_.y + size_; g += 50.0f)
        ui.rect({pos.x, pos.y + (g - min_.y) * metre}, {side, 1.0f}, {0.0f, 0.0f, 0.0f, 0.25f});
    auto toScreen = [&](glm::vec2 world) { return pos + (world - min_) * metre; };
    const float textSize = std::round(std::clamp(H * 0.022f, 12.0f, 26.0f));
    // Labels go right of the dot; if that overlaps an earlier label try left, below, above.
    std::vector<glm::vec4> placed; // x, y, w, h
    for (const Marker& m : markers_) {
        const glm::vec2 at = toScreen(m.position);
        ui.rect(at - glm::vec2(5.0f), glm::vec2(10.0f), kMarker);
        const glm::vec2 size(ui.textWidth(m.name, textSize, true) + 6.0f, textSize * 1.2f);
        const glm::vec2 tries[4] = {at + glm::vec2(9.0f, -size.y * 0.5f), at + glm::vec2(-9.0f - size.x, -size.y * 0.5f),
                                    at + glm::vec2(-size.x * 0.5f, 8.0f), at + glm::vec2(-size.x * 0.5f, -8.0f - size.y)};
        glm::vec2 best = tries[0];
        for (const glm::vec2& t : tries) {
            const bool outside = t.x < pos.x || t.y < pos.y || t.x + size.x > pos.x + side || t.y + size.y > pos.y + side;
            const bool hit = outside || std::any_of(placed.begin(), placed.end(), [&](const glm::vec4& r) {
                return t.x < r.x + r.z && t.x + size.x > r.x && t.y < r.y + r.w && t.y + size.y > r.y;
            });
            if (!hit) {
                best = t;
                break;
            }
        }
        placed.push_back({best.x, best.y, size.x, size.y});
        ui.rect(best, size, {0.0f, 0.0f, 0.0f, 0.45f});
        ui.text(m.name, best + glm::vec2(3.0f, size.y * 0.5f - textSize * 0.6f), textSize, kText, true);
    }
    glm::vec2 forward(forward3.x, forward3.z);
    forward = glm::length(forward) > 1e-4f ? glm::normalize(forward) : glm::vec2(0.0f, -1.0f);
    const glm::vec2 me = toScreen({player.x, player.z});
    arrow(ui, me, forward, std::max(9.0f, side * 0.018f), kPlayer);
    ui.text("PDA", pos + glm::vec2(0.0f, -textSize * 2.2f), textSize * 1.4f, kText, true);
    const std::string help = "M  zavřít mapu";
    ui.text(help, pos + glm::vec2(side - ui.textWidth(help, textSize), -textSize * 1.9f), textSize, kText);
    ui.text("N", pos + glm::vec2(side * 0.5f - textSize * 0.4f, 4.0f), textSize, kText, true);
}
