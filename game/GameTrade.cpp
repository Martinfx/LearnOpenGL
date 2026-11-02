#include "UiScale.h"
#include "GameTrade.h"

#include <engine/Localization.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <sstream>

namespace {
constexpr int kAmmoLot = 30; // rounds per click

std::string percent(float c)
{
    return std::to_string(static_cast<int>(std::lround(std::clamp(c, 0.0f, 1.0f) * 100.0f))) + " %";
}
} // namespace

std::vector<GameTrade::Stock> GameTrade::parseStock(const std::string& text)
{
    std::vector<Stock> out;
    std::stringstream all(text);
    std::string entry;
    while (std::getline(all, entry, ';')) {
        std::istringstream s(entry);
        std::string kind, id;
        if (!(s >> kind))
            continue;
        Stock st{Stock::Medkit, {}, 0};
        if (kind == "medkit") {
            st.kind = Stock::Medkit;
            st.id = "medkit";
        } else if (kind == "ammo" || kind == "weapon" || kind == "armor" || kind == "artifact") {
            st.kind = kind == "ammo" ? Stock::Ammo : kind == "weapon" ? Stock::Weapon : kind == "armor" ? Stock::Armor : Stock::Artifact;
            if (!(s >> st.id))
                continue;
        } else {
            continue;
        }
        if (!(s >> st.count))
            st.count = 1;
        out.push_back(st);
    }
    return out;
}

std::string GameTrade::formatStock(const std::vector<Stock>& stock)
{
    std::string out;
    for (const Stock& s : stock) {
        if (!out.empty())
            out += "; ";
        static const char* kinds[] = {"medkit", "ammo", "weapon", "armor", "artifact"};
        out += kinds[s.kind];
        if (s.kind != Stock::Medkit)
            out += ' ' + s.id;
        out += ' ' + std::to_string(s.count);
    }
    return out;
}

void GameTrade::begin(int entity, const std::string& name, const std::map<std::string, std::string>& p, const std::filesystem::path& assetDir)
{
    auto prop = [&](const char* key, const char* fallback) {
        const auto it = p.find(key);
        return it == p.end() || it->second.empty() ? std::string(fallback) : it->second;
    };
    entity_ = entity;
    name_ = name;
    stock_ = parseStock(prop("stock", ""));
    markup_ = std::max(0.1f, static_cast<float>(std::atof(prop("markup", "1.3").c_str())));
    buyRate_ = std::clamp(static_cast<float>(std::atof(prop("buy_rate", "0.45").c_str())), 0.0f, 1.0f);
    repairs_ = prop("repair", "0") == "1";
    assetDir_ = assetDir;
    message_.clear();
}

const engine::WeaponSettings* GameTrade::weaponInfo(const std::string& relative) const
{
    auto it = weapons_.find(relative);
    if (it == weapons_.end()) {
        engine::WeaponSettings w;
        if (!engine::loadWeaponFile(w, (assetDir_ / std::filesystem::path(std::u8string(relative.begin(), relative.end()))).lexically_normal()))
            return nullptr;
        it = weapons_.emplace(relative, std::move(w)).first;
    }
    return &it->second;
}

std::vector<GameTrade::Row> GameTrade::rows(const Inventory& inv, const std::vector<engine::Weapon>& weapons, std::size_t current,
                                            const engine::ItemCatalog& items) const
{
    std::vector<Row> out;
    // The trader's goods.
    for (std::size_t i = 0; i < stock_.size(); ++i) {
        const Stock& s = stock_[i];
        if (s.count <= 0)
            continue;
        Row r;
        r.action = 1;
        r.index = static_cast<int>(i);
        if (s.kind == Stock::Medkit) {
            const engine::ItemDef* d = items.find(engine::ItemDef::Kind::Medkit);
            r.label = d ? engine::localize(d->name) : engine::tr("item.medkit");
            r.detail = "× " + std::to_string(s.count);
            r.price = engine::buyPrice(d ? d->price : 250, markup_);
        } else if (s.kind == Stock::Ammo) {
            const engine::ItemDef* d = items.find(engine::ItemDef::Kind::Ammo, s.id);
            const int lot = std::min(kAmmoLot, s.count);
            r.label = engine::trf("trade.lot", d ? engine::localize(d->name) : s.id, std::to_string(lot));
            r.detail = "× " + std::to_string(s.count);
            r.price = engine::buyPrice((d ? d->price : 3) * lot, markup_);
        } else if (s.kind == Stock::Weapon) {
            const engine::WeaponSettings* w = weaponInfo(s.id);
            if (!w)
                continue;
            r.label = engine::localize(w->name);
            r.detail = "× " + std::to_string(s.count);
            r.price = engine::buyPrice(w->price, markup_);
            const std::filesystem::path abs = (assetDir_ / std::filesystem::path(std::u8string(s.id.begin(), s.id.end()))).lexically_normal();
            if (std::find(inv.weaponFiles.begin(), inv.weaponFiles.end(), abs) != inv.weaponFiles.end()) {
                r.enabled = false;
                r.detail = engine::tr("trade.owned");
            }
        } else if (s.kind == Stock::Artifact) {
            const engine::ItemDef* d = items.find(engine::ItemDef::Kind::Artifact, s.id);
            if (!d)
                continue;
            r.label = engine::localize(d->name);
            r.detail = engine::trf("trade.artifact_count", std::to_string(s.count));
            r.price = engine::buyPrice(d->price, markup_);
        } else {
            const engine::ItemDef* d = items.find(engine::ItemDef::Kind::Armor, s.id);
            if (!d)
                continue;
            r.label = engine::localize(d->name);
            r.detail = engine::trf("trade.protection", percent(d->protection));
            r.price = engine::buyPrice(d->price, markup_);
        }
        r.enabled = r.enabled && inv.money >= r.price;
        out.push_back(r);
    }
    // The player's things.
    for (std::size_t i = 0; i < weapons.size() && i < inv.weaponFiles.size(); ++i) {
        Row r;
        r.action = 2;
        r.index = static_cast<int>(i);
        r.key = "weapon";
        r.label = engine::localize(weapons[i].settings.name);
        r.detail = i == current ? engine::tr("trade.in_hand") : engine::trf("trade.condition", percent(weapons[i].condition));
        r.price = engine::sellPrice(weapons[i].settings.price, buyRate_, weapons[i].condition);
        r.enabled = i != current && weapons.size() > 1;
        out.push_back(r);
    }
    for (const auto& [type, rounds] : inv.ammo) {
        if (rounds <= 0)
            continue;
        const engine::ItemDef* d = items.find(engine::ItemDef::Kind::Ammo, type);
        const int lot = std::min(kAmmoLot, rounds);
        Row r;
        r.action = 2;
        r.detail = "× " + std::to_string(rounds);
        r.label = engine::trf("trade.lot", d ? engine::localize(d->name) : type, std::to_string(lot));
        r.price = engine::sellPrice((d ? d->price : 3) * lot, buyRate_);
        r.key = "ammo:" + type;
        out.push_back(r);
    }
    if (inv.medkits > 0) {
        const engine::ItemDef* d = items.find(engine::ItemDef::Kind::Medkit);
        Row r;
        r.action = 2;
        r.key = "medkit";
        r.label = d ? engine::localize(d->name) : engine::tr("item.medkit");
        r.detail = "× " + std::to_string(inv.medkits);
        r.price = engine::sellPrice(d ? d->price : 250, buyRate_);
        out.push_back(r);
    }
    if (const engine::ItemDef* d = inv.armor.empty() ? nullptr : items.find(engine::ItemDef::Kind::Armor, inv.armor)) {
        Row r;
        r.action = 2;
        r.key = "armor";
        r.label = engine::localize(d->name);
        r.detail = engine::trf("trade.worn", percent(inv.armorCondition));
        r.price = engine::sellPrice(d->price, buyRate_, inv.armorCondition);
        out.push_back(r);
    }
    for (std::size_t a = 0; a < inv.artifacts.size(); ++a) {
        const engine::ItemDef* d = items.find(engine::ItemDef::Kind::Artifact, inv.artifacts[a]);
        if (!d)
            continue;
        Row r;
        r.action = 2;
        r.key = "artifact";
        r.index = static_cast<int>(a);
        r.label = engine::localize(d->name);
        r.detail = static_cast<int>(a) < items.beltSlots ? engine::tr("trade.artifact_belt") : engine::tr("trade.artifact");
        r.price = engine::sellPrice(d->price, buyRate_);
        out.push_back(r);
    }
    // Services.
    if (repairs_ && current < weapons.size() && weapons[current].condition < 0.995f) {
        Row r;
        r.action = 3;
        r.label = engine::trf("trade.repair", engine::localize(weapons[current].settings.name), percent(weapons[current].condition));
        r.price = std::max(1, engine::repairPrice(weapons[current].settings.price, weapons[current].condition));
        r.enabled = inv.money >= r.price;
        out.push_back(r);
    }
    if (const engine::ItemDef* d = inv.armor.empty() || !repairs_ ? nullptr : items.find(engine::ItemDef::Kind::Armor, inv.armor);
        d && inv.armorCondition < 0.995f) {
        Row r;
        r.action = 4;
        r.label = engine::trf("trade.repair", engine::localize(d->name), percent(inv.armorCondition));
        r.price = std::max(1, engine::repairPrice(d->price, inv.armorCondition));
        r.enabled = inv.money >= r.price;
        out.push_back(r);
    }
    Row close;
    close.action = 5;
    close.label = engine::tr("trade.leave");
    out.push_back(close);
    return out;
}

void GameTrade::click(glm::vec2 mouse, Inventory& inv, std::vector<engine::Weapon>& weapons, std::size_t current,
                      const engine::ItemCatalog& items, const Hooks& hooks, bool many)
{
    const Row* hit = nullptr;
    for (const auto& [rect, row] : drawn_)
        if (mouse.x >= rect.x && mouse.y >= rect.y && mouse.x < rect.x + rect.z && mouse.y < rect.y + rect.w)
            hit = &row;
    if (!hit)
        return;
    const Row r = *hit;
    if (r.action == 5) {
        end();
        return;
    }
    if (!r.enabled) {
        if (r.action != 2 && inv.money < r.price)
            message_ = engine::tr("trade.no_money");
        return;
    }
    const int times = many ? 5 : 1;
    if (r.action == 1) {
        Stock& s = stock_[static_cast<std::size_t>(r.index)];
        for (int t = 0; t < (s.kind == Stock::Ammo || s.kind == Stock::Medkit ? times : 1) && s.count > 0; ++t) {
            int price = r.price;
            if (s.kind == Stock::Ammo) {
                const engine::ItemDef* d = items.find(engine::ItemDef::Kind::Ammo, s.id);
                price = engine::buyPrice((d ? d->price : 3) * std::min(kAmmoLot, s.count), markup_);
            }
            if (inv.money < price) {
                message_ = engine::tr("trade.no_money");
                break;
            }
            if (s.kind == Stock::Medkit) {
                inv.medkits += 1;
                s.count -= 1;
            } else if (s.kind == Stock::Ammo) {
                const int lot = std::min(kAmmoLot, s.count);
                inv.ammo[s.id] += lot;
                s.count -= lot;
                inv.link(weapons);
            } else if (s.kind == Stock::Weapon) {
                const auto slot =
                    hooks.addWeapon ? hooks.addWeapon((assetDir_ / std::filesystem::path(std::u8string(s.id.begin(), s.id.end()))).lexically_normal())
                                    : std::nullopt;
                if (!slot)
                    break;
                s.count -= 1;
            } else if (s.kind == Stock::Artifact) {
                inv.artifacts.push_back(s.id);
                s.count -= 1;
            } else {
                if (const engine::ItemDef* old = inv.armor.empty() ? nullptr : items.find(engine::ItemDef::Kind::Armor, inv.armor)) {
                    inv.money += engine::sellPrice(old->price, buyRate_, inv.armorCondition); // the old one goes to the trader
                    stock_.push_back({Stock::Armor, inv.armor, 1});
                }
                inv.armor = s.id;
                inv.armorCondition = 1.0f;
                s.count -= 1;
            }
            inv.money -= price;
            message_ = engine::trf("trade.bought", r.label);
        }
        if (hooks.sound)
            hooks.sound("sounds/items/pickup_ammo.sound");
    } else if (r.action == 2) {
        auto addStock = [&](Stock::Kind kind, const std::string& id, int n) {
            for (Stock& s : stock_)
                if (s.kind == kind && s.id == id) {
                    s.count += n;
                    return;
                }
            stock_.push_back({kind, id, n});
        };
        if (r.key == "weapon") {
            const std::size_t slot = static_cast<std::size_t>(r.index);
            const std::string rel = std::filesystem::relative(inv.weaponFiles[slot], assetDir_).generic_string();
            inv.money += r.price;
            addStock(Stock::Weapon, rel, 1);
            if (hooks.removeWeapon)
                hooks.removeWeapon(slot);
        } else if (r.key.starts_with("ammo:")) {
            const std::string type = r.key.substr(5);
            const engine::ItemDef* d = items.find(engine::ItemDef::Kind::Ammo, type);
            for (int t = 0; t < times && inv.ammo[type] > 0; ++t) {
                const int lot = std::min(kAmmoLot, inv.ammo[type]);
                inv.money += engine::sellPrice((d ? d->price : 3) * lot, buyRate_);
                inv.ammo[type] -= lot;
                addStock(Stock::Ammo, type, lot);
            }
        } else if (r.key == "medkit") {
            for (int t = 0; t < times && inv.medkits > 0; ++t) {
                inv.money += r.price;
                inv.medkits -= 1;
                addStock(Stock::Medkit, "medkit", 1);
            }
        } else if (r.key == "artifact" && r.index < static_cast<int>(inv.artifacts.size())) {
            inv.money += r.price;
            addStock(Stock::Artifact, inv.artifacts[static_cast<std::size_t>(r.index)], 1);
            inv.artifacts.erase(inv.artifacts.begin() + r.index);
        } else if (r.key == "armor") {
            inv.money += r.price;
            addStock(Stock::Armor, inv.armor, 1);
            inv.armor.clear();
        }
        message_ = engine::trf("trade.sold", r.label);
        if (hooks.sound)
            hooks.sound("sounds/items/pickup_ammo.sound");
    } else if (r.action == 3 && current < weapons.size()) {
        inv.money -= r.price;
        weapons[current].condition = 1.0f;
        message_ = engine::tr("trade.weapon_repaired");
    } else if (r.action == 4) {
        inv.money -= r.price;
        inv.armorCondition = 1.0f;
        message_ = engine::tr("trade.armor_repaired");
    }
}

void GameTrade::draw(engine::Ui2D& ui, int width, int height, const Inventory& inv, const std::vector<engine::Weapon>& weapons,
                     std::size_t current, const engine::ItemCatalog& items) const
{
    drawn_.clear();
    if (!active())
        return;
    const float W = static_cast<float>(width), H = static_cast<float>(height);
    const float text = uiSize(H, 0.022f, 12.0f, 26.0f);
    const float panelW = std::round(std::min(W - text * 2.0f, text * 56.0f)), panelH = std::round(std::min(H - text * 2.0f, text * 30.0f));
    const glm::vec2 pos(std::round((W - panelW) * 0.5f), std::round((H - panelH) * 0.5f));
    const glm::vec4 frame(0.05f, 0.055f, 0.045f, 0.94f), bone(0.88f, 0.84f, 0.74f, 1.0f), amber(0.93f, 0.62f, 0.2f, 1.0f),
        dim(0.5f, 0.5f, 0.46f, 1.0f), red(0.9f, 0.4f, 0.3f, 1.0f);
    ui.rect({0, 0}, {W, H}, {0.0f, 0.0f, 0.0f, 0.45f});
    ui.rect(pos, {panelW, panelH}, frame);
    ui.rect(pos, {panelW, std::max(2.0f, text * 0.12f)}, amber);
    const float weight = carriedWeight(inv, weapons, items);
    char load[64];
    std::snprintf(load, sizeof load, "%.1f / %.0f", weight, items.carryFree);
    const std::string head = engine::trf("trade.header", std::to_string(inv.money), std::string(load));
    ui.text(engine::trf("trade.title", name_), pos + glm::vec2(text, text * 0.7f), text * 1.1f, amber, true);
    ui.text(head, pos + glm::vec2(panelW - ui.textWidth(head, text) - text, text * 0.8f), text,
            weight > items.carryFree ? red : bone);
    const float colW = (panelW - text * 3.0f) * 0.5f;
    const float top = pos.y + text * 3.2f;
    ui.text(engine::tr("trade.offers"), {pos.x + text, top}, text * 0.9f, dim, true);
    ui.text(engine::tr("trade.yours"), {pos.x + text * 2.0f + colW, top}, text * 0.9f, dim, true);
    float y[2] = {top + text * 1.6f, top + text * 1.6f};
    float bottom = pos.y + panelH - text * 1.2f;
    std::vector<Row> list = rows(inv, weapons, current, items);
    // Services and close: buttons along the bottom.
    float bx = pos.x + text;
    for (const Row& r : list) {
        if (r.action < 3)
            continue;
        const std::string label = r.action == 5 ? r.label : r.label + "  " + std::to_string(r.price) + " RU";
        const float w = ui.textWidth(label, text * 0.9f, true) + text * 1.4f;
        const glm::vec4 rect(bx, bottom - text * 1.6f, w, text * 1.6f);
        const bool over = mouse_.x >= rect.x && mouse_.y >= rect.y && mouse_.x < rect.x + rect.z && mouse_.y < rect.y + rect.w;
        ui.rect({rect.x, rect.y}, {rect.z, rect.w}, over ? glm::vec4(0.93f, 0.62f, 0.2f, 0.3f) : glm::vec4(0.15f, 0.15f, 0.13f, 0.9f));
        ui.text(label, {rect.x + text * 0.7f, rect.y + text * 0.3f}, text * 0.9f, r.enabled ? bone : dim, true);
        drawn_.emplace_back(rect, r);
        bx += w + text * 0.6f;
    }
    const float listBottom = bottom - text * 2.4f;
    for (const Row& r : list) {
        if (r.action > 2)
            continue;
        const int col = r.action - 1;
        if (y[col] > listBottom - text * 1.3f)
            continue;
        const float x = pos.x + text + static_cast<float>(col) * (colW + text);
        const glm::vec4 rect(x, y[col] - text * 0.2f, colW, text * 1.4f);
        const bool over = mouse_.x >= rect.x && mouse_.y >= rect.y && mouse_.x < rect.x + rect.z && mouse_.y < rect.y + rect.w;
        if (over && r.enabled)
            ui.rect({rect.x, rect.y}, {rect.z, rect.w}, {0.93f, 0.62f, 0.2f, 0.18f});
        const glm::vec4 c = r.enabled ? (over ? amber : bone) : dim;
        ui.text(r.label, {x + text * 0.4f, y[col]}, text * 0.95f, c);
        const std::string price = std::to_string(r.price) + " RU";
        ui.text(price, {x + colW - ui.textWidth(price, text * 0.95f) - text * 0.4f, y[col]}, text * 0.95f, c, true);
        ui.text(r.detail, {x + colW * 0.58f, y[col]}, text * 0.85f, dim);
        drawn_.emplace_back(rect, r);
        y[col] += text * 1.45f;
    }
    if (!message_.empty())
        ui.text(message_, {pos.x + text, listBottom + text * 0.2f}, text * 0.9f, amber);
}
