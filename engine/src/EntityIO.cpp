#include "engine/EntityIO.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace engine {

std::vector<IoName> entityOutputs(const std::string& type)
{
    std::vector<IoName> out = {
        {"OnStart", "When the level starts"},
        {"OnDamaged", "When a weapon or script hurts it"},
        {"OnDeath", "When its health reaches 0"},
    };
    if (type == "trigger") {
        out.insert(out.begin(), {{"OnTrigger", "When something allowed by Filter enters (with Once: only the first time)"},
                                 {"OnStartTouch", "Every time something enters the volume"},
                                 {"OnEndTouch", "Every time it leaves the volume"}});
    } else if (type == "relay") {
        out.insert(out.begin(), {"OnTrigger", "When it receives Trigger (while enabled)"});
    } else if (type == "pickup") {
        out.insert(out.begin(), {"OnPickup", "When the player takes it"});
    }
    return out;
}

std::vector<IoName> entityInputs(const std::string& type)
{
    std::vector<IoName> in;
    if (type == "light") {
        in = {{"TurnOn", "Switches the light on"}, {"TurnOff", "Switches it off"}, {"Toggle", "On <-> off"}};
    } else if (type == "relay" || type == "trigger") {
        in = {{"Trigger", "Fires its OnTrigger outputs now"}};
    }
    for (IoName n : std::initializer_list<IoName>{
             {"Enable", "Property enabled = 1 (lights shine, sounds play, triggers react)"},
             {"Disable", "Property enabled = 0"},
             {"Toggle", "Enable <-> Disable"},
             {"Kill", "Removes the entity for the rest of the game (no OnDeath)"},
             {"Damage", "Hurts it by Parameter (default 100)"},
             {"SetProperty", "Parameter key=value sets an entity property"},
             {"ShowMessage", "Shows Parameter (or the property message) on the player's screen"},
             {"PlaySound", "Plays the .sound in Parameter at the entity"},
         }) {
        bool dup = false;
        for (const IoName& have : in)
            dup = dup || std::string(have.name) == n.name;
        if (!dup)
            in.push_back(n);
    }
    return in;
}

TriggerFilter triggerFilter(const Entity& trigger)
{
    const auto it = trigger.properties.find("filter");
    if (it == trigger.properties.end())
        return TriggerFilter::Player;
    if (it->second == "npc")
        return TriggerFilter::Npc;
    if (it->second == "any")
        return TriggerFilter::Any;
    return TriggerFilter::Player;
}

std::vector<InteriorVolume> interiorVolumes(const Scene& scene)
{
    std::vector<InteriorVolume> out;
    for (const Entity& e : scene.entities) {
        if (e.type != "interior" || !e.alive || !entityEnabled(e))
            continue;
        auto number = [&e](const char* key, float fallback) {
            const auto it = e.properties.find(key);
            return it == e.properties.end() || it->second.empty() ? fallback : std::strtof(it->second.c_str(), nullptr);
        };
        InteriorVolume v;
        v.half = glm::max(e.size * 0.5f, glm::vec3(0.01f));
        v.center = e.position + glm::vec3(0.0f, v.half.y, 0.0f);
        v.yawDegrees = e.yawDegrees;
        v.fade = std::max(0.0f, number("fade", 0.5f));
        v.sky = std::clamp(number("sky", 0.15f), 0.0f, 1.0f);
        v.fog = std::clamp(number("fog", 0.2f), 0.0f, 1.0f);
        out.push_back(v);
    }
    return out;
}

float InteriorVolume::distance(glm::vec3 p) const
{
    // World -> box frame (the entity box is turned by -yaw about +Y, as water bodies).
    const float c = std::cos(glm::radians(yawDegrees)), s = std::sin(glm::radians(yawDegrees));
    const glm::vec3 d = p - center;
    const glm::vec3 local(c * d.x + s * d.z, d.y, -s * d.x + c * d.z);
    return glm::length(glm::max(glm::abs(local) - half, glm::vec3(0.0f)));
}

float interiorWeight(const std::vector<InteriorVolume>& volumes, glm::vec3 p)
{
    float best = 0.0f;
    for (const InteriorVolume& v : volumes) {
        const float d = v.distance(p);
        best = std::max(best, v.fade > 0.0f ? std::clamp(1.0f - d / v.fade, 0.0f, 1.0f) : (d <= 0.0f ? 1.0f : 0.0f));
    }
    return best;
}

bool entityEnabled(const Entity& entity)
{
    const auto it = entity.properties.find("enabled");
    return it == entity.properties.end() || it->second != "0";
}

std::vector<int> findEntitiesByName(const Scene& scene, const std::string& target)
{
    std::vector<int> out;
    for (std::size_t i = 0; i < scene.entities.size(); ++i)
        if (scene.entities[i].name == target)
            out.push_back(static_cast<int>(i));
    return out;
}

} // namespace engine
