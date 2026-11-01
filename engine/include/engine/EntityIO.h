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

// Entities named `target` (several may share a name), in scene order.
std::vector<int> findEntitiesByName(const Scene& scene, const std::string& target);

} // namespace engine
