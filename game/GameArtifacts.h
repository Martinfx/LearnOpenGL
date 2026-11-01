#pragma once
#include <engine/Anomaly.h>
#include <engine/Items.h>
#include <engine/Scene.h>
#include <engine/Renderer.h>

#include <filesystem>
#include <map>
#include <memory>
#include <random>
#include <string>
#include <vector>

namespace engine {
class PhysicsWorld;
class Ui2D;
}
class GameAudio;

// Artifacts: valuable things that grow inside anomaly fields (the kinds in assets/items.cfg name
// the anomaly kind they grow in; each anomaly says how many and how likely, Properties >
// Anomaly > Artifacts). They lie hidden until the player comes within a few metres, the
// detector pings faster and higher the closer one is. E picks one up; the first ones carried
// (the belt) work on the player. A storm grows new ones in the free places.
class GameArtifacts {
public:
    struct Lying {
        std::string id;
        glm::vec3 position{0.0f}; // where it rests (bottom)
        int anomaly = -1;         // index into AnomalyField::anomalies(), -1 = none (loaded, dropped)
        float phase = 0.0f;       // hover / spin offset
    };
    static constexpr float kRevealRange = 4.5f; // m: closer than this it shows
    static constexpr float kDetectRange = 18.0f; // m: the detector pings within this
    static constexpr float kPickRange = 1.8f;

    void init(const std::filesystem::path& assetDir, GameAudio& audio);
    // A new level: forgets what lay there; `grow` fills the anomalies (false when a save follows).
    void startLevel(const engine::AnomalyField& field, const engine::ItemCatalog& items, const engine::PhysicsWorld& physics, bool grow);
    // Fills the free places of every anomaly by its chance (level start, after each storm). Returns how many grew.
    int grow(const engine::AnomalyField& field, const engine::ItemCatalog& items, const engine::PhysicsWorld& physics);

    // Per frame: detector pings and the reveal fade.
    void update(float dt, glm::vec3 eye);
    void fill(engine::RenderView& view, double time) const;
    void drawHud(engine::Ui2D& ui, int width, int height) const;

    // The revealed artifact in reach the player looks at most directly (-1 = none).
    int pickable(glm::vec3 eye, glm::vec3 forward) const;
    std::string take(int index);
    void drop(const std::string& id, glm::vec3 position) { lying_.push_back({id, position, -1, 0.0f}); }

    const std::vector<Lying>& lying() const { return lying_; }
    void setLying(std::vector<Lying> lying); // loading a saved game
    std::string name(const std::string& id) const;

private:
    std::shared_ptr<const engine::Model> model(const std::string& relative);

    std::filesystem::path assetDir_;
    GameAudio* audio_ = nullptr;
    const engine::ItemCatalog* items_ = nullptr;
    std::vector<Lying> lying_;
    std::vector<float> reveal_; // 0..1 per lying artifact
    std::map<std::string, std::shared_ptr<const engine::Model>> models_;
    float signal_ = 0.0f;
    float pingTimer_ = 0.0f;
    float pingFlash_ = 0.0f;
    std::mt19937 rng_{4242};
};
