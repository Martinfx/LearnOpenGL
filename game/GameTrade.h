#pragma once
#include "Inventory.h"

#include <engine/Items.h>
#include <engine/Ui2D.h>
#include <engine/Weapon.h>

#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

// Trading with a character whose entity has the property trade = 1 (E, or a dialog's `trade`
// action). Its other properties: stock ("medkit 5; ammo 5.45x39 300; weapon weapons/smg.weapon 1;
// armor jacket 1"), markup (what it asks over the price, 1.3), buy_rate (what it pays, 0.45)
// and repair (1 = repairs weapons and armor). The game is paused; a click buys or sells.
class GameTrade {
public:
    struct Stock {
        enum Kind { Medkit, Ammo, Weapon, Armor } kind;
        std::string id; // ammo type, .weapon path (relative to assets/), armor id
        int count = 0;
    };
    // What the trade does to the game besides the inventory.
    struct Hooks {
        std::function<std::optional<std::size_t>(const std::filesystem::path&)> addWeapon; // absolute .weapon
        std::function<void(std::size_t)> removeWeapon;                                    // a carried slot
        std::function<void(const std::string&)> sound;                                    // relative to assets/
    };

    static std::vector<Stock> parseStock(const std::string& text);
    static std::string formatStock(const std::vector<Stock>& stock);

    void begin(int entity, const std::string& name, const std::map<std::string, std::string>& properties, const std::filesystem::path& assetDir);
    bool active() const { return entity_ >= 0; }
    int entity() const { return entity_; }
    void end() { entity_ = -1; }
    const std::vector<Stock>& stock() const { return stock_; }

    // A click at `mouse` (pixels as drawn last): buys, sells or repairs.
    void click(glm::vec2 mouse, Inventory& inventory, std::vector<engine::Weapon>& weapons, std::size_t current,
               const engine::ItemCatalog& items, const Hooks& hooks, bool many);
    void hover(glm::vec2 mouse) { mouse_ = mouse; }
    void draw(engine::Ui2D& ui, int width, int height, const Inventory& inventory, const std::vector<engine::Weapon>& weapons,
              std::size_t current, const engine::ItemCatalog& items) const;

private:
    const engine::WeaponSettings* weaponInfo(const std::string& relative) const; // cached .weapon of the stock
    struct Row {
        std::string label, detail;
        int price = 0;
        bool enabled = true;
        int action = 0;  // 1 buy, 2 sell, 3 repair weapon, 4 repair armor, 5 close
        int index = 0;   // stock row / weapon slot / ammo index
        std::string key; // ammo type for selling
    };
    std::vector<Row> rows(const Inventory& inventory, const std::vector<engine::Weapon>& weapons, std::size_t current,
                          const engine::ItemCatalog& items) const;

    int entity_ = -1;
    std::string name_;
    std::vector<Stock> stock_;
    float markup_ = 1.3f, buyRate_ = 0.45f;
    bool repairs_ = false;
    std::filesystem::path assetDir_;
    mutable std::map<std::string, engine::WeaponSettings> weapons_;
    std::string message_;
    glm::vec2 mouse_{-1.0f};
    mutable std::vector<std::pair<glm::vec4, Row>> drawn_; // x, y, w, h of each clickable row as drawn
};
