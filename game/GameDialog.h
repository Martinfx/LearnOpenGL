#pragma once
#include <engine/Dialog.h>
#include <engine/Ui2D.h>

#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace engine {
class Quests;
}

// A conversation on the screen: what the character says at the bottom, the player's answers
// under it (keys 1-9 or a mouse click, Esc leaves). The game is paused while talking.
class GameDialog {
public:
    // `file` = .dialog of the entity (relative to the assets folder or absolute). False when it
    // cannot be read or has nothing to say now (error / reason in `why`).
    bool begin(const std::filesystem::path& file, const std::string& speaker, int entity, engine::Quests& quests, std::string* why);
    bool active() const { return session_.active(); }
    int entity() const { return entity_; }
    // Picks visible answer `index` (0-based); returns the game's actions (give, fire, hostile).
    std::vector<engine::DialogAction> choose(int index, engine::Quests& quests);
    std::vector<engine::DialogAction> takeGameActions() { return session_.takeGameActions(); }
    void end() { session_.end(); }
    int answers() const; // visible answers (a talk with none shows "Odejít", index 0)
    // The answer under the mouse (pixels, as drawn last), -1 = none.
    int hit(glm::vec2 mouse) const;
    void draw(engine::Ui2D& ui, int width, int height, int hover) const;
    void reloadFiles() { cache_.clear(); } // hot reload of edited dialogs
    // The dialog's speaker in the current language (for "E  Talk: Tolik"), else `fallback`.
    std::string speakerOf(const std::filesystem::path& file, const std::string& fallback);

private:
    const engine::DialogDef* dialog(const std::filesystem::path& file, std::string* why = nullptr); // loaded once
    std::map<std::filesystem::path, engine::DialogDef> cache_;
    engine::DialogSession session_;
    std::string speaker_;
    int entity_ = -1;
    mutable std::vector<std::pair<glm::vec2, glm::vec2>> answerRects_; // pos, size of each answer as drawn
};

// Breaks `text` into lines no wider than `width` pixels (at spaces).
std::vector<std::string> wrapText(const engine::Ui2D& ui, const std::string& text, float size, float width, bool bold = false);
