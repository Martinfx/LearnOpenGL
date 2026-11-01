#pragma once
#include <engine/Anomaly.h>
#include <engine/Particles.h>
#include <engine/Renderer.h>
#include <engine/Scene.h>

#include <filesystem>
#include <map>
#include <memory>
#include <random>
#include <vector>

namespace engine {
class PhysicsWorld;
class PlayerController;
class ScriptSystem;
class Ui2D;
}
class GameAudio;

// The level's anomalies in the game: runs engine::AnomalyField on the player,
// characters and thrown bolts, and turns its events into particles, sounds and
// screen warps. Also the player's detector (beeps faster near an anomaly), the
// Geiger counter (clicks with the dose rate) and the radiation dose.
class GameAnomalies {
public:
    void init(const std::filesystem::path& assetDir, GameAudio& audio);
    void startLevel(const engine::Scene& scene);

    // Throws a bolt from the eye (key G): it sets anomalies off and shows where they are.
    void throwBolt(glm::vec3 eye, glm::vec3 forward, glm::vec3 playerVelocity);

    // One fixed step: forces and damage on the player and characters, bolts, dose.
    void fixedUpdate(float dt, engine::Scene& scene, const engine::PhysicsWorld& physics, engine::PlayerController& player,
                     engine::ScriptSystem& scripts);
    // Per frame: particles, loop volumes, detector beeps, Geiger clicks.
    void update(float dt, const engine::PhysicsWorld& physics, glm::vec3 eye);
    void fill(engine::RenderView& view, const glm::mat4& viewMatrix) const;
    void drawHud(engine::Ui2D& ui, int width, int height) const;

    float dose() const { return dose_; }
    float doseRate() const { return doseRate_; }
    void resetDose() { dose_ = 0.0f; }
    void setDose(float rad) { dose_ = rad; } // loading a saved game
    bool empty() const { return field_.anomalies().empty(); }

private:
    struct Bolt {
        glm::vec3 position{0.0f}, velocity{0.0f};
        float age = 0.0f;
        float spin = 0.0f;
        bool resting = false;
        bool dead = false;
    };
    std::shared_ptr<const engine::ParticleEffect> effect(const std::string& relative);
    std::filesystem::path asset(const std::string& relative) const;
    void stepBolt(Bolt& b, float dt, const engine::PhysicsWorld& physics);
    void handleEvent(const engine::AnomalyEvent& ev);

    std::filesystem::path assetDir_;
    GameAudio* audio_ = nullptr;
    engine::AnomalyField field_;
    engine::ParticleSystem particles_;
    std::map<std::string, std::shared_ptr<const engine::ParticleEffect>> effects_;
    struct Live {
        engine::ParticleSystem::Handle idle = 0;
        engine::ParticleSystem::Handle burst = 0; // vent column (stopped at End)
        std::uint32_t loop = 0;                   // audio handle
        float visible = 1.0f;                     // 0 when a wall is between the camera and it (distortion)
    };
    std::vector<Live> live_;
    std::vector<Bolt> bolts_;
    std::vector<engine::AnomalyBody> bodies_;
    std::vector<int> bodyEntity_;          // per body: entity index, -1 player, -2 - i bolt i
    std::vector<engine::AnomalyInfluence> influences_;
    std::vector<engine::AnomalyEvent> events_;
    std::map<int, float> pendingDamage_;   // characters: small damage collected until it is worth a hit
    // Player state
    float dose_ = 0.0f;       // rad
    float doseRate_ = 0.0f;   // rad/s where the player is
    float signal_ = 0.0f;     // detector 0..1
    float beepTimer_ = 0.0f;
    float beepFlash_ = 0.0f;  // HUD LED
    float clickFlash_ = 0.0f;
    float hurtTimer_ = 0.0f;
    float healthDrain_ = 0.0f; // radiation damage carried between steps (less than 1 hp)
    glm::vec3 playerFeet_{0.0f};
    std::mt19937 rng_{777};
};
