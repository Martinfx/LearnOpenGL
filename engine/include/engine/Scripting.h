#pragma once
#include "engine/Scene.h"
#include <glm/vec3.hpp>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace engine {

class PhysicsWorld;
class NavMesh;

// Result of compiling a script without running it (editor syntax check).
struct ScriptCheck {
    bool ok = true;
    int line = 0;            // 1-based line of the first error, 0 if unknown
    std::string message;     // Lua's message without the "chunk:line:" prefix
};
ScriptCheck checkScriptSyntax(const std::string& source, const std::string& chunkName = "script");
class PlayerController;

// Runs the Lua scripts attached to scene entities (one isolated environment
// per entity). See assets/scripts/API.md for the functions scripts can call.
class ScriptSystem {
public:
    using LogFn = std::function<void(const std::string&)>;

    // `scene` is the runtime copy of the level; scripts move its entities.
    ScriptSystem(Scene& scene, PhysicsWorld& physics, PlayerController& player, LogFn log = {});
    ~ScriptSystem();
    ScriptSystem(const ScriptSystem&) = delete;
    ScriptSystem& operator=(const ScriptSystem&) = delete;

    // Loads every entity script and calls its on_start(self). Script errors are
    // logged and disable only that entity's script; returns the error count.
    int start();

    // Calls on_update(self, dt) and moves character entities. Call once per fixed step.
    void fixedUpdate(float dt);

    // Applies weapon damage: calls on_damage(self, amount); at 0 health the
    // entity dies (on_death(self), no longer drawn or simulated).
    void damageEntity(int entityIndex, float amount);

    // Outside forces on an entity for the next fixedUpdate (anomalies): a one-off
    // velocity change, an extra velocity (pull, lift) and 0..1 of its walking speed taken away.
    void pushEntity(int entityIndex, glm::vec3 impulse, glm::vec3 drift = glm::vec3(0.0f), float slow = 0.0f);
    void teleportEntity(int entityIndex, glm::vec3 feet);

    // game.play_sound(sound [, position] [, volume]): `sound` is a path relative to
    // the assets folder; position nil = not positioned (2D). The game plays it.
    using SoundFn = std::function<void(const std::string& sound, const glm::vec3* position, float volume)>;
    void setSoundHandler(SoundFn handler);

    // Text for the player's screen (ShowMessage input, game.message()).
    using MessageFn = std::function<void(const std::string& text)>;
    void setMessageHandler(MessageFn handler);
    // game.duck_ambience(gain, seconds): the ambience steps back before a scripted event.
    using DuckFn = std::function<void(float gain, float seconds)>;
    void setDuckHandler(DuckFn handler);

    // Entity I/O (engine/EntityIO.h): fires `output` of an entity, i.e. queues the
    // inputs of its matching connections. `activator` = entity index, -1 = the player.
    void fireOutput(int entityIndex, const std::string& output, int activator = -1);
    // Sends an input to every entity named `target` after `delay` seconds.
    void sendInput(const std::string& target, const std::string& input, const std::string& parameter = {}, float delay = 0.0f,
                   int activator = -1, int caller = -1);
    int pendingInputs() const;

    // Navigation for self:move_to() and game.find_path(); null or not ready = straight lines.
    void setNavMesh(const NavMesh* nav);

    // Saved games: per entity (matched by ObjectId) whether it lives, its health, place,
    // facing and what its script's on_save(self) returned (a Lua table written as a
    // one-line Lua literal). restore() runs after start(): sets them back and calls
    // on_load(self, data).
    struct EntityState {
        ObjectId id = 0;
        bool alive = true;
        float health = 100.0f;
        glm::vec3 position{0.0f};
        float yaw = 0.0f;
        std::string data; // Lua literal, "" = nothing
        // Entity I/O: properties changed by inputs (all of them, when changed), whether
        // something stands in it (triggers) and which `once` outputs already fired.
        bool propertiesChanged = false;
        std::map<std::string, std::string> properties;
        bool touching = false;
        std::vector<int> firedOutputs;
    };
    // Inputs waiting for their delay (saved with the game).
    struct PendingInput {
        float delay = 0.0f; // seconds left
        std::string target, input, parameter;
        ObjectId caller = 0, activator = 0; // 0 = none / the player
    };
    std::vector<PendingInput> savePendingInputs() const;
    void restorePendingInputs(const std::vector<PendingInput>& pending);
    std::vector<EntityState> saveEntities();
    void restoreEntities(const std::vector<EntityState>& states);
    double time() const; // seconds of game time (game.time())
    void setTime(double seconds);

    float playerHealth() const;
    // Level file a script asked for with game.change_level() (empty = none); cleared by the call.
    std::string takeLevelChange();
    void setPlayerHealth(float health);
    int errorCount() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace engine
