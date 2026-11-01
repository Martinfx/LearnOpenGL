#include "GameHud.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

void GameHud::update(float dt, float health)
{
    if (lastHealth_ >= 0.0f && health < lastHealth_ - 0.01f)
        hurt_ = std::min(1.0f, hurt_ + 0.35f + (lastHealth_ - health) * 0.02f);
    lastHealth_ = health;
    hurt_ = std::max(0.0f, hurt_ - dt * 1.5f);
    messageTime_ = std::max(0.0f, messageTime_ - dt);
}

void GameHud::notify(const std::string& text)
{
    message_ = text;
    messageTime_ = 2.5f;
}

void GameHud::draw(engine::Ui2D& ui, int width, int height, const HudState& s) const
{
    const float W = static_cast<float>(width), H = static_cast<float>(height);
    // Hurt: red at the screen edges, fading.
    if (hurt_ > 0.0f) {
        const float edge = H * 0.12f;
        const glm::vec4 red(0.6f, 0.03f, 0.02f, 0.45f * hurt_);
        for (int i = 0; i < 4; ++i) { // stacked strips: a cheap gradient
            const float k = static_cast<float>(i) / 4.0f, e = edge * (1.0f - k);
            const glm::vec4 c = red * glm::vec4(1, 1, 1, 0.5f);
            ui.rect({0, 0}, {W, e}, c);
            ui.rect({0, H - e}, {W, e}, c);
            ui.rect({0, e}, {e, H - 2 * e}, c);
            ui.rect({W - e, e}, {e, H - 2 * e}, c);
        }
    }

    const float margin = std::round(H * 0.025f);
    const float text = std::round(std::clamp(H * 0.022f, 12.0f, 26.0f));
    if (messageTime_ > 0.0f) { // fades out in its last half second
        const float a = std::min(1.0f, messageTime_ * 2.0f);
        const float tw = ui.textWidth(message_, text, true);
        const glm::vec2 p(std::round((W - tw) * 0.5f), std::round(H * 0.12f));
        ui.rect(p - glm::vec2(text * 0.8f, text * 0.4f), glm::vec2(tw + text * 1.6f, text * 1.8f), {0.06f, 0.065f, 0.05f, 0.75f * a});
        ui.text(message_, p, text, {0.93f, 0.62f, 0.2f, a}, true);
    }
    const glm::vec4 frame(0.06f, 0.065f, 0.05f, 0.75f), bone(0.86f, 0.82f, 0.72f, 1.0f), dim(0.55f, 0.55f, 0.5f, 1.0f);
    const float panelW = std::round(text * 12.0f), panelH = std::round(text * 4.2f);
    const glm::vec2 pos(W - margin - panelW, H - margin - panelH);
    ui.rect(pos, {panelW, panelH}, frame);
    const float x = pos.x + text * 0.6f, innerW = panelW - text * 1.2f;

    // Flashlight battery: a thin strip above the panel while the light is on or recharging.
    if (s.battery >= 0.0f && (s.flashlightOn || s.battery < 0.999f)) {
        const float h = std::round(text * 1.3f);
        const glm::vec2 strip(pos.x, pos.y - h - text * 0.3f);
        ui.rect(strip, {panelW, h}, frame);
        const float small = std::round(text * 0.75f);
        ui.text("BATERKA", {x, strip.y + (h - small) * 0.5f}, small, s.flashlightOn ? bone : dim, true);
        const float labelW = ui.textWidth("BATERKA", small, true) + text * 0.6f;
        const glm::vec2 bar(x + labelW, strip.y + h * 0.5f - 2.5f);
        const float barW = innerW - labelW;
        const float f = std::clamp(s.battery, 0.0f, 1.0f);
        ui.rect(bar, {barW, 5.0f}, {0.15f, 0.15f, 0.13f, 0.9f});
        ui.rect(bar, {barW * f, 5.0f}, f > 0.15f ? glm::vec4(0.93f, 0.82f, 0.45f, 0.95f) : glm::vec4(0.85f, 0.3f, 0.15f, 0.95f));
    }
    // Health: green-grey bar turning red when low, the number on the right.
    {
        const float y = pos.y + text * 0.5f;
        const float f = s.maxHealth > 0.0f ? std::clamp(s.health / s.maxHealth, 0.0f, 1.0f) : 0.0f;
        const glm::vec4 good(0.55f, 0.68f, 0.38f, 0.95f), bad(0.85f, 0.18f, 0.12f, 0.95f);
        const glm::vec4 color = f > 0.35f ? good : glm::mix(bad, good, f / 0.35f);
        char hp[16];
        std::snprintf(hp, sizeof hp, "%.0f", std::max(0.0f, s.health));
        const float numW = text * 2.4f;
        ui.text("+", {x, y}, text, f > 0.35f ? bone : bad, true);
        const glm::vec2 bar(x + text * 1.0f, y + text * 0.3f);
        const float barW = innerW - text * 1.0f - numW, barH = std::max(5.0f, std::round(text * 0.45f));
        ui.rect(bar, {barW, barH}, {0.15f, 0.15f, 0.13f, 0.9f});
        ui.rect(bar, {barW * f, barH}, color);
        ui.text(hp, {x + innerW - ui.textWidth(hp, text, true), y}, text, bone, true);
    }
    // Weapon and ammo: "AK-74   23 / 30", reload progress under it.
    {
        const float y = pos.y + text * 2.2f;
        ui.text(s.weapon, {x, y + text * 0.15f}, std::round(text * 0.85f), dim, true);
        if (!s.melee) {
            char mag[16], cur[16];
            std::snprintf(cur, sizeof cur, "%d", s.ammo);
            std::snprintf(mag, sizeof mag, " / %d", s.magazine);
            const float big = std::round(text * 1.35f);
            const float magW = ui.textWidth(mag, text, true);
            const bool empty = s.ammo == 0;
            ui.text(mag, {x + innerW - magW, y + (big - text) * 0.8f}, text, dim, true);
            ui.text(cur, {x + innerW - magW - ui.textWidth(cur, big, true), y - text * 0.15f}, big,
                    empty ? glm::vec4(0.9f, 0.3f, 0.2f, 1.0f) : bone, true);
            if (s.reload >= 0.0f) {
                const glm::vec2 bar(x, pos.y + panelH - text * 0.45f);
                ui.rect(bar, {innerW, 3.0f}, {0.15f, 0.15f, 0.13f, 0.9f});
                ui.rect(bar, {innerW * std::clamp(s.reload, 0.0f, 1.0f), 3.0f}, {0.93f, 0.62f, 0.2f, 1.0f});
            }
        }
    }
}
