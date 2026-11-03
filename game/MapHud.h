#pragma once
#include <engine/MapBake.h>
#include <engine/Renderer.h>
#include <engine/Scene.h>
#include <engine/Ui2D.h>

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <filesystem>
#include <future>
#include <memory>
#include <string>
#include <vector>

// Map of the level, PDA style: a minimap in the corner that turns with the player,
// and the full map (M) with north up that zooms (wheel) and pans (mouse). The picture
// is the level rendered from above (engine/MapBake.h, baked on the first load and
// cached) or the level's own map image; points of interest are entities of type
// "map_marker", anomaly fields and the current quest targets. Look: Level > Map in
// the editor (MapSettings).
// Quests for the PDA: the list beside the map and the current stage's target on both maps.
struct QuestView {
    std::string title, stage, description;
    bool active = true;          // false = done or failed (listed greyed out)
    bool failed = false;
    std::vector<glm::vec2> targets; // world X, Z of the stage's target entities
};

class MapHud {
public:
    ~MapHud();
    void release(); // frees the GPU image; call while the GL context is current
    // Starts loading or baking the map; then call step() until it returns 1 (loading screen).
    void begin(engine::Renderer& renderer, const engine::Scene& scene, const std::filesystem::path& levelFile);
    float step(engine::Renderer& renderer, const engine::Scene& scene, double budgetMs = 40.0);
    bool ready() const { return texture_ != 0; }

    // Full map control while it is open: mouse movement pans, the wheel zooms,
    // `recenter` jumps back to the player. Call every frame the map is open.
    void control(glm::vec2 mouseDelta, float wheel, bool recenter, glm::vec3 player);
    void opened(glm::vec3 player); // the map was just opened: centred on the player
    void setZoom(float zoom) { zoom_ = zoom; }           // test hooks
    void setCenter(glm::vec2 worldXZ) { center_ = worldXZ; }

    void drawMinimap(engine::Ui2D& ui, int width, int height, glm::vec3 player, glm::vec3 forward,
                     const std::vector<QuestView>& quests = {}) const;
    void drawFullMap(engine::Ui2D& ui, int width, int height, glm::vec3 player, glm::vec3 forward,
                     const std::vector<QuestView>& quests = {}) const;

private:
    struct Marker {
        std::string name;
        glm::vec2 position; // world X, Z
    };
    struct Anomaly {
        glm::vec2 position;
        float radius;
        glm::vec4 color;
    };
    glm::vec2 uv(glm::vec2 worldXZ) const; // texture coordinates of a world point
    void finishBake();

    unsigned texture_ = 0;
    engine::MapArea area_;
    engine::MapSettings settings_;
    std::vector<Marker> markers_;
    std::vector<Anomaly> anomalies_;
    // Baking (first load of a level or after it changed).
    std::unique_ptr<engine::MapBaker> baker_;
    std::filesystem::path cacheFile_;
    std::shared_ptr<std::future<engine::CookedTexture>> cooking_;
    // Full map view.
    float zoom_ = 1.0f;              // 1 = the whole area fits
    glm::vec2 center_{0.0f};         // world X, Z in the middle of the screen
    mutable glm::vec2 screenSize_{1.0f}; // pixels of the map screen at the last draw (for panning)
};
