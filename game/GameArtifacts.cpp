#include "UiScale.h"
#include "GameArtifacts.h"
#include "GameAudio.h"

#include <engine/Localization.h>
#include <engine/Lights.h>
#include <engine/Log.h>
#include <engine/Physics.h>
#include <engine/Ui2D.h>

#include <glm/geometric.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>

namespace {
std::filesystem::path fromUtf8(const std::string& s)
{
    return std::filesystem::path(std::u8string(s.begin(), s.end()));
}
} // namespace

void GameArtifacts::init(const std::filesystem::path& assetDir, GameAudio& audio)
{
    assetDir_ = assetDir;
    audio_ = &audio;
}

std::shared_ptr<const engine::Model> GameArtifacts::model(const std::string& relative)
{
    if (relative.empty())
        return nullptr;
    if (auto it = models_.find(relative); it != models_.end())
        return it->second;
    auto m = std::make_shared<engine::Model>();
    std::string error;
    if (!engine::loadModelFile(*m, assetDir_ / fromUtf8(relative), &error)) {
        LOG_WARN("artifacts", "%s\n", error.c_str());
        m.reset();
    }
    models_[relative] = m;
    return m;
}

void GameArtifacts::startLevel(const engine::AnomalyField& field, const engine::ItemCatalog& items, const engine::PhysicsWorld& physics,
                               bool grow)
{
    items_ = &items;
    lying_.clear();
    reveal_.clear();
    signal_ = 0.0f;
    // The models are a few kilobytes each: load them all now, not in the middle of play.
    for (const engine::ItemDef& d : items.items)
        if (d.kind == engine::ItemDef::Kind::Artifact)
            model(d.model);
    if (grow)
        this->grow(field, items, physics);
}

int GameArtifacts::grow(const engine::AnomalyField& field, const engine::ItemCatalog& items, const engine::PhysicsWorld& physics)
{
    items_ = &items;
    int grown = 0;
    const auto& states = field.anomalies();
    std::uniform_real_distribution<float> unit(0.0f, 1.0f);
    for (std::size_t i = 0; i < states.size(); ++i) {
        const auto& s = states[i];
        const auto kinds = engine::artifactsFor(items, engine::anomalyKindInfo(s.settings.kind).id);
        if (kinds.empty())
            continue;
        const int most = static_cast<int>(std::lround(s.settings.get("artifacts")));
        const float chance = s.settings.get("artifact_chance");
        int present = 0;
        for (const Lying& l : lying_)
            present += l.anomaly == static_cast<int>(i);
        for (int k = present; k < most; ++k) {
            if (unit(rng_) >= chance)
                continue;
            // Somewhere in the inner part of the field, resting on what is below.
            const float a = unit(rng_) * 6.2831853f, r = s.radius * (0.15f + 0.5f * std::sqrt(unit(rng_)));
            glm::vec3 p = s.center + glm::vec3(std::cos(a) * r, 0.0f, std::sin(a) * r);
            if (auto hit = physics.raycast(p + glm::vec3(0.0f, std::max(1.0f, s.height), 0.0f), {0.0f, -1.0f, 0.0f}, s.height + 3.0f))
                p = hit->point;
            const engine::ItemDef* d = kinds[std::min(kinds.size() - 1, static_cast<std::size_t>(unit(rng_) * static_cast<float>(kinds.size())))];
            lying_.push_back({d->id, p, static_cast<int>(i), unit(rng_) * 6.28f});
            ++grown;
        }
    }
    reveal_.resize(lying_.size(), 0.0f);
    return grown;
}

void GameArtifacts::setLying(std::vector<Lying> lying)
{
    lying_ = std::move(lying);
    reveal_.assign(lying_.size(), 0.0f);
}

std::string GameArtifacts::name(const std::string& id) const
{
    const engine::ItemDef* d = items_ ? items_->find(engine::ItemDef::Kind::Artifact, id) : nullptr;
    return d ? engine::localize(d->name) : id;
}

void GameArtifacts::update(float dt, glm::vec3 eye)
{
    reveal_.resize(lying_.size(), 0.0f);
    float nearest = 1e9f;
    for (std::size_t i = 0; i < lying_.size(); ++i) {
        const float d = glm::length(lying_[i].position - eye);
        nearest = std::min(nearest, d);
        const float target = d < kRevealRange ? 1.0f : 0.0f;
        reveal_[i] += (target - reveal_[i]) * std::min(1.0f, dt * 2.5f);
    }
    signal_ = nearest < kDetectRange ? 1.0f - nearest / kDetectRange : 0.0f;
    pingFlash_ = std::max(0.0f, pingFlash_ - dt * 5.0f);
    if (signal_ > 0.0f && audio_) {
        pingTimer_ -= dt;
        if (pingTimer_ <= 0.0f) {
            audio_->play2D(assetDir_ / "sounds/anomalies/artifact_ping.sound", 0.3f + 0.4f * signal_);
            pingFlash_ = 1.0f;
            pingTimer_ = 2.2f - 1.9f * std::pow(signal_, 0.7f);
        }
    } else {
        pingTimer_ = std::min(pingTimer_, 0.3f);
    }
}

void GameArtifacts::fill(engine::RenderView& view, double time) const
{
    if (!items_)
        return;
    for (std::size_t i = 0; i < lying_.size() && i < reveal_.size(); ++i) {
        const float shown = reveal_[i];
        if (shown < 0.02f)
            continue;
        const Lying& l = lying_[i];
        const engine::ItemDef* d = items_->find(engine::ItemDef::Kind::Artifact, l.id);
        if (!d)
            continue;
        const auto it = models_.find(d->model);
        const float t = static_cast<float>(time) + l.phase;
        // Hovers a hand above the ground, turning slowly; grows in as it is revealed.
        const glm::vec3 at = l.position + glm::vec3(0.0f, 0.12f + 0.04f * std::sin(t * 1.7f), 0.0f);
        if (it != models_.end() && it->second) {
            glm::mat4 m = glm::translate(glm::mat4(1.0f), at);
            m = glm::rotate(m, t * 0.8f, glm::vec3(0.0f, 1.0f, 0.0f));
            m = glm::rotate(m, 0.3f * std::sin(t * 0.6f), glm::vec3(1.0f, 0.0f, 0.0f));
            m = glm::scale(m, glm::vec3(0.4f + 0.6f * shown));
            view.extraModels.emplace_back(it->second.get(), m);
        }
        engine::LocalLight light;
        light.position = at + glm::vec3(0.0f, 0.08f, 0.0f);
        const float pulse = 0.8f + 0.2f * std::sin(t * 3.1f);
        light.color = glm::vec3(d->glow[0], d->glow[1], d->glow[2]) * (1.6f * shown * pulse);
        light.range = 2.5f;
        light.key = 0xA27F000000ull + i;
        view.lights.push_back(light);
    }
}

int GameArtifacts::pickable(glm::vec3 eye, glm::vec3 forward) const
{
    int best = -1;
    float bestDot = 0.5f;
    for (std::size_t i = 0; i < lying_.size() && i < reveal_.size(); ++i) {
        if (reveal_[i] < 0.5f)
            continue;
        const glm::vec3 to = lying_[i].position + glm::vec3(0.0f, 0.15f, 0.0f) - eye;
        const float d = glm::length(to);
        if (d > kPickRange + 0.9f) // the eye is above the ground
            continue;
        const float dot = glm::dot(to / std::max(d, 1e-3f), forward);
        if (dot > bestDot) {
            bestDot = dot;
            best = static_cast<int>(i);
        }
    }
    return best;
}

std::string GameArtifacts::take(int index)
{
    if (index < 0 || index >= static_cast<int>(lying_.size()))
        return {};
    std::string id = lying_[static_cast<std::size_t>(index)].id;
    lying_.erase(lying_.begin() + index);
    if (index < static_cast<int>(reveal_.size()))
        reveal_.erase(reveal_.begin() + index);
    return id;
}

void GameArtifacts::drawHud(engine::Ui2D& ui, int width, int height) const
{
    (void)width;
    // A row above the detector / radiation box: artifact LED and signal.
    const float H = static_cast<float>(height);
    const float margin = std::round(H * 0.025f);
    const float text = uiSize(H, 0.022f, 12.0f, 26.0f);
    const float barW = std::round(text * 7.0f), barH = std::max(4.0f, std::round(text * 0.35f));
    const glm::vec2 pos(margin, H - margin - text * 4.2f - text * 1.9f);
    const glm::vec2 size(barW + text * 4.6f, text * 1.7f);
    if (signal_ <= 0.0f && lying_.empty())
        return;
    ui.rect(pos, size, {0.06f, 0.065f, 0.05f, 0.75f});
    const float x = pos.x + text * 0.6f, y = pos.y + text * 0.35f;
    const glm::vec4 led = signal_ > 0.0f ? glm::mix(glm::vec4(0.05f, 0.15f, 0.2f, 1.0f), glm::vec4(0.4f, 0.9f, 1.0f, 1.0f), pingFlash_)
                                         : glm::vec4(0.15f, 0.15f, 0.13f, 1.0f);
    ui.rect({x, y + text * 0.2f}, {text * 0.6f, text * 0.6f}, led);
    ui.text("ART", {x + text * 1.0f, y}, text, signal_ > 0.0f ? glm::vec4(0.86f, 0.82f, 0.72f, 1.0f) : glm::vec4(0.55f, 0.55f, 0.5f, 1.0f), true);
    const glm::vec2 bar(x + text * 3.4f, y + text * 0.35f);
    ui.rect(bar, {barW, barH}, {0.15f, 0.15f, 0.13f, 0.9f});
    ui.rect(bar, {barW * signal_, barH}, {0.4f, 0.85f, 1.0f, 0.95f});
}
