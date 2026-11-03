#pragma once
#include "engine/Scene.h"

#include <string>
#include <vector>

namespace engine {

// Entity I/O: the outputs an entity of a type fires and the inputs every entity
// understands (ScriptSystem delivers them; scripts add their own with on_input
// and self:fire_output). The editor offers these in its Outputs table.
struct IoName {
    const char* name;
    const char* help;
};

// Outputs fired by the engine for an entity of `type` (every type has OnStart,
// OnDamaged, OnDeath; triggers add touch outputs, relays OnTrigger...).
std::vector<IoName> entityOutputs(const std::string& type);
// Inputs an entity of `type` reacts to without a script.
std::vector<IoName> entityInputs(const std::string& type);

// Trigger volume settings (entity properties of type "trigger").
enum class TriggerFilter { Player, Npc, Any };
TriggerFilter triggerFilter(const Entity& trigger); // property "filter": player (default), npc, any
bool entityEnabled(const Entity& entity);           // property "enabled" is not "0"

// Interior volumes (entity type "interior"): inside, the sky light and the outdoor fog are
// dimmed (dark buildings where the flashlight matters), rain is muffled as under a roof and
// the room reverb applies. Properties: sky (sky light left, 0..1), fog (fog density left),
// fade (m outside the box over which it blends out), preset/amount (reverb, "none" = keep).
struct InteriorVolume {
    glm::vec3 center{0.0f};   // middle of the box
    glm::vec3 half{0.5f};     // half size along its own axes
    float yawDegrees = 0.0f;  // turned like the entity (0 = +X)
    float fade = 0.5f;
    float sky = 0.15f;
    float fog = 0.2f;
    // Distance from the box (0 inside), turned into its frame.
    float distance(glm::vec3 p) const;
};
std::vector<InteriorVolume> interiorVolumes(const Scene& scene);
// 0 outside every interior .. 1 inside one (blends over `fade` outside the box).
float interiorWeight(const std::vector<InteriorVolume>& volumes, glm::vec3 p);

// Post-process volumes (entity type "post_volume"): inside, the image looks different (a
// greenish basement, a washed-out radiation zone, a warm camp). Each property present overrides
// the level's value: exposure, saturation, contrast, temperature, green_magenta, tint (r g b),
// vignette, grain, bloom, chromatic_aberration, lens_dirt; fade (m outside the box over which it
// blends out), priority (higher wins where volumes overlap).
struct PostVolume {
    InteriorVolume box;               // center, half, yaw, fade (sky / fog unused)
    int priority = 0;
    std::vector<std::pair<std::string, std::string>> values; // key, value as in the entity
};
std::vector<PostVolume> postVolumes(const Scene& scene);
// The level's image settings blended toward the volumes around `p` (lowest priority first).
void applyPostVolumes(const std::vector<PostVolume>& volumes, glm::vec3 p, Environment& e);
// Keys a post volume may override, in the editor's order.
const std::vector<std::string>& postVolumeKeys();

// Entities named `target` (several may share a name), in scene order.
std::vector<int> findEntitiesByName(const Scene& scene, const std::string& target);

} // namespace engine
