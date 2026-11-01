#pragma once
#include <engine/Lights.h>
#include <engine/Scene.h>

#include <glm/vec3.hpp>

namespace engine {
class FpsCamera;
struct RenderView;
} // namespace engine

// The player's flashlight: a spot light just below and right of the eye that
// trails the view a little, with the reflector pattern and a shadow. Settings
// come from the level (PlayerSettings::flashlight*, edited in the editor).
// An optional battery drains while it is on, flickers when nearly empty and
// recharges while it is off.
class Flashlight {
public:
    void reset(const engine::PlayerSettings& settings); // level start: off, full battery
    // Returns false (and does nothing) when the level gives no flashlight or the battery is empty.
    bool toggle();
    bool on() const { return on_; }
    void setOn(bool on) { on_ = on && settings_.flashlight; }
    void update(float dt, double time, const engine::FpsCamera& camera);
    void fill(engine::RenderView& view) const;
    bool hasBattery() const { return settings_.flashlightBattery > 0.0f; }
    float charge() const { return charge_; }       // 0..1
    void setCharge(float charge);
    bool justEmptied();                            // the battery ran out since the last call

private:
    engine::PlayerSettings settings_;
    bool on_ = false;
    bool emptied_ = false;
    float charge_ = 1.0f;
    float flicker_ = 1.0f;
    glm::vec3 position_{0.0f};
    glm::vec3 direction_{0.0f, 0.0f, -1.0f};
    bool placed_ = false;
};

// Flashlights of NPCs: characters with the property flashlight = 1 light a spot
// from their eyes along their facing (alive ones only).
void addCharacterFlashlights(const engine::Scene& scene, engine::RenderView& view);
