#pragma once
#include "engine/Scene.h"
#include <glm/vec3.hpp>
#include <functional>
#include <memory>
#include <string>

namespace engine {

class PhysicsWorld;

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

    // game.play_sound(sound [, position] [, volume]): `sound` is a path relative to
    // the assets folder; position nil = not positioned (2D). The game plays it.
    using SoundFn = std::function<void(const std::string& sound, const glm::vec3* position, float volume)>;
    void setSoundHandler(SoundFn handler);

    float playerHealth() const;
    void setPlayerHealth(float health);
    int errorCount() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace engine
