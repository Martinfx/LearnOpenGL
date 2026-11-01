#include "engine/Items.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace engine {

const ItemDef* ItemCatalog::find(ItemDef::Kind kind, std::string_view id) const
{
    for (const ItemDef& i : items)
        if (i.kind == kind && (kind == ItemDef::Kind::Medkit || i.id == id))
            return &i;
    return nullptr;
}

ItemCatalog defaultItems()
{
    ItemCatalog c;
    c.items.push_back({ItemDef::Kind::Medkit, "medkit", "Lékárnička", 250, 0.4f});
    c.items.push_back({ItemDef::Kind::Ammo, "5.45x39", "Náboje 5,45×39", 3, 0.012f});
    c.items.push_back({ItemDef::Kind::Ammo, "9x18", "Náboje 9×18", 2, 0.011f});
    c.items.push_back({ItemDef::Kind::Armor, "jacket", "Kožená bunda", 1200, 2.5f, 0.2f, 0.004f});
    return c;
}

bool loadItems(ItemCatalog& catalog, const std::filesystem::path& file, std::string* error)
{
    std::ifstream in(file);
    if (!in) {
        if (error)
            *error = "cannot open " + file.string();
        return false;
    }
    ItemCatalog c;
    std::string line;
    int lineNo = 0;
    auto fail = [&](const std::string& what) {
        if (error)
            *error = file.filename().string() + ":" + std::to_string(lineNo) + ": " + what;
        return false;
    };
    while (std::getline(in, line)) {
        ++lineNo;
        if (const std::size_t hash = line.find('#'); hash != std::string::npos && line.find('"') > hash)
            line.erase(hash);
        std::istringstream s(line);
        std::string key;
        if (!(s >> key))
            continue;
        ItemDef d;
        if (key == "money_start") {
            if (!(s >> c.moneyStart))
                return fail("money_start needs a number");
        } else if (key == "carry") {
            if (!(s >> c.carryFree >> c.carryMax))
                return fail("carry needs two weights");
        } else if (key == "medkit") {
            d.kind = ItemDef::Kind::Medkit;
            d.id = "medkit";
            if (!(s >> std::quoted(d.name) >> d.price >> d.weight))
                return fail("medkit needs name price weight");
            c.items.push_back(d);
        } else if (key == "ammo") {
            d.kind = ItemDef::Kind::Ammo;
            if (!(s >> std::quoted(d.id) >> std::quoted(d.name) >> d.price >> d.weight))
                return fail("ammo needs type name price weight");
            c.items.push_back(d);
        } else if (key == "armor") {
            d.kind = ItemDef::Kind::Armor;
            if (!(s >> std::quoted(d.id) >> std::quoted(d.name) >> d.price >> d.weight >> d.protection >> d.wear))
                return fail("armor needs id name price weight protection wear");
            c.items.push_back(d);
        } else {
            return fail("unknown key " + key);
        }
    }
    catalog = std::move(c);
    return true;
}

bool saveItems(const ItemCatalog& c, const std::filesystem::path& file, std::string* error)
{
    std::ofstream out(file);
    if (!out) {
        if (error)
            *error = "cannot write " + file.string();
        return false;
    }
    out << "# Price list and weights (editor: Assets > Items). Weapons keep theirs in the .weapon files.\n"
        << "money_start " << c.moneyStart << '\n'
        << "carry " << c.carryFree << ' ' << c.carryMax << '\n';
    for (const ItemDef& d : c.items) {
        switch (d.kind) {
        case ItemDef::Kind::Medkit: out << "medkit " << std::quoted(d.name) << ' ' << d.price << ' ' << d.weight << '\n'; break;
        case ItemDef::Kind::Ammo:
            out << "ammo " << std::quoted(d.id) << ' ' << std::quoted(d.name) << ' ' << d.price << ' ' << d.weight << '\n';
            break;
        case ItemDef::Kind::Armor:
            out << "armor " << std::quoted(d.id) << ' ' << std::quoted(d.name) << ' ' << d.price << ' ' << d.weight << ' ' << d.protection
                << ' ' << d.wear << '\n';
            break;
        }
    }
    return static_cast<bool>(out);
}

int buyPrice(int price, float markup, float condition)
{
    const float c = std::clamp(condition, 0.0f, 1.0f);
    return std::max(1, static_cast<int>(std::lround(static_cast<float>(price) * std::max(0.0f, markup) * (0.25f + 0.75f * c))));
}

int sellPrice(int price, float rate, float condition)
{
    const float c = std::clamp(condition, 0.0f, 1.0f);
    return std::max(0, static_cast<int>(std::floor(static_cast<float>(price) * std::max(0.0f, rate) * (0.1f + 0.9f * c * c))));
}

int repairPrice(int price, float condition, float rate)
{
    const float missing = 1.0f - std::clamp(condition, 0.0f, 1.0f);
    return static_cast<int>(std::ceil(static_cast<float>(price) * rate * missing));
}

float armorAbsorb(const ItemDef& armor, float& condition, float damage)
{
    if (damage <= 0.0f || condition <= 0.0f)
        return std::max(damage, 0.0f);
    const float stopped = damage * std::clamp(armor.protection, 0.0f, 0.95f) * (0.3f + 0.7f * std::clamp(condition, 0.0f, 1.0f));
    condition = std::max(0.0f, condition - stopped * armor.wear);
    return damage - stopped;
}

float loadSpeedFactor(const ItemCatalog& c, float weight)
{
    if (weight <= c.carryFree)
        return 1.0f;
    const float span = std::max(0.1f, c.carryMax - c.carryFree);
    return std::max(0.3f, 1.0f - 0.7f * (weight - c.carryFree) / span);
}

bool loadAllowsSprint(const ItemCatalog& c, float weight)
{
    return weight <= c.carryFree;
}

} // namespace engine
