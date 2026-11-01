#pragma once
#include <glm/vec3.hpp>
#include <functional>
#include <cstdint>
#include <optional>
#include <random>
#include <string>
#include <vector>

namespace engine {

struct Scene;
struct FactionTable;
class ScriptSystem;

// Simplified life of the Zone away from the player ("A-life"). NPC squads (characters sharing
// a Group, Properties > Character) are online near the player: drawn and run by their scripts.
// Beyond the offline radius they become records: hidden (Entity::offline), the squad moves
// between points of interest (entities of type "alife_point") at walking pace, waits there,
// and hostile squads that meet fight it out by chance (members die, gunfire is heard). Coming
// back online the members stand where the squad got to and walk on to its target.
//
// Point properties: `roles` (text: camp, patrol, lair... for scripts and the map), `factions`
// (comma list of faction ids that use it, empty = anyone), `capacity` (squads at once, default 1).
// Squad members with property `alife` = "roam" travel between points; other squads only switch
// online / offline where they stand.
struct ALifeSettings {
    float onlineRadius = 150.0f;  // squads within this of the player are online
    float offlineRadius = 170.0f; // and go offline beyond this (hysteresis)
    float travelSpeed = 1.4f;     // m/s of an offline squad
    float minWait = 60.0f, maxWait = 240.0f; // seconds a roaming squad stays at a point
    float fightRange = 40.0f;     // hostile offline squads this close fight
    float killChance = 0.08f;     // per second and living member of the other side
};

class ALife {
public:
    struct Point {
        int entity = -1;
        std::string name, roles;
        std::vector<std::string> factions; // empty = anyone
        glm::vec3 position{0.0f};
        int capacity = 1;
    };
    struct Squad {
        std::string name; // the group
        std::string faction;
        std::vector<int> members; // entity indices
        bool roams = false;
        bool online = true;
        glm::vec3 position{0.0f}; // centre (offline: where the record is)
        int target = -1;          // point it travels to / stays at, -1 = none
        bool arrived = true;
        float wait = 0.0f;        // seconds left at the point
        int living = 0;           // members alive (update())
        std::vector<glm::vec3> offsets; // offline: each member's place relative to `position`
    };
    // Something the game shows or plays: shots of an offline fight, a squad coming online.
    struct Event {
        enum Kind { Gunfire, Death, Online, Offline } kind = Gunfire;
        glm::vec3 position{0.0f};
        int squad = -1, entity = -1;
    };
    using Snap = std::function<std::optional<glm::vec3>(glm::vec3 near, float radius)>; // a walkable spot (navmesh)

    // Builds points and squads from the level (after ScriptSystem::start()).
    void start(Scene& scene, std::uint32_t seed = 1234);
    // Online / offline switching and the offline simulation; `scripts` moves, kills and
    // sends inputs (`GoTo` "x y z" to an online roaming squad, `Arrive` at its point).
    void update(Scene& scene, ScriptSystem& scripts, const FactionTable& factions, glm::vec3 player, float dt,
                const Snap& snap = {});
    std::vector<Event> takeEvents();

    const std::vector<Point>& points() const { return points_; }
    const std::vector<Squad>& squads() const { return squads_; }
    ALifeSettings settings;

    // Saved games: per roaming squad its record (matched by name).
    struct SquadState {
        std::string name;
        glm::vec3 position{0.0f};
        int target = -1;
        bool arrived = true;
        float wait = 0.0f;
    };
    std::vector<SquadState> save() const;
    void restore(const std::vector<SquadState>& states);

private:
    int pickTarget(const Squad& s);
    void goOnline(Scene& scene, ScriptSystem& scripts, Squad& s, const Snap& snap);
    void goOffline(Scene& scene, Squad& s);

    std::vector<Point> points_;
    std::vector<Squad> squads_;
    std::vector<Event> events_;
    std::mt19937 rng_;
    float tick_ = 0.0f;
};

} // namespace engine
