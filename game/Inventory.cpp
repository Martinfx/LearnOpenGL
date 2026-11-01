#include "Inventory.h"

#include <engine/EntityIO.h>

#include <algorithm>
#include <cstdlib>

void Inventory::link(std::vector<engine::Weapon>& weapons)
{
    for (engine::Weapon& w : weapons)
        w.setReserve(w.settings.ammoType.empty() ? nullptr : &ammo[w.settings.ammoType]);
}

namespace {
std::string property(const engine::Entity& e, const char* key, const std::string& fallback = {})
{
    const auto it = e.properties.find(key);
    return it == e.properties.end() || it->second.empty() ? fallback : it->second;
}
} // namespace

std::vector<PickupResult> collectPickups(engine::Scene& scene, glm::vec3 feet, Inventory& inventory,
                                         const std::vector<engine::Weapon>& weapons, std::size_t currentWeapon,
                                         const std::filesystem::path& assetDir)
{
    std::vector<PickupResult> out;
    for (std::size_t i = 0; i < scene.entities.size(); ++i) {
        engine::Entity& e = scene.entities[i];
        if (e.type != "pickup" || !e.alive || !engine::entityEnabled(e))
            continue;
        const glm::vec3 d = e.position - feet;
        if (d.x * d.x + d.z * d.z > 1.1f * 1.1f || d.y < -0.6f || d.y > 1.6f)
            continue; // within ~1 m, between the knees and above the head
        const std::string item = property(e, "item", "ammo");
        PickupResult r;
        r.entity = i;
        if (item == "medkit") {
            const int n = std::max(1, std::atoi(property(e, "amount", "1").c_str()));
            inventory.medkits += n;
            r.message = n > 1 ? "Sebráno: " + std::to_string(n) + " × lékárnička" : "Sebráno: lékárnička";
            r.sound = property(e, "sound", "sounds/items/pickup_medkit.sound");
        } else if (item == "weapon") {
            const std::string file = property(e, "weapon");
            if (file.empty())
                continue;
            const std::filesystem::path path = (assetDir / std::filesystem::path(std::u8string(file.begin(), file.end()))).lexically_normal();
            const auto owned = std::find(inventory.weaponFiles.begin(), inventory.weaponFiles.end(), path);
            if (owned != inventory.weaponFiles.end()) { // carried already: take its rounds
                const engine::Weapon& w = weapons[static_cast<std::size_t>(owned - inventory.weaponFiles.begin())];
                if (w.settings.ammoType.empty())
                    continue; // nothing to take (knife): leave it lying
                inventory.ammo[w.settings.ammoType] += w.settings.startReserve;
                r.message = "Sebráno: " + std::to_string(w.settings.startReserve) + " × " + w.settings.ammoType;
                r.sound = property(e, "sound", "sounds/items/pickup_ammo.sound");
            } else {
                r.weapon = path;
                r.message = "Sebráno: " + property(e, "name", path.stem().string());
                r.sound = property(e, "sound", "sounds/items/pickup_weapon.sound");
            }
        } else { // ammo; without a type the rounds of the weapon in hand
            std::string type = property(e, "ammo_type");
            if (type.empty() && currentWeapon < weapons.size())
                type = weapons[currentWeapon].settings.ammoType;
            if (type.empty())
                continue;
            const int n = std::max(1, std::atoi(property(e, "amount", "30").c_str()));
            inventory.ammo[type] += n;
            r.message = "Sebráno: " + std::to_string(n) + " × " + type;
            r.sound = property(e, "sound", "sounds/items/pickup_ammo.sound");
        }
        e.alive = false;
        out.push_back(std::move(r));
    }
    return out;
}
