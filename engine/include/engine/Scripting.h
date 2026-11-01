#pragma once
#include "engine/Scene.h"
#include <glm/vec3.hpp>
#include <functional>
#include <memory>
#include <string>

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

    // Navigation for self:move_to() and game.find_path(); null or not ready = straight lines.
    void setNavMesh(const NavMesh* nav);

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
