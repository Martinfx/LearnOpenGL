#pragma once
#include <glm/vec3.hpp>
#include <filesystem>
#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace engine {

// Who is on whose side: factions (stalkers, bandits, military, mutants...), the relation of
// every pair (-100 hate .. 100 allies) and the player's standing. Stored as assets/factions.cfg,
// edited in the editor (Assets > Factions). NPCs pick their faction in Properties > Character.
//   player "stalker"                  the player's own faction
//   thresholds -30 30                 below = hostile, above = friendly, between = neutral
//   faction "stalker" "Stalkers" 0.45 0.7 0.35    id, display name, colour
//   relation "stalker" "bandit" -80   symmetric; a faction with itself defaults to 100
// The player's relation to a faction is relation(player faction, faction) + goodwill, which
// changes in play (shooting its members) and is kept in saved games.
struct Faction {
    std::string id;
    std::string name;
    glm::vec3 color{0.7f};
};

enum class Attitude { Hostile, Neutral, Friendly };
const char* attitudeName(Attitude a); // "hostile", "neutral", "friendly"

struct FactionTable {
    std::vector<Faction> factions;
    std::map<std::pair<std::string, std::string>, float> relations; // key: ids in sorted order
    std::string playerFaction = "stalker";
    float hostileBelow = -30.0f, friendlyAbove = 30.0f;
    std::map<std::string, float> goodwill; // the player's standing per faction (play state)

    const Faction* find(std::string_view id) const;
    // -100..100; a faction with itself 100 unless set; unknown pairs 0. "player" means the player.
    float relation(std::string_view a, std::string_view b) const;
    void setRelation(std::string_view a, std::string_view b, float value);
    bool hasRelation(std::string_view a, std::string_view b) const;
    Attitude attitude(std::string_view a, std::string_view b) const;
    Attitude attitudeOf(float relation) const;
    void changeGoodwill(std::string_view faction, float delta); // clamped so the relation stays in -100..100
};

// The built-in table (the same as the shipped assets/factions.cfg), used when there is no file.
FactionTable defaultFactions();
bool loadFactions(FactionTable& table, const std::filesystem::path& file, std::string* error = nullptr);
bool saveFactions(const FactionTable& table, const std::filesystem::path& file, std::string* error = nullptr);

} // namespace engine

namespace engine {
struct Entity;
// An entity's faction: its "faction" property, else by type ("enemy" -> bandit, "npc" -> stalker,
// "mutant" -> mutant), else "" (not a character: nobody's friend or foe).
std::string entityFaction(const Entity& e);
} // namespace engine
