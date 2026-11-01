#pragma once
#include <engine/Ui2D.h>

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
    enum class Screen { None, Main, Pause, Settings, Credits, Death };
    enum class Action { None, Play, Resume, Respawn, MainMenu, Quit };

    // Reads the credits (CREDITS.md under assets/ and the third-party licences).
    void init(const std::filesystem::path& assetDir, const std::filesystem::path& licenseFile);
    void setTitle(const std::string& title, const std::string& subtitle);

    void open(Screen screen);
    void close() { open(Screen::None); }
    Screen screen() const { return screen_; }
    bool active() const { return screen_ != Screen::None; }
    bool started() const { return started_; } // a game is running (Main shows "Continue")
    void setStarted(bool started) { started_ = started; }

    // Esc: back one screen (settings/credits to where they came from, pause to the game).
    void back();
    // Mouse in window pixels; call every frame before draw().
    void mouse(glm::vec2 position, bool leftDown, double scroll);
    void draw(engine::Ui2D& ui, int width, int height, double time);
    Action takeAction();

private:
    bool button(engine::Ui2D& ui, glm::vec2 pos, glm::vec2 size, const std::string& label, bool enabled = true);
    void drawMain(engine::Ui2D& ui, float w, float h, double time);
    void drawPause(engine::Ui2D& ui, float w, float h);
    void drawSettings(engine::Ui2D& ui, float w, float h);
    void drawCredits(engine::Ui2D& ui, float w, float h);
    void drawDeath(engine::Ui2D& ui, float w, float h, double time);

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
    float creditsScroll_ = 0.0f;

    struct CreditLine {
        std::string text;
        int kind; // 0 heading, 1 entry, 2 blank
    };
    std::vector<CreditLine> credits_;
};
