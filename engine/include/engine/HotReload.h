#pragma once
#include <filesystem>
#include <string>
#include <vector>

namespace engine {

struct Scene;
class PhysicsWorld;
class AudioSystem;
class ScriptSystem;

// What a hot reload may touch; null parts are skipped (the editor has no scripts).
struct HotReloadTargets {
    Scene* scene = nullptr;
    PhysicsWorld* physics = nullptr; // static collision of reloaded models
    AudioSystem* audio = nullptr;
    ScriptSystem* scripts = nullptr;
};

// Reloads the changed files (engine/FileWatcher.h) that are in use: textures and
// materials (in place, everything using them updates), glTF models of the scene
// (and their collision), particle effects, sounds and Lua scripts. Weapons are the
// game's (it knows the inventory). Returns one line per reloaded file for the log.
std::vector<std::string> hotReload(const std::vector<std::filesystem::path>& changed, const HotReloadTargets& targets);

} // namespace engine
