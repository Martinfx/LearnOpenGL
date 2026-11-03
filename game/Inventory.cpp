#include "Inventory.h"

#include <engine/Localization.h>
#include <engine/EntityIO.h>
#include <engine/Items.h>

#include <algorithm>
#include <cstdlib>

float carriedWeight(const Inventory& inventory, const std::vector<engine::Weapon>& weapons, const engine::ItemCatalog& items)
{
    float kg = 0.0f;
    for (const engine::Weapon& w : weapons)
        kg += std::max(0.0f, w.settings.weight);
    for (const auto& [type, rounds] : inventory.ammo)
        if (const engine::ItemDef* d = items.find(engine::ItemDef::Kind::Ammo, type))
            kg += d->weight * static_cast<float>(std::max(rounds, 0));
    if (const engine::ItemDef* d = items.find(engine::ItemDef::Kind::Medkit))
        kg += d->weight * static_cast<float>(std::max(inventory.medkits, 0));
    if (!inventory.armor.empty())
        if (const engine::ItemDef* d = items.find(engine::ItemDef::Kind::Armor, inventory.armor))
            kg += d->weight;
    for (const std::string& a : inventory.artifacts)
        if (const engine::ItemDef* d = items.find(engine::ItemDef::Kind::Artifact, a))
            kg += d->weight;
    return kg;
}

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
        if (item == "money") {
            const int n = std::max(1, std::atoi(property(e, "amount", "100").c_str()));
            inventory.money += n;
            r.message = engine::trf("pickup.money", std::to_string(n));
            r.sound = property(e, "sound", "sounds/items/pickup_ammo.sound");
        } else if (item == "medkit") {
            const int n = std::max(1, std::atoi(property(e, "amount", "1").c_str()));
            inventory.medkits += n;
            r.message = n > 1 ? engine::trf("pickup.medkits", std::to_string(n)) : engine::tr("pickup.medkit");
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
                r.message = engine::trf("pickup.count", std::to_string(w.settings.startReserve), w.settings.ammoType);
                r.sound = property(e, "sound", "sounds/items/pickup_ammo.sound");
            } else {
                r.weapon = path;
                r.message = engine::trf("pickup.item", engine::localize(property(e, "name", path.stem().string())));
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
            r.message = engine::trf("pickup.count", std::to_string(n), type);
            r.sound = property(e, "sound", "sounds/items/pickup_ammo.sound");
        }
        e.alive = false;
        out.push_back(std::move(r));
    }
    return out;
}
