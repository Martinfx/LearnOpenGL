#pragma once
#include <engine/Ui2D.h>

#include <string>

// Player HUD in the bottom right: health bar, weapon name and ammo (with the
// reload progress), and a red edge flash when hurt. The detector and dose panel
// in the bottom left is GameAnomalies::drawHud.
struct HudState {
    float health = 100.0f, maxHealth = 100.0f;
    std::string weapon;
    int ammo = 0, magazine = 0;
    bool melee = false;
    float reload = -1.0f; // 0..1 while reloading, else < 0
};

class GameHud {
public:
    void update(float dt, float health); // hurt flash follows health drops
    void draw(engine::Ui2D& ui, int width, int height, const HudState& state) const;

private:
    float lastHealth_ = -1.0f;
    float hurt_ = 0.0f;
};
