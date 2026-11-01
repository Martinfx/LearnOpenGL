#pragma once
#include <engine/Scene.h>
#include <engine/Weapon.h>

#include <filesystem>
#include <functional>
#include <map>
#include <string>
#include <vector>

// What the player carries besides health: rounds by ammo type (shared by every weapon
// of that type), medkits, and the weapon files in slot order (parallel to the game's
// weapons). Carried across level changes and stored in saved games.
struct Inventory {
    std::map<std::string, int> ammo;
    int medkits = 0;
    std::vector<std::filesystem::path> weaponFiles;

    // Points every weapon's reserve at the stock of its ammo type (endless without a type).
    void link(std::vector<engine::Weapon>& weapons);
    // Rounds the weapons start with (their start reserve), once per new weapon.
    void addStartingAmmo(const engine::Weapon& weapon) {
        if (!weapon.settings.ammoType.empty())
            ammo[weapon.settings.ammoType] += weapon.settings.startReserve;
    }
};

// Pickups: entities of type "pickup" with properties item = ammo | medkit | weapon,
// ammo_type, amount and weapon (a .weapon path relative to assets/). Touching one takes
// it: the entity disappears (alive = 0, saved) and fires OnPickup.
struct PickupResult {
    std::size_t entity = 0;
    std::string message;          // "Sebráno: 30 × 5.45x39"
    std::string sound;            // relative to assets/
    std::filesystem::path weapon; // a new weapon to add (absolute .weapon), empty otherwise
};
// Pickups within reach of the player's feet; `weaponOwned` tells whether a .weapon is already carried.
std::vector<PickupResult> collectPickups(engine::Scene& scene, glm::vec3 feet, Inventory& inventory,
                                         const std::vector<engine::Weapon>& weapons, std::size_t currentWeapon,
                                         const std::filesystem::path& assetDir);
