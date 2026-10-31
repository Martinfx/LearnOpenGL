#pragma once
#include <engine/Ui2D.h>

#include <string>
#include <vector>

// In-game console (~): the engine log and a command line for console variables
// and commands (engine/CVars.h). Text comes from GLFW's char callback.
class GameConsole {
public:
    bool open() const { return open_; }
    void toggle();
    void close() { open_ = false; }

    void onChar(unsigned codepoint);           // typed text (UTF-32)
    // Editing keys (GLFW key codes); returns true when the console used the key.
    bool onKey(int key, int action, int mods);
    void draw(engine::Ui2D& ui, int width, int height, double time);
    void submitLine(const std::string& line) { input_ = line; submit(); }

private:
    void submit();
    void completeWord();

    bool open_ = false;
    bool swallowChar_ = false; // the ~ that opened the console also arrives as a char
    std::string input_;
    std::vector<std::string> history_;
    int historyPos_ = -1;
    int scroll_ = 0;           // lines scrolled up from the newest
};
