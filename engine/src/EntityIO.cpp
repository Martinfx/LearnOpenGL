#include "engine/EntityIO.h"

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
