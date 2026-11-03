#pragma once
#include <glm/vec3.hpp>

#include <filesystem>
#include <memory>
#include <string>

namespace engine {

struct Entity;
struct Model;
struct Scene;

// A ladder: an entity of type "ladder". Its box is the ladder itself (position = bottom
// centre, size = width x height x thickness, 1 unit = 1 m); yaw is the direction the
// climber faces while on it (towards the wall). The player grabs it from the front
// (the side opposite to the facing) or from the top, climbs with forward/back along
// the view (looking down + forward = down), steps off at the top onto what is behind
// it, Jump lets go backwards, Crouch slides down.
// Properties (editor "Ladder" section):
//   style     "metal" | "wood" | "none" (no own mesh: the ladder is part of a model, e.g. a fire escape)
//   speed     climbing speed, m/s (default 2)
//   rung      rung spacing, m (default 0.3); a climbing sound plays every second rung
//   material  engine .material relative to assets/ (empty = the style's default)
//   sound     .sound relative to assets/ (empty = the style's default)
struct Ladder {
    const Entity* entity = nullptr;
    glm::vec3 base{0.0f};       // bottom centre (on the rungs' middle plane)
    glm::vec3 forward{1.0f, 0.0f, 0.0f}; // the climber faces this way (into the ladder)
    glm::vec3 right{0.0f, 0.0f, 1.0f};
    float width = 0.5f, height = 3.0f, thickness = 0.08f;
    float speed = 2.0f;
    float rung = 0.3f;
    std::string style = "metal";
    std::string sound;          // relative to assets/

    float top() const { return base.y + height; }
    // p relative to the ladder: x along `right`, y up from the bottom, z along `forward`
    // (negative = in front of the ladder, where the climber hangs).
    glm::vec3 local(glm::vec3 p) const;
    glm::vec3 world(glm::vec3 local) const;
    // Feet within reach: in front of the ladder (up to `reach` m), beside it no farther than
    // a hand, from just below the bottom to just above the top.
    bool reachable(glm::vec3 feet, float reach) const;
};

Ladder ladderOf(const Entity& e);
// The nearest live, enabled ladder the feet can reach, or none.
bool ladderAt(const Scene& scene, glm::vec3 feet, float reach, Ladder* out = nullptr);

// Gives a ladder entity its procedural mesh (two rails and rungs for its size and style),
// or drops it for style "none". Cheap when nothing changed (call it after edits or each
// frame in the editor). Returns true when the model changed.
bool ensureLadderModel(Entity& e, const std::filesystem::path& assetDir);
// Builds the mesh (model space: width on X, height on Y from 0, thickness on Z, rungs
// facing -Z). Exposed for tests.
std::shared_ptr<Model> buildLadderModel(float width, float height, float thickness, float rung, const std::string& style);

} // namespace engine
