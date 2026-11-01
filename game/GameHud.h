#pragma once
#include <engine/Ui2D.h>

#include <string>
#include <vector>

// Player HUD in the bottom right: health bar, weapon name and ammo (with the
// reload progress), and a red edge flash when hurt. The detector and dose panel
// in the bottom left is GameAnomalies::drawHud.
struct HudState {
    float health = 100.0f, maxHealth = 100.0f;
    std::string weapon;
    int ammo = 0, magazine = 0;
    bool melee = false;
    int medkits = 0;
    float reload = -1.0f; // 0..1 while reloading, else < 0
    float battery = -1.0f; // flashlight charge 0..1 (shown while on or not full), < 0 = no battery
    bool flashlightOn = false;
};

class GameHud {
public:
    void update(float dt, float health); // hurt flash follows health drops
    void notify(const std::string& text); // short message in the top centre ("Uloženo")
    // Subtitle line at the bottom centre (cl_subtitles); the same text again only
    // refreshes it. At most four lines, the oldest goes first.
    void caption(const std::string& text, float seconds = 2.5f);
    std::size_t captionCount() const { return captions_.size(); }
    void draw(engine::Ui2D& ui, int width, int height, const HudState& state) const;

private:
    float lastHealth_ = -1.0f;
    float hurt_ = 0.0f;
    std::string message_;
    float messageTime_ = 0.0f;
    struct Caption {
        std::string text;
        float time = 0.0f;
    };
    std::vector<Caption> captions_;
};
