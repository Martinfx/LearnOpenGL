#pragma once
#include <engine/Scripting.h>

#include <glm/vec3.hpp>

#include <filesystem>
#include <map>
#include <string>
#include <vector>

// A saved game: which level, the player, and the state of every scripted entity
// (matched by its persistent ObjectId, so a save survives small level edits).
// Text file in the user directory's saves/ folder, written atomically (temporary
// file + rename) with a checksum, so a crash while saving never leaves a broken save.
struct SaveGame {
    static constexpr int kVersion = 1;

    std::filesystem::path level;  // .level file, relative to the game folder (or absolute outside it)
    std::string title;            // level title, for the load screen
    std::string created;          // local time "2026-11-01 14:05"
    double playTime = 0.0;        // seconds in the level (game.time())
    glm::vec3 feet{0.0f};
    float yaw = 0.0f, pitch = 0.0f;
    float health = 100.0f, dose = 0.0f;
    std::size_t weapon = 0;
    std::vector<int> ammo;
    int kills = 0;
    // Physics props (dynamic models) where they lie now.
    struct Prop {
        engine::ObjectId id = 0;
        glm::vec3 position{0.0f}, rotation{0.0f};
    };
    std::vector<Prop> props;
    bool flashlightOn = false;
    std::vector<std::filesystem::path> weaponFiles; // carried weapons, parallel to `ammo` (magazines)
    std::map<std::string, int> stock;               // rounds by ammo type
    int medkits = 0;
    float flashlightCharge = 1.0f;
    // Day/night cycle: time of day (< 0 = not saved, keep the level's), clock speed, cycle on.
    float timeOfDay = -1.0f;
    float dayMinutes = 0.0f;
    bool timeEnabled = false;
    float wetness = 0.0f;         // how wet the ground is (dries over minutes)
    std::string weather;          // weather kind now (Environment > Weather cycle), empty = none
    float weatherHold = 0.0f;     // s until the cycle changes it
    std::vector<engine::ScriptSystem::EntityState> entities;
    std::vector<engine::ScriptSystem::PendingInput> pendingInputs; // entity I/O waiting for its delay

    bool write(const std::filesystem::path& file, std::string* error = nullptr) const;
    bool read(const std::filesystem::path& file, std::string* error = nullptr);
};

// Save slots: "quick" (F5 / F9), "auto" (start of each level), "slot1".."slot3".
std::filesystem::path saveFile(const std::string& slot);
struct SaveInfo {
    std::string slot;
    std::filesystem::path file;
    std::string title, created;
    bool valid = false;
};
std::vector<SaveInfo> listSaves(); // newest first, only slots that exist
std::string slotLabel(const std::string& slot); // "Rychlé uložení", "Pozice 1"...
