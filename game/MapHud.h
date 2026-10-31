#pragma once
#include <engine/Renderer.h>
#include <engine/Scene.h>
#include <engine/Ui2D.h>

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <string>
#include <vector>

// Map of the level, PDA style: a minimap in the corner that turns with the
// player, and the full map (M) with north up. The picture is the level's map
// image or the level rendered from above; points of interest are entities of
// type "map_marker".
class MapHud {
public:
    ~MapHud();
    void release(); // frees the GPU image; call while the GL context is current
    void init(engine::Renderer& renderer, const engine::Scene& scene);
    void drawMinimap(engine::Ui2D& ui, int width, int height, glm::vec3 player, glm::vec3 forward) const;
    void drawFullMap(engine::Ui2D& ui, int width, int height, glm::vec3 player, glm::vec3 forward) const;

private:
    struct Marker {
        std::string name;
        glm::vec2 position; // world X, Z
    };
    glm::vec2 uv(glm::vec2 worldXZ) const; // texture coordinates of a world point

    unsigned texture_ = 0;
    bool ownImage_ = false; // loaded file (row 0 = top) instead of a render (row 0 = bottom)
    glm::vec2 min_{0.0f};
    float size_ = 100.0f;
    std::vector<Marker> markers_;
};
