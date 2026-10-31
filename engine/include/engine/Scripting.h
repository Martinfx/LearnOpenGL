#pragma once
#include "engine/Scene.h"
#include <functional>
#include <memory>
#include <string>

namespace engine {

class PhysicsWorld;
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

    float playerHealth() const;
    void setPlayerHealth(float health);
    int errorCount() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace engine
