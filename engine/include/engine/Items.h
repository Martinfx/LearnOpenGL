#pragma once
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace engine {

// Price list and weights of what the player carries besides weapons (whose price, weight
// and wear are in their .weapon files): assets/items.cfg, editor Assets > Items.
//   money_start 500                         roubles of a new game
//   carry 30 50                             kg carried freely / at most (over the first: no
//                                           sprint and slower, near the second: a crawl)
//   medkit "Lékárnička" 250 0.4             name, price, weight
//   ammo "5.45x39" "Náboje 5,45×39" 3 0.012 type, name, price and weight per round
//   armor "jacket" "Kožená bunda" 1200 2.5 0.2 0.004
//                                           id, name, price, weight, protection (share of damage
//                                           taken off when new), wear (condition lost per hp it stops)
struct ItemDef {
    enum class Kind { Medkit, Ammo, Armor };
    Kind kind = Kind::Medkit;
    std::string id, name;
    int price = 0;
    float weight = 0.0f;
    float protection = 0.0f, wear = 0.0f; // armor
    bool operator==(const ItemDef&) const = default;
};

struct ItemCatalog {
    int moneyStart = 500;
    float carryFree = 30.0f, carryMax = 50.0f;
    std::vector<ItemDef> items;
    const ItemDef* find(ItemDef::Kind kind, std::string_view id = {}) const; // medkit: id ignored
    bool operator==(const ItemCatalog&) const = default;
};

ItemCatalog defaultItems();
bool loadItems(ItemCatalog& catalog, const std::filesystem::path& file, std::string* error = nullptr);
bool saveItems(const ItemCatalog& catalog, const std::filesystem::path& file, std::string* error = nullptr);

// Trade: what a trader asks (markup over the price, worn things cheaper) and pays the player.
int buyPrice(int price, float markup, float condition = 1.0f);
int sellPrice(int price, float rate, float condition = 1.0f);
// Repairing to new costs this share of the price per missing condition (e.g. 0.6 at 40 %: 0.6 x 0.4 x price).
int repairPrice(int price, float condition, float rate = 0.6f);

// Armor: hp actually taken from `damage`, and the condition after stopping the rest.
float armorAbsorb(const ItemDef& armor, float& condition, float damage);

// Movement under load: speed factor for the carried weight (1 up to carryFree, falling to 0.3 at
// carryMax and beyond) and whether sprinting is still possible.
float loadSpeedFactor(const ItemCatalog& catalog, float weight);
bool loadAllowsSprint(const ItemCatalog& catalog, float weight);

} // namespace engine
