#pragma once
#include <engine/Ui2D.h>

#include "GameSave.h"

#include <glm/vec2.hpp>

#include <filesystem>
#include <string>
#include <vector>

// Menus drawn over the game with Ui2D: main menu, pause, settings, credits and
// the death screen. Immediate mode: draw() lays the screen out and handles the
// mouse in one go; what the player chose comes out of takeAction().
//
// Settings are the console variables flagged CVarConfig (engine/CVars.h), so a
// new setting appears here as soon as it is registered; its label and page come
// from the table in GameMenu.cpp (unknown ones go to "Other").
class GameMenu {
public:
    enum class Screen { None, Main, Pause, Settings, Credits, Death, Levels, Load, Save };
    enum class Action { None, Play, Resume, Respawn, Restart, LoadLevel, LoadSave, SaveSlot, MainMenu, Quit };

    // Reads the credits (CREDITS.md under assets/ and the third-party licences).
    void init(const std::filesystem::path& assetDir, const std::filesystem::path& licenseFile);
    void setTitle(const std::string& title, const std::string& subtitle);

    void open(Screen screen);
    void close() { open(Screen::None); }
    Screen screen() const { return screen_; }
    bool active() const { return screen_ != Screen::None; }
    bool started() const { return started_; } // a game is running (Main shows "Continue")
    void setStarted(bool started) { started_ = started; }
    void setSettingsPage(int page) { settingsPage_ = page; } // 0 graphics, 1 controls, 2 audio

    // Esc: back one screen (settings/credits to where they came from, pause to the game).
    void back();
    // Mouse in window pixels; call every frame before draw().
    void mouse(glm::vec2 position, bool leftDown, double scroll);
    // Key and mouse button presses (KeyNames.h codes) for binding a key in Settings > Controls.
    void keyEvent(int code);
    bool capturingKey() const { return !capturing_.empty(); }
    void draw(engine::Ui2D& ui, int width, int height, double time);
    Action takeAction();
    const std::filesystem::path& chosenLevel() const { return chosenLevel_; } // after Action::LoadLevel
    const std::filesystem::path& chosenSave() const { return chosenSave_; }   // after Action::LoadSave
    const std::string& chosenSlot() const { return chosenSlot_; }             // after Action::SaveSlot
    void setCanSave(bool can) { canSave_ = can; }                             // not while dead

private:
    bool button(engine::Ui2D& ui, glm::vec2 pos, glm::vec2 size, const std::string& label, bool enabled = true);
    void drawMain(engine::Ui2D& ui, float w, float h, double time);
    void drawPause(engine::Ui2D& ui, float w, float h);
    void drawSettings(engine::Ui2D& ui, float w, float h);
    void drawCredits(engine::Ui2D& ui, float w, float h);
    void drawDeath(engine::Ui2D& ui, float w, float h, double time);
    void drawLevels(engine::Ui2D& ui, float w, float h);
    void drawSaves(engine::Ui2D& ui, float w, float h, bool saving);

    Screen screen_ = Screen::None;
    Screen returnTo_ = Screen::Main; // where settings / credits go back to
    Action action_ = Action::None;
    bool started_ = false;
    std::string title_ = "FPS Engine", subtitle_;
    double openedAt_ = 0.0;
    bool justOpened_ = true;

    glm::vec2 mouse_{0.0f};
    bool down_ = false, pressed_ = false, released_ = false;
    double scroll_ = 0.0;
    std::string dragging_;     // slider being dragged (cvar name)
    int settingsPage_ = 0;
    int settingsScroll_ = 0;   // rows scrolled on the current page
    std::string capturing_;    // binding waiting for a key (cvar name)
    bool swallowEsc_ = false;
    bool suppressRelease_ = false;  // the Esc that cancelled a binding does not also leave the screen
    float creditsScroll_ = 0.0f;

    struct CreditLine {
        std::string text;
        int kind; // 0 heading, 1 entry, 2 blank
    };
    std::vector<CreditLine> credits_;

    struct LevelEntry {
        std::filesystem::path file;
        std::string title, hint;
    };
    std::vector<LevelEntry> levels_; // assets/levels/*.level
    std::filesystem::path chosenLevel_;
    std::vector<SaveInfo> saves_; // refreshed when a load / save screen opens
    std::filesystem::path chosenSave_;
    std::string chosenSlot_;
    bool canSave_ = true;
    std::filesystem::path assetDir_; // language list (assets/text)
};
