#include "UiScale.h"
#include "MapHud.h"

#include <engine/Anomaly.h>
#include <engine/JobSystem.h>
#include <engine/Localization.h>
#include <engine/Log.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

namespace {
const glm::vec4 kBezel(0.105f, 0.11f, 0.095f, 1.0f);
const glm::vec4 kBezelEdge(0.17f, 0.175f, 0.15f, 1.0f);
const glm::vec4 kScreenBack(0.045f, 0.055f, 0.045f, 1.0f);
const glm::vec4 kText(0.86f, 0.88f, 0.74f, 1.0f);
const glm::vec4 kDim(0.62f, 0.68f, 0.55f, 0.85f);
const glm::vec4 kPlayer(0.98f, 0.82f, 0.3f, 1.0f);
const glm::vec4 kMarker(0.92f, 0.36f, 0.22f, 1.0f);
const glm::vec4 kQuest(1.0f, 0.72f, 0.18f, 1.0f);
const glm::vec4 kLed(0.35f, 0.9f, 0.4f, 1.0f);

glm::vec2 rotate(glm::vec2 v, glm::vec2 forward)
{
    // Local (x right, y down on screen) -> world X, Z for a view looking along `forward`.
    const glm::vec2 right(-forward.y, forward.x);
    return right * v.x - forward * v.y;
}

void diamond(engine::Ui2D& ui, glm::vec2 at, float r, glm::vec4 color)
{
    ui.triangle(at + glm::vec2(0, -r), at + glm::vec2(r, 0), at + glm::vec2(0, r), color);
    ui.triangle(at + glm::vec2(0, -r), at + glm::vec2(0, r), at + glm::vec2(-r, 0), color);
}

void arrow(engine::Ui2D& ui, glm::vec2 at, glm::vec2 dir, float size, glm::vec4 color)
{
    const glm::vec2 side(-dir.y, dir.x);
    ui.triangle(at + dir * size, at - dir * size * 0.6f + side * size * 0.6f, at - dir * size * 0.3f, color);
    ui.triangle(at + dir * size, at - dir * size * 0.3f, at - dir * size * 0.6f - side * size * 0.6f, color);
}

void disc(engine::Ui2D& ui, glm::vec2 c, float r, glm::vec4 color, int segments = 20)
{
    for (int i = 0; i < segments; ++i) {
        const float a0 = 6.2831853f * static_cast<float>(i) / static_cast<float>(segments);
        const float a1 = 6.2831853f * static_cast<float>(i + 1) / static_cast<float>(segments);
        ui.triangle(c, c + r * glm::vec2(std::cos(a0), std::sin(a0)), c + r * glm::vec2(std::cos(a1), std::sin(a1)), color);
    }
}

void ring(engine::Ui2D& ui, glm::vec2 c, float r, float w, glm::vec4 color, int segments = 28)
{
    for (int i = 0; i < segments; ++i) {
        const float a0 = 6.2831853f * static_cast<float>(i) / static_cast<float>(segments);
        const float a1 = 6.2831853f * static_cast<float>(i + 1) / static_cast<float>(segments);
        const glm::vec2 d0(std::cos(a0), std::sin(a0)), d1(std::cos(a1), std::sin(a1));
        ui.triangle(c + d0 * (r - w), c + d0 * r, c + d1 * r, color);
        ui.triangle(c + d0 * (r - w), c + d1 * r, c + d1 * (r - w), color);
    }
}

// The player's field of view as a faint wedge.
void viewCone(engine::Ui2D& ui, glm::vec2 at, glm::vec2 dir, float length, glm::vec4 color)
{
    const float half = 0.6f; // ~35 degrees
    const int n = 6;
    const float base = std::atan2(dir.y, dir.x);
    for (int i = 0; i < n; ++i) {
        const float a0 = base - half + 2.0f * half * static_cast<float>(i) / n;
        const float a1 = base - half + 2.0f * half * static_cast<float>(i + 1) / n;
        ui.triangle(at, at + length * glm::vec2(std::cos(a0), std::sin(a0)), at + length * glm::vec2(std::cos(a1), std::sin(a1)), color);
    }
}

// Worn screen: thin dark scan lines and darker edges.
void screenWear(engine::Ui2D& ui, glm::vec2 pos, glm::vec2 size, float scanlines)
{
    if (scanlines > 0.0f)
        for (float y = pos.y + 1.0f; y < pos.y + size.y; y += 3.0f)
            ui.rect({pos.x, y}, {size.x, 1.0f}, {0.0f, 0.0f, 0.0f, 0.22f * scanlines});
    const int steps = 8;
    const float edge = std::min(size.x, size.y) * 0.06f;
    for (int i = 0; i < steps; ++i) {
        const float t = static_cast<float>(i) / steps, w = edge / steps;
        const glm::vec4 c(0.0f, 0.0f, 0.0f, 0.09f * (1.0f - t));
        const float o = edge * t;
        ui.rect({pos.x + o, pos.y + o}, {size.x - 2 * o, w}, c);
        ui.rect({pos.x + o, pos.y + size.y - o - w}, {size.x - 2 * o, w}, c);
        ui.rect({pos.x + o, pos.y + o + w}, {w, size.y - 2 * o - 2 * w}, c);
        ui.rect({pos.x + size.x - o - w, pos.y + o + w}, {w, size.y - 2 * o - 2 * w}, c);
    }
}

void bezel(engine::Ui2D& ui, glm::vec2 pos, glm::vec2 size, float b)
{
    ui.rect(pos - glm::vec2(b + 2.0f), size + glm::vec2(2.0f * b + 4.0f), {0.0f, 0.0f, 0.0f, 0.55f});
    ui.rect(pos - glm::vec2(b), size + glm::vec2(2.0f * b), kBezelEdge);
    ui.rect(pos - glm::vec2(b - 2.0f), size + glm::vec2(2.0f * b - 4.0f), kBezel);
    ui.rect(pos - glm::vec2(2.0f), size + glm::vec2(4.0f), {0.02f, 0.02f, 0.018f, 1.0f});
    // Screws in the corners.
    const float s = std::max(3.0f, b * 0.22f);
    for (glm::vec2 c : {pos + glm::vec2(-b * 0.5f), pos + glm::vec2(size.x + b * 0.5f, -b * 0.5f), pos + glm::vec2(-b * 0.5f, size.y + b * 0.5f),
                        pos + size + glm::vec2(b * 0.5f)}) {
        disc(ui, c, s, {0.24f, 0.24f, 0.21f, 1.0f}, 10);
        ui.rect(c - glm::vec2(s * 0.7f, 0.5f), {s * 1.4f, 1.0f}, {0.06f, 0.06f, 0.05f, 1.0f});
    }
}

glm::vec4 anomalyColor(engine::AnomalyKind kind)
{
    const glm::vec3 c = engine::anomalyKindInfo(kind).color;
    return {c, 1.0f};
}

// A round length for the scale bar (1, 2, 5 x 10^n metres) close to `metres`.
float niceLength(float metres)
{
    const float p = std::pow(10.0f, std::floor(std::log10(std::max(metres, 0.1f))));
    for (float m : {1.0f, 2.0f, 5.0f, 10.0f})
        if (m * p >= metres * 0.7f)
            return m * p;
    return 10.0f * p;
}
} // namespace

MapHud::~MapHud() { release(); }

void MapHud::release()
{
    if (texture_)
        engine::Renderer::deleteTexture(texture_);
    texture_ = 0;
}

void MapHud::begin(engine::Renderer& renderer, const engine::Scene& scene, const std::filesystem::path& levelFile)
{
    (void)renderer;
    release();
    settings_ = scene.map;
    area_ = engine::mapAreaOf(scene);
    markers_.clear();
    anomalies_.clear();
    for (const engine::Entity& e : scene.entities) {
        if (e.type == "map_marker")
            markers_.push_back({e.name, {e.position.x, e.position.z}});
        else if (e.type == "anomaly") {
            const engine::AnomalySettings a = engine::anomalySettings(e);
            anomalies_.push_back({{e.position.x, e.position.z}, std::max(1.0f, e.size.x * 0.5f), anomalyColor(a.kind)});
        }
    }
    center_ = area_.min + area_.size * 0.5f;
    zoom_ = 1.0f;
    baker_.reset();
    cooking_.reset();
    if (!scene.map.image.empty()) {
        texture_ = engine::Ui2D::loadImage(scene.map.image);
        if (texture_)
            return;
        LOG_WARN("map", "cannot read the map image %s, rendering the level from above", scene.map.image.string().c_str());
    }
    cacheFile_ = engine::mapCachePath(levelFile, scene);
    const bool rebake = std::getenv("FPS_ENGINE_MAP_REBAKE") != nullptr; // test hook
    engine::CookedTexture cooked;
    engine::MapArea area;
    if (!rebake && engine::readMapCache(cacheFile_, cooked, area)) {
        texture_ = engine::uploadMapTexture(cooked);
        if (texture_) {
            area_ = area;
            center_ = area_.min + area_.size * 0.5f;
            return;
        }
    }
    baker_ = std::make_unique<engine::MapBaker>();
    baker_->begin(scene);
    area_ = baker_->area();
    center_ = area_.min + area_.size * 0.5f;
}

float MapHud::step(engine::Renderer& renderer, const engine::Scene& scene, double budgetMs)
{
    if (texture_ || !baker_)
        return 1.0f;
    if (!baker_->done()) {
        baker_->step(renderer, scene, budgetMs);
        return 0.9f * baker_->progress();
    }
    if (!cooking_) {
        // Grading, outlines and compression on a worker; the cache file is written there too.
        engine::MapBaker* baker = baker_.get();
        const std::filesystem::path file = cacheFile_;
        cooking_ = std::make_shared<std::future<engine::CookedTexture>>(engine::JobSystem::global().submit([baker, file] {
            const engine::MapImage image = baker->finish();
            if (const char* dump = std::getenv("FPS_ENGINE_MAP_DUMP")) { // test hook: the baked picture as a .ppm
                if (std::FILE* f = std::fopen(dump, "wb")) {
                    std::fprintf(f, "P6\n%d %d\n255\n", image.width, image.height);
                    for (std::size_t i = 0; i < image.rgba.size(); i += 4)
                        std::fwrite(&image.rgba[i], 1, 3, f);
                    std::fclose(f);
                }
            }
            engine::CookedTexture cooked = engine::cookMapImage(image);
            if (!engine::writeMapCache(file, cooked, image.area))
                LOG_WARN("map", "cannot write the map cache %s", file.string().c_str());
            return cooked;
        }));
        return 0.92f;
    }
    if (cooking_->wait_for(std::chrono::milliseconds(static_cast<int>(budgetMs))) != std::future_status::ready)
        return 0.96f;
    finishBake();
    return 1.0f;
}

void MapHud::finishBake()
{
    const engine::CookedTexture cooked = cooking_->get();
    texture_ = engine::uploadMapTexture(cooked);
    area_ = baker_->area();
    cooking_.reset();
    baker_.reset();
    LOG_INFO("map", "map baked: %d x %d px, cached in %s", cooked.width, cooked.height, cacheFile_.string().c_str());
}

glm::vec2 MapHud::uv(glm::vec2 p) const
{
    return (p - area_.min) / area_.size; // row 0 of the image = north (-Z), top of the screen
}

void MapHud::opened(glm::vec3 player)
{
    center_ = glm::vec2(player.x, player.z);
    if (zoom_ <= 1.0f)
        center_ = area_.min + area_.size * 0.5f;
}

void MapHud::control(glm::vec2 mouseDelta, float wheel, bool recenter, glm::vec3 player)
{
    const float fit = std::max(area_.size.x / std::max(1.0f, screenSize_.x), area_.size.y / std::max(1.0f, screenSize_.y));
    if (wheel != 0.0f)
        zoom_ = std::clamp(zoom_ * std::pow(1.25f, wheel), 1.0f, std::max(1.0f, settings_.maxZoom));
    const float mpp = fit / zoom_;
    center_ += mouseDelta * mpp;
    if (recenter)
        center_ = glm::vec2(player.x, player.z);
    // Keep the middle of the screen over the map (at zoom 1 it stays centred).
    const glm::vec2 halfView = screenSize_ * 0.5f * mpp;
    const glm::vec2 lo = area_.min + glm::min(halfView, area_.size * 0.5f), hi = area_.min + area_.size - glm::min(halfView, area_.size * 0.5f);
    center_ = glm::clamp(center_, lo, hi);
}

void MapHud::drawMinimap(engine::Ui2D& ui, int width, int height, glm::vec3 player, glm::vec3 forward3, const std::vector<QuestView>& quests) const
{
    if (!texture_)
        return;
    const float H = static_cast<float>(height);
    const float side = std::round(std::clamp(H * 0.24f, 140.0f, 320.0f) * uiScale());
    const float margin = std::round(H * 0.03f);
    const float b = std::max(6.0f, side * 0.035f);
    const glm::vec2 pos(static_cast<float>(width) - side - margin - b, margin + b);
    const glm::vec2 center = pos + glm::vec2(side * 0.5f);
    glm::vec2 forward(forward3.x, forward3.z);
    forward = glm::length(forward) > 1e-4f ? glm::normalize(forward) : glm::vec2(0.0f, -1.0f);
    const float radius = std::max(10.0f, settings_.minimapRadius); // metres from the center to the edge
    const glm::vec2 right(-forward.y, forward.x);
    const glm::vec2 me(player.x, player.z);
    auto toScreen = [&](glm::vec2 world) {
        const glm::vec2 d = world - me;
        return glm::vec2(glm::dot(d, right), -glm::dot(d, forward)) / radius * side * 0.5f;
    };

    bezel(ui, pos, glm::vec2(side), b);
    ui.rect(pos, glm::vec2(side), kScreenBack);
    // The map turns so the player always looks up.
    const glm::vec2 local[4] = {{-1, -1}, {1, -1}, {1, 1}, {-1, 1}};
    glm::vec2 corners[4], uvs[4];
    for (int i = 0; i < 4; ++i) {
        corners[i] = center + local[i] * side * 0.5f;
        uvs[i] = uv(me + rotate(local[i] * radius, forward));
    }
    ui.imageQuad(texture_, corners, uvs, glm::vec4(settings_.tint, 1.0f));
    const float px = side * 0.5f / radius; // pixels per metre
    if (settings_.showAnomalies)
        for (const Anomaly& a : anomalies_) {
            const glm::vec2 s = toScreen(a.position);
            const float r = std::max(3.0f, a.radius * px);
            if (std::abs(s.x) + r > side * 0.5f || std::abs(s.y) + r > side * 0.5f)
                continue;
            disc(ui, center + s, r, {glm::vec3(a.color), 0.22f}, 14);
            ring(ui, center + s, r, 1.5f, {glm::vec3(a.color), 0.8f}, 18);
        }
    for (const Marker& m : markers_) {
        const glm::vec2 s = toScreen(m.position);
        if (std::abs(s.x) > side * 0.47f || std::abs(s.y) > side * 0.47f)
            continue;
        ui.rect(center + s - glm::vec2(4.0f), glm::vec2(8.0f), {0.0f, 0.0f, 0.0f, 0.6f});
        ui.rect(center + s - glm::vec2(3.0f), glm::vec2(6.0f), kMarker);
    }
    // Quest targets: a diamond, held at the edge when farther than the minimap shows.
    for (const QuestView& q : quests) {
        if (!q.active)
            continue;
        for (const glm::vec2& t : q.targets) {
            glm::vec2 s = toScreen(t);
            const float far = std::max(std::abs(s.x), std::abs(s.y));
            const bool outside = far > side * 0.46f;
            if (outside)
                s *= side * 0.46f / far;
            const float r = side * (outside ? 0.025f : 0.035f);
            diamond(ui, center + s, r + 1.5f, {0.0f, 0.0f, 0.0f, 0.6f});
            diamond(ui, center + s, r, kQuest);
        }
    }
    viewCone(ui, center, {0.0f, -1.0f}, side * 0.22f, {kPlayer.r, kPlayer.g, kPlayer.b, 0.12f});
    arrow(ui, center, {0.0f, -1.0f}, side * 0.05f + 1.5f, {0.0f, 0.0f, 0.0f, 0.7f});
    arrow(ui, center, {0.0f, -1.0f}, side * 0.05f, kPlayer);
    screenWear(ui, pos, glm::vec2(side), settings_.scanlines);
    // North: a letter on the edge in the direction of -Z.
    glm::vec2 north(glm::dot(glm::vec2(0, -1), right), -glm::dot(glm::vec2(0, -1), forward));
    north /= std::max(std::abs(north.x), std::abs(north.y));
    const float textSize = std::round(side * 0.085f);
    const std::string n = engine::tr("map.north");
    const glm::vec2 at = center + north * (side * 0.5f - textSize * 0.7f);
    disc(ui, at, textSize * 0.62f, {0.0f, 0.0f, 0.0f, 0.55f}, 14);
    ui.text(n, at - glm::vec2(ui.textWidth(n, textSize, true) * 0.5f, textSize * 0.55f), textSize, kText, true);
    char coords[48];
    std::snprintf(coords, sizeof coords, "%.0f / %.0f", player.x, player.z);
    ui.text(coords, pos + glm::vec2(side - ui.textWidth(coords, textSize * 0.8f), side + b + 4.0f), textSize * 0.8f, kDim);
}

void MapHud::drawFullMap(engine::Ui2D& ui, int width, int height, glm::vec3 player, glm::vec3 forward3, const std::vector<QuestView>& quests) const
{
    const float W = static_cast<float>(width), H = static_cast<float>(height);
    ui.rect({0, 0}, {W, H}, {0.02f, 0.02f, 0.018f, 0.78f});
    const float textSize = uiSize(H, 0.021f, 12.0f, 26.0f);
    const float margin = std::round(H * 0.05f);
    const float b = std::round(std::max(10.0f, H * 0.022f));
    // Quest list on the right when the window is wide enough, else over the map.
    const bool sideList = !quests.empty() && W > H * 1.45f;
    const float listW = sideList ? std::round(std::clamp(W * 0.24f, 220.0f, 520.0f)) : 0.0f;
    const glm::vec2 pos(margin + b, margin + b + textSize * 1.6f);
    const glm::vec2 size(W - pos.x - b - margin - (sideList ? listW + margin : 0.0f), H - pos.y - b - margin - textSize * 1.4f);
    screenSize_ = size;
    bezel(ui, pos, size, b);
    ui.rect(pos, size, kScreenBack);

    // View: zoom 1 fits the whole area, the middle at center_.
    const float fit = std::max(area_.size.x / size.x, area_.size.y / size.y);
    const float mpp = fit / std::max(1.0f, zoom_);
    const glm::vec2 viewMin = center_ - size * 0.5f * mpp;
    auto toScreen = [&](glm::vec2 world) { return pos + (world - viewMin) / mpp; };
    auto inside = [&](glm::vec2 s, float r = 0.0f) {
        return s.x - r >= pos.x && s.y - r >= pos.y && s.x + r <= pos.x + size.x && s.y + r <= pos.y + size.y;
    };
    if (texture_) {
        const glm::vec2 uv0 = uv(viewMin), uv1 = uv(viewMin + size * mpp);
        ui.image(texture_, pos, size, glm::vec4(settings_.tint, 1.0f), glm::vec4(uv0, uv1));
    } else {
        const std::string wait = engine::tr("loading.map");
        ui.text(wait, pos + size * 0.5f - glm::vec2(ui.textWidth(wait, textSize) * 0.5f, textSize * 0.5f), textSize, kDim);
    }
    // Grid with sector letters (columns) and numbers (rows), like a paper map.
    if (settings_.grid > 0.0f) {
        const float g = settings_.grid;
        const glm::vec4 line(0.0f, 0.0f, 0.0f, 0.28f), light(0.85f, 0.9f, 0.7f, 0.07f);
        const float ts = textSize * 0.72f;
        for (float x = std::ceil(viewMin.x / g) * g; x < viewMin.x + size.x * mpp; x += g) {
            const float sx = std::round(toScreen({x, 0.0f}).x);
            ui.rect({sx, pos.y}, {1.0f, size.y}, line);
            ui.rect({sx + 1.0f, pos.y}, {1.0f, size.y}, light);
            const int col = static_cast<int>(std::floor((x - area_.min.x) / g));
            if (col >= 0 && col < 26 && x < area_.min.x + area_.size.x - 1.0f && g / mpp > ts * 2.0f) {
                const std::string label(1, static_cast<char>('A' + col));
                ui.text(label, {sx + (g / mpp - ui.textWidth(label, ts)) * 0.5f, pos.y + 3.0f}, ts, kDim);
            }
        }
        for (float z = std::ceil(viewMin.y / g) * g; z < viewMin.y + size.y * mpp; z += g) {
            const float sy = std::round(toScreen({0.0f, z}).y);
            ui.rect({pos.x, sy}, {size.x, 1.0f}, line);
            ui.rect({pos.x, sy + 1.0f}, {size.x, 1.0f}, light);
            const int row = static_cast<int>(std::floor((z - area_.min.y) / g)) + 1;
            if (row >= 1 && z < area_.min.y + area_.size.y - 1.0f && g / mpp > ts * 1.6f)
                ui.text(std::to_string(row), {pos.x + 4.0f, sy + (g / mpp - ts) * 0.5f}, ts, kDim);
        }
    }
    // Anomaly fields: tinted discs with a rim.
    if (settings_.showAnomalies)
        for (const Anomaly& a : anomalies_) {
            const glm::vec2 s = toScreen(a.position);
            const float r = std::max(4.0f, a.radius / mpp);
            if (!inside(s, r))
                continue;
            disc(ui, s, r, {glm::vec3(a.color), 0.2f}, 20);
            ring(ui, s, r, std::max(1.5f, r * 0.08f), {glm::vec3(a.color), 0.85f});
            disc(ui, s, 2.5f, {glm::vec3(a.color), 1.0f}, 8);
        }
    // Markers: labels right of the dot; if that overlaps an earlier label try left, below, above.
    std::vector<glm::vec4> placed; // x, y, w, h
    for (const Marker& m : markers_) {
        const glm::vec2 at = toScreen(m.position);
        if (!inside(at, 4.0f))
            continue;
        ui.rect(at - glm::vec2(6.0f), glm::vec2(12.0f), {0.0f, 0.0f, 0.0f, 0.65f});
        ui.rect(at - glm::vec2(4.5f), glm::vec2(9.0f), kMarker);
        const std::string label = engine::localize(m.name);
        const glm::vec2 lsize(ui.textWidth(label, textSize, true) + 8.0f, textSize * 1.25f);
        const glm::vec2 tries[4] = {at + glm::vec2(10.0f, -lsize.y * 0.5f), at + glm::vec2(-10.0f - lsize.x, -lsize.y * 0.5f),
                                    at + glm::vec2(-lsize.x * 0.5f, 9.0f), at + glm::vec2(-lsize.x * 0.5f, -9.0f - lsize.y)};
        glm::vec2 best = tries[0];
        for (const glm::vec2& t : tries) {
            const bool out = t.x < pos.x || t.y < pos.y || t.x + lsize.x > pos.x + size.x || t.y + lsize.y > pos.y + size.y;
            const bool hit = out || std::any_of(placed.begin(), placed.end(), [&](const glm::vec4& r) {
                return t.x < r.x + r.z && t.x + lsize.x > r.x && t.y < r.y + r.w && t.y + lsize.y > r.y;
            });
            if (!hit) {
                best = t;
                break;
            }
        }
        placed.push_back({best.x, best.y, lsize.x, lsize.y});
        ui.rect(best, lsize, {0.03f, 0.035f, 0.03f, 0.78f});
        ui.rect(best, {2.0f, lsize.y}, kMarker);
        ui.text(label, best + glm::vec2(5.0f, lsize.y * 0.5f - textSize * 0.6f), textSize, kText, true);
    }
    for (const QuestView& q : quests)
        if (q.active)
            for (const glm::vec2& t : q.targets) {
                const glm::vec2 at = glm::clamp(toScreen(t), pos + glm::vec2(10.0f), pos + size - glm::vec2(10.0f));
                diamond(ui, at, std::max(9.0f, textSize * 0.6f), {0.0f, 0.0f, 0.0f, 0.65f});
                diamond(ui, at, std::max(7.0f, textSize * 0.48f), kQuest);
            }
    glm::vec2 forward(forward3.x, forward3.z);
    forward = glm::length(forward) > 1e-4f ? glm::normalize(forward) : glm::vec2(0.0f, -1.0f);
    const glm::vec2 me = toScreen({player.x, player.z});
    if (inside(me)) {
        viewCone(ui, me, forward, std::max(40.0f, 35.0f / mpp), {kPlayer.r, kPlayer.g, kPlayer.b, 0.13f});
        arrow(ui, me, forward, std::max(10.0f, textSize * 0.75f) + 2.0f, {0.0f, 0.0f, 0.0f, 0.7f});
        arrow(ui, me, forward, std::max(10.0f, textSize * 0.75f), kPlayer);
    }
    screenWear(ui, pos, size, settings_.scanlines);

    // Scale bar (bottom left of the screen) and the compass.
    {
        const float metres = niceLength(size.x * 0.14f * mpp);
        const float len = metres / mpp;
        const glm::vec2 at(pos.x + textSize, pos.y + size.y - textSize * 1.6f);
        ui.rect(at - glm::vec2(4.0f, textSize * 1.4f), {len + 8.0f + textSize * 4.0f, textSize * 2.0f}, {0.0f, 0.0f, 0.0f, 0.5f});
        ui.rect(at, {len, 3.0f}, kText);
        ui.rect(at - glm::vec2(0.0f, 6.0f), {2.0f, 9.0f}, kText);
        ui.rect(at + glm::vec2(len - 2.0f, -6.0f), {2.0f, 9.0f}, kText);
        char text[32];
        std::snprintf(text, sizeof text, "%.0f m", metres);
        ui.text(text, at + glm::vec2(len + 8.0f, -textSize * 0.75f), textSize * 0.9f, kText);
        const std::string n = engine::tr("map.north");
        const glm::vec2 c(pos.x + size.x - textSize * 1.6f, pos.y + textSize * 1.8f);
        disc(ui, c, textSize * 1.1f, {0.0f, 0.0f, 0.0f, 0.5f}, 18);
        ring(ui, c, textSize * 1.1f, 1.5f, kDim, 22);
        ui.triangle(c + glm::vec2(0.0f, -textSize * 0.95f), c + glm::vec2(textSize * 0.28f, 0.0f), c + glm::vec2(-textSize * 0.28f, 0.0f), kMarker);
        ui.triangle(c + glm::vec2(0.0f, textSize * 0.95f), c + glm::vec2(-textSize * 0.28f, 0.0f), c + glm::vec2(textSize * 0.28f, 0.0f), kDim);
        ui.text(n, c + glm::vec2(-ui.textWidth(n, textSize * 0.8f, true) * 0.5f, -textSize * 2.25f), textSize * 0.8f, kText, true);
    }

    // Device labels on the bezel: name, status light, controls.
    ui.text("PDA", pos + glm::vec2(0.0f, -b - textSize * 1.5f), textSize * 1.25f, kText, true);
    disc(ui, pos + glm::vec2(textSize * 3.3f, -b - textSize * 0.8f), textSize * 0.22f, kLed, 10);
    char zoomText[48];
    std::snprintf(zoomText, sizeof zoomText, "x%.1f   %.0f / %.0f", zoom_, player.x, player.z);
    ui.text(zoomText, pos + glm::vec2(textSize * 4.2f, -b - textSize * 1.35f), textSize * 0.9f, kDim);
    const std::string help = engine::tr("map.controls");
    ui.text(help, pos + glm::vec2(size.x - ui.textWidth(help, textSize * 0.9f), -b - textSize * 1.35f), textSize * 0.9f, kDim);

    // Quest list (PDA tasks) beside the map, or over its top left corner on narrow screens.
    if (!quests.empty()) {
        float x = sideList ? pos.x + size.x + b + margin : pos.x + textSize;
        float w = sideList ? listW : size.x * 0.45f;
        float y = pos.y + (sideList ? -b : textSize * 2.0f);
        auto wrap = [&](const std::string& s, float sz) {
            std::vector<std::string> lines;
            std::string line, word;
            for (std::size_t i = 0; i <= s.size(); ++i) {
                if (i == s.size() || s[i] == ' ') {
                    const std::string c = line.empty() ? word : line + ' ' + word;
                    if (!line.empty() && ui.textWidth(c, sz) > w - textSize)
                        lines.push_back(line), line = word;
                    else
                        line = c;
                    word.clear();
                } else {
                    word += s[i];
                }
            }
            if (!line.empty())
                lines.push_back(line);
            return lines;
        };
        std::vector<std::pair<std::string, glm::vec4>> rows; // text, colour (titles coloured, stage text plain)
        for (const QuestView& q : quests) {
            const glm::vec4 c = q.active ? kQuest : (q.failed ? glm::vec4(0.7f, 0.35f, 0.3f, 1.0f) : glm::vec4(0.55f, 0.55f, 0.5f, 1.0f));
            rows.push_back({(q.active ? std::string() : engine::tr(q.failed ? "map.quest_failed" : "map.quest_done") + " ") + engine::localize(q.title), c});
            if (q.active)
                for (const std::string& l : wrap(engine::localize(q.stage), textSize * 0.9f))
                    rows.push_back({"  " + l, kText});
            rows.push_back({std::string(), kText});
        }
        const float h = static_cast<float>(rows.size()) * textSize * 1.2f + textSize * 2.4f;
        const float maxH = sideList ? size.y + 2.0f * b : size.y * 0.6f;
        if (sideList)
            bezel(ui, {x, y}, {w, std::min(h, maxH)}, b * 0.5f);
        ui.rect({x - 8.0f, y - 8.0f}, {w + 16.0f, std::min(h, maxH) + 8.0f}, {0.04f, 0.05f, 0.04f, sideList ? 1.0f : 0.85f});
        ui.text(engine::tr("map.quests"), {x, y}, textSize * 1.1f, kText, true);
        y += textSize * 2.0f;
        const float bottom = (sideList ? pos.y - b : pos.y) + maxH - textSize;
        for (const auto& [t, c] : rows) {
            if (y > bottom)
                break;
            const bool title = c != kText;
            ui.text(t, {x, y}, title ? textSize : textSize * 0.9f, c, title);
            y += textSize * 1.2f;
        }
    }
}
