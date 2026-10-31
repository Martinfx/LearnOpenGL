#include "GameConsole.h"

#include <engine/CVars.h>
#include <engine/Log.h>

#include <GLFW/glfw3.h>

#include <algorithm>
#include <cmath>

namespace {
void appendUtf8(std::string& s, unsigned c)
{
    if (c < 0x80) {
        s += static_cast<char>(c);
    } else if (c < 0x800) {
        s += static_cast<char>(0xC0 | (c >> 6));
        s += static_cast<char>(0x80 | (c & 0x3F));
    } else if (c < 0x10000) {
        s += static_cast<char>(0xE0 | (c >> 12));
        s += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
        s += static_cast<char>(0x80 | (c & 0x3F));
    } else {
        s += static_cast<char>(0xF0 | (c >> 18));
        s += static_cast<char>(0x80 | ((c >> 12) & 0x3F));
        s += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
        s += static_cast<char>(0x80 | (c & 0x3F));
    }
}
void popUtf8(std::string& s)
{
    while (!s.empty()) {
        const unsigned char c = static_cast<unsigned char>(s.back());
        s.pop_back();
        if ((c & 0xC0) != 0x80)
            break;
    }
}
} // namespace

void GameConsole::toggle()
{
    open_ = !open_;
    swallowChar_ = true;
    scroll_ = 0;
}

void GameConsole::onChar(unsigned codepoint)
{
    if (!open_)
        return;
    if (swallowChar_) {
        swallowChar_ = false;
        if (codepoint == '`' || codepoint == '~' || codepoint == ';' || codepoint == 0x16F) // the toggle key (US, Czech layouts)
            return;
    }
    if (codepoint >= 32)
        appendUtf8(input_, codepoint);
}

bool GameConsole::onKey(int key, int action, int mods)
{
    (void)mods;
    if (!open_ || (action != GLFW_PRESS && action != GLFW_REPEAT))
        return false;
    swallowChar_ = false;
    switch (key) {
    case GLFW_KEY_ENTER:
    case GLFW_KEY_KP_ENTER: submit(); return true;
    case GLFW_KEY_BACKSPACE: popUtf8(input_); return true;
    case GLFW_KEY_ESCAPE: open_ = false; return true;
    case GLFW_KEY_TAB: completeWord(); return true;
    case GLFW_KEY_UP:
        if (!history_.empty()) {
            historyPos_ = historyPos_ < 0 ? static_cast<int>(history_.size()) - 1 : std::max(0, historyPos_ - 1);
            input_ = history_[static_cast<std::size_t>(historyPos_)];
        }
        return true;
    case GLFW_KEY_DOWN:
        if (historyPos_ >= 0) {
            ++historyPos_;
            if (historyPos_ >= static_cast<int>(history_.size())) {
                historyPos_ = -1;
                input_.clear();
            } else {
                input_ = history_[static_cast<std::size_t>(historyPos_)];
            }
        }
        return true;
    case GLFW_KEY_PAGE_UP: scroll_ += 10; return true;
    case GLFW_KEY_PAGE_DOWN: scroll_ = std::max(0, scroll_ - 10); return true;
    default: return true; // the game does not see keys while the console is open
    }
}

void GameConsole::submit()
{
    const std::string line = input_;
    input_.clear();
    historyPos_ = -1;
    scroll_ = 0;
    if (line.find_first_not_of(' ') == std::string::npos)
        return;
    if (history_.empty() || history_.back() != line)
        history_.push_back(line);
    LOG_INFO("console", "> %s", line.c_str());
    const engine::ConsoleResult r = engine::CVarRegistry::global().execute(line);
    if (!r.text.empty()) {
        // Multi-line results (help) are logged line by line.
        std::size_t start = 0;
        while (start <= r.text.size()) {
            const std::size_t end = r.text.find('\n', start);
            const std::string part = r.text.substr(start, end == std::string::npos ? std::string::npos : end - start);
            engine::logMessage(r.ok ? engine::LogLevel::Info : engine::LogLevel::Warning, "console", part);
            if (end == std::string::npos)
                break;
            start = end + 1;
        }
    }
}

void GameConsole::completeWord()
{
    if (input_.find(' ') != std::string::npos)
        return;
    const std::vector<std::string> names = engine::CVarRegistry::global().complete(input_);
    if (names.empty())
        return;
    if (names.size() == 1) {
        input_ = names[0] + " ";
        return;
    }
    // Longest common prefix, and list the candidates.
    std::string common = names[0];
    for (const std::string& n : names) {
        std::size_t k = 0;
        while (k < common.size() && k < n.size() && common[k] == n[k])
            ++k;
        common.resize(k);
    }
    input_ = common;
    std::string all;
    for (const std::string& n : names)
        all += n + "  ";
    LOG_INFO("console", "%s", all.c_str());
}

void GameConsole::draw(engine::Ui2D& ui, int width, int height, double time)
{
    if (!open_)
        return;
    const float W = static_cast<float>(width), H = static_cast<float>(height) * 0.45f;
    const float size = std::round(std::clamp(static_cast<float>(height) * 0.022f, 13.0f, 22.0f));
    const float pad = size * 0.6f;
    ui.rect({0, 0}, {W, H}, {0.04f, 0.04f, 0.035f, 0.88f});
    ui.rect({0, H}, {W, 2}, {0.78f, 0.42f, 0.13f, 1.0f});
    // input line
    const float inputY = H - pad - size;
    const bool caret = std::fmod(time, 1.0) < 0.55;
    ui.text("> " + input_ + (caret ? "_" : ""), {pad, inputY}, size, {0.95f, 0.9f, 0.8f, 1.0f}, true);
    // log lines, newest at the bottom
    const int rows = std::max(1, static_cast<int>((inputY - pad) / (size * 1.15f)) - 1);
    const std::vector<engine::LogLine> lines = engine::recentLog(static_cast<std::size_t>(rows + scroll_));
    const int count = static_cast<int>(lines.size());
    const int last = std::max(0, count - scroll_);
    const int first = std::max(0, last - rows);
    float y = inputY - size * 1.4f;
    for (int i = last - 1; i >= first; --i) {
        const engine::LogLine& l = lines[static_cast<std::size_t>(i)];
        const glm::vec4 color = l.level >= engine::LogLevel::Error     ? glm::vec4(1.0f, 0.42f, 0.37f, 1.0f)
                                : l.level == engine::LogLevel::Warning ? glm::vec4(0.91f, 0.76f, 0.35f, 1.0f)
                                : l.category == "console"              ? glm::vec4(0.85f, 0.85f, 0.8f, 1.0f)
                                                                       : glm::vec4(0.62f, 0.62f, 0.58f, 1.0f);
        std::string text = l.category.empty() || l.category == "console" ? l.text : l.category + ": " + l.text;
        // Cut lines wider than the screen.
        while (text.size() > 4 && ui.textWidth(text, size) > W - 2 * pad)
            text.resize(text.size() * 9 / 10);
        ui.text(text, {pad, y}, size, color);
        y -= size * 1.15f;
    }
    if (scroll_ > 0)
        ui.text("PgDn", {W - pad - ui.textWidth("PgDn", size), inputY}, size, {0.6f, 0.6f, 0.55f, 1.0f});
}
