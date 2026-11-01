#include "GameMenu.h"
#include <engine/Vfs.h>

#include <engine/CVars.h>
#include <engine/Crash.h>
#include <engine/Paths.h>
#include <engine/Scene.h>

#include "KeyNames.h"

#include <GLFW/glfw3.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <map>
#include <regex>

namespace {

// Zone palette: worn bone text on dark olive panels, amber for focus.
const glm::vec4 kPanel(0.05f, 0.055f, 0.045f, 0.82f);
const glm::vec4 kShade(0.02f, 0.02f, 0.015f, 0.55f);
const glm::vec4 kBone(0.88f, 0.84f, 0.74f, 1.0f);
const glm::vec4 kDim(0.55f, 0.54f, 0.49f, 1.0f);
const glm::vec4 kAmber(0.93f, 0.62f, 0.2f, 1.0f);
const glm::vec4 kTrack(0.16f, 0.16f, 0.14f, 0.95f);

// Labels and pages of the settings (console variables). Order here is the order on screen.
struct SettingInfo {
    const char* cvar;
    const char* label;
    int page; // 0 graphics, 1 controls, 2 audio
    std::vector<const char*> choices; // ints shown as named choices (min..max)
};
const std::vector<SettingInfo>& settingInfos()
{
    static const std::vector<SettingInfo> infos = {
        {"r_quality", "Kvalita grafiky", 0, {"Nízká", "Střední", "Vysoká", "Ultra"}},
        {"fov", "Zorné pole (FOV)", 0, {}},
        {"vsync", "Vertikální synchronizace", 0, {}},
        {"fps_max", "Omezení snímků (0 = bez)", 0, {}},
        {"cl_showfps", "Zobrazit FPS", 0, {}},
        {"r_fullscreen", "Režim okna", 0, {"Okno", "Bez rámečku", "Celá obrazovka"}},
        {"r_monitor", "Monitor", 0, {"Hlavní", "2", "3", "4", "5", "6", "7", "8"}},
        {"r_scale", "Rozlišení scény", 0, {}},
        {"r_aa", "Vyhlazování hran", 0, {"Vypnuto", "FXAA", "TAA"}},
        {"r_motionblur", "Rozmazání pohybem", 0, {}},
        {"m_sensitivity", "Citlivost myši", 1, {}},
        {"m_invert_y", "Obrátit osu Y", 1, {}},
        {"joy_sensitivity", "Citlivost gamepadu", 1, {}},
        {"joy_deadzone", "Mrtvá zóna páček", 1, {}},
        {"in_forward", "Dopředu", 1, {}},
        {"in_back", "Dozadu", 1, {}},
        {"in_left", "Vlevo", 1, {}},
        {"in_right", "Vpravo", 1, {}},
        {"in_jump", "Skok", 1, {}},
        {"in_sprint", "Sprint", 1, {}},
        {"in_crouch", "Dolů (noclip, volná kamera)", 1, {}},
        {"in_fire", "Střelba", 1, {}},
        {"in_aim", "Míření", 1, {}},
        {"in_reload", "Přebití", 1, {}},
        {"in_bolt", "Hodit šroub", 1, {}},
        {"in_map", "Mapa", 1, {}},
        {"in_quicksave", "Rychlé uložení", 1, {}},
        {"in_quickload", "Rychlé načtení", 1, {}},
        {"snd_volume", "Celková hlasitost", 2, {}},
        {"snd_sfx", "Efekty", 2, {}},
        {"snd_ambient", "Prostředí", 2, {}},
        {"snd_music", "Hudba", 2, {}},
    };
    return infos;
}
const char* const kPages[] = {"Grafika", "Ovládání", "Zvuk", "Ostatní"};

std::string formatValue(const engine::CVar& v)
{
    char buf[32];
    if (v.type == engine::CVarType::Float) {
        const float x = v.number();
        if (v.max <= 1.0 && v.min >= 0.0)
            std::snprintf(buf, sizeof buf, "%d %%", static_cast<int>(std::lround(x * 100.0f)));
        else
            std::snprintf(buf, sizeof buf, x >= 10.0f ? "%.0f" : "%.2f", x);
        return buf;
    }
    return v.value;
}

// "| a | `b` | [c](url) | d |" -> "a · b · c · d"; headings keep their text.
std::string plainMarkdown(std::string s)
{
    static const std::regex link(R"(\[([^\]]*)\]\([^)]*\))");
    s = std::regex_replace(s, link, "$1");
    s.erase(std::remove(s.begin(), s.end(), '`'), s.end());
    std::string out;
    for (char c : s)
        if (c != '*')
            out += c;
    return out;
}

} // namespace

void GameMenu::init(const std::filesystem::path& assetDir, const std::filesystem::path& licenseFile)
{
    credits_.clear();
    levels_.clear();
    std::error_code levelsError;
    for (const std::filesystem::path& level : engine::vfs().list(assetDir / "levels")) // mods can add levels
        if (level.extension() == ".level") {
            const engine::LoadingScreen look = engine::readLoadingScreen(level);
            levels_.push_back({level, look.title.empty() ? level.stem().string() : look.title, look.hint});
        }
    std::sort(levels_.begin(), levels_.end(), [](const LevelEntry& a, const LevelEntry& b) { return a.title < b.title; });
    std::vector<std::filesystem::path> files;
    std::error_code ec;
    for (const std::filesystem::path& f : engine::vfs().list(assetDir, true))
        if (f.filename() == "CREDITS.md")
            files.push_back(f);
    std::sort(files.begin(), files.end());
    if (!licenseFile.empty())
        files.push_back(licenseFile);
    // Each file: its title, then the rows of its tables (the attribution CC-BY asks for).
    for (const std::filesystem::path& file : files) {
        engine::VfsStream in(file);
        bool titled = false, inTable = false;
        int rows = 0;
        for (std::string line; std::getline(in, line);) {
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            if (line.rfind("# ", 0) == 0 && !titled) {
                credits_.push_back({"", 2});
                credits_.push_back({plainMarkdown(line.substr(2)), 0});
                titled = true;
                continue;
            }
            if (line.rfind("|", 0) != 0) {
                inTable = false;
                continue;
            }
            if (line.find("---") != std::string::npos) { // header separator: rows follow
                inTable = true;
                continue;
            }
            if (!inTable)
                continue;
            std::vector<std::string> cells;
            std::size_t start = 1;
            while (start < line.size()) {
                const std::size_t end = line.find('|', start);
                std::string cell = line.substr(start, end == std::string::npos ? std::string::npos : end - start);
                cell.erase(0, cell.find_first_not_of(' '));
                cell.erase(cell.find_last_not_of(' ') + 1);
                if (!cell.empty())
                    cells.push_back(plainMarkdown(cell));
                if (end == std::string::npos)
                    break;
                start = end + 1;
            }
            std::string text;
            for (const std::string& c : cells)
                text += (text.empty() ? "" : "  ·  ") + c;
            if (!text.empty()) {
                credits_.push_back({text, 1});
                ++rows;
            }
        }
        if (titled && rows == 0)
            credits_.pop_back(), credits_.pop_back(); // a CREDITS.md without a table: nothing to show
    }
}

void GameMenu::setTitle(const std::string& title, const std::string& subtitle)
{
    title_ = title;
    subtitle_ = subtitle;
}

void GameMenu::open(Screen screen)
{
    auto sub = [](Screen s) {
        return s == Screen::Settings || s == Screen::Credits || s == Screen::Levels || s == Screen::Load || s == Screen::Save;
    };
    if (screen == Screen::Load || screen == Screen::Save || screen == Screen::Main || screen == Screen::Death || screen == Screen::Pause)
        saves_ = listSaves();
    if (sub(screen) && !sub(screen_))
        returnTo_ = screen_ == Screen::None ? Screen::Pause : screen_;
    screen_ = screen;
    dragging_.clear();
    capturing_.clear();
    settingsScroll_ = 0;
    creditsScroll_ = 0.0f;
    justOpened_ = true;
    openedAt_ = 0.0;
}

void GameMenu::keyEvent(int code)
{
    if (capturing_.empty())
        return;
    if (code == GLFW_KEY_ESCAPE) {
        capturing_.clear();
        swallowEsc_ = true;
        return;
    }
    const std::string name = keyName(code);
    if (name.empty())
        return; // a key without a name (layout specific): keep waiting
    engine::CVarRegistry::global().set(capturing_, name);
    capturing_.clear();
    if (code >= kMouseBase)
        suppressRelease_ = true; // releasing the button that was just bound clicks nothing
}

void GameMenu::back()
{
    if (swallowEsc_) {
        swallowEsc_ = false;
        return;
    }
    if (!capturing_.empty()) {
        capturing_.clear();
        return;
    }
    switch (screen_) {
    case Screen::Settings:
    case Screen::Credits:
    case Screen::Levels:
    case Screen::Load:
    case Screen::Save: open(returnTo_); break;
    case Screen::Pause: action_ = Action::Resume; break;
    case Screen::Main:
        if (started_)
            action_ = Action::Resume;
        break;
    default: break;
    }
}

void GameMenu::mouse(glm::vec2 position, bool leftDown, double scroll)
{
    mouse_ = position;
    pressed_ = leftDown && !down_;
    released_ = !leftDown && down_;
    if (released_ && suppressRelease_) {
        released_ = false;
        suppressRelease_ = false;
    }
    down_ = leftDown;
    scroll_ = scroll;
    if (!down_)
        dragging_.clear();
}

GameMenu::Action GameMenu::takeAction()
{
    const Action a = action_;
    action_ = Action::None;
    return a;
}

bool GameMenu::button(engine::Ui2D& ui, glm::vec2 pos, glm::vec2 size, const std::string& label, bool enabled)
{
    const bool hover = enabled && mouse_.x >= pos.x && mouse_.x < pos.x + size.x && mouse_.y >= pos.y && mouse_.y < pos.y + size.y;
    const float text = std::round(size.y * 0.55f);
    if (hover) {
        ui.rect(pos, size, {0.93f, 0.62f, 0.2f, 0.12f});
        ui.rect(pos, {std::max(3.0f, std::round(size.y * 0.08f)), size.y}, kAmber);
    }
    ui.text(label, {pos.x + size.y * 0.45f, pos.y + (size.y - text) * 0.5f}, text, !enabled ? kDim * glm::vec4(1, 1, 1, 0.6f) : hover ? kAmber : kBone,
            true);
    return hover && released_ && !justOpened_;
}

void GameMenu::draw(engine::Ui2D& ui, int width, int height, double time)
{
    if (screen_ == Screen::None)
        return;
    if (justOpened_ && !down_)
        justOpened_ = false; // a click that opened a screen does not also press its buttons
    if (openedAt_ == 0.0)
        openedAt_ = time;
    const float w = static_cast<float>(width), h = static_cast<float>(height);
    switch (screen_) {
    case Screen::Main: drawMain(ui, w, h, time); break;
    case Screen::Pause: drawPause(ui, w, h); break;
    case Screen::Settings: drawSettings(ui, w, h); break;
    case Screen::Credits: drawCredits(ui, w, h); break;
    case Screen::Death: drawDeath(ui, w, h, time); break;
    case Screen::Levels: drawLevels(ui, w, h); break;
    case Screen::Load: drawSaves(ui, w, h, false); break;
    case Screen::Save: drawSaves(ui, w, h, true); break;
    default: break;
    }
    // Version in the corner of every menu (bug reports).
    const float small = std::round(std::clamp(h * 0.018f, 11.0f, 18.0f));
    const std::string version = std::string("sestavení ") + engine::engineVersion();
    ui.text(version, {w - ui.textWidth(version, small) - small, h - small * 1.8f}, small, kDim);
    pressed_ = released_ = false;
    scroll_ = 0.0;
}

void GameMenu::drawMain(engine::Ui2D& ui, float w, float h, double time)
{
    // Darkened game view with a vignette band on the left for the menu.
    ui.rect({0, 0}, {w, h}, {0.02f, 0.02f, 0.015f, 0.35f});
    const float panelW = std::max(w * 0.34f, 380.0f);
    ui.rect({0, 0}, {panelW, h}, kPanel);
    ui.rect({panelW, 0}, {3.0f, h}, {0.93f, 0.62f, 0.2f, 0.35f});
    const float x = std::round(h * 0.07f);
    float titleSize = std::round(std::clamp(h * 0.075f, 30.0f, 84.0f));
    while (titleSize > 18.0f && ui.textWidth(title_, titleSize, true) > panelW - x * 1.4f) // long level names
        titleSize -= 2.0f;
    // The title flickers once in a while like a failing sign.
    const float flicker = std::fmod(time, 7.0) < 0.12 ? 0.55f : 1.0f;
    ui.text(title_, {x, h * 0.16f}, titleSize, kBone * glm::vec4(flicker, flicker, flicker, 1.0f), true);
    if (!subtitle_.empty())
        ui.text(subtitle_, {x, h * 0.16f + titleSize * 1.15f}, std::round(std::clamp(h * 0.026f, 13.0f, 28.0f)), kAmber);

    const float bh = std::round(std::clamp(h * 0.06f, 34.0f, 64.0f));
    const glm::vec2 size(panelW - x, bh);
    float y = h * 0.42f;
    if (started_ && button(ui, {x - bh * 0.45f, y}, size, "Pokračovat"))
        action_ = Action::Resume;
    if (started_)
        y += bh * 1.1f;
    if (button(ui, {x - bh * 0.45f, y}, size, started_ ? "Začít lokaci znovu" : "Nová hra"))
        action_ = started_ ? Action::Restart : Action::Play;
    y += bh * 1.1f;
    if (levels_.size() > 1) {
        if (button(ui, {x - bh * 0.45f, y}, size, "Vybrat lokaci"))
            open(Screen::Levels);
        y += bh * 1.1f;
    }
    if (button(ui, {x - bh * 0.45f, y}, size, "Načíst hru", !saves_.empty()))
        open(Screen::Load);
    y += bh * 1.1f;
    if (button(ui, {x - bh * 0.45f, y}, size, "Nastavení"))
        open(Screen::Settings);
    y += bh * 1.1f;
    if (button(ui, {x - bh * 0.45f, y}, size, "Autoři"))
        open(Screen::Credits);
    y += bh * 1.1f;
    if (button(ui, {x - bh * 0.45f, y}, size, "Konec"))
        action_ = Action::Quit;
}

void GameMenu::drawPause(engine::Ui2D& ui, float w, float h)
{
    ui.rect({0, 0}, {w, h}, kShade);
    const float bh = std::round(std::clamp(h * 0.055f, 32.0f, 58.0f));
    const glm::vec2 panel(std::max(w * 0.26f, 340.0f), bh * 9.8f);
    const glm::vec2 pos = glm::round((glm::vec2(w, h) - panel) * 0.5f);
    ui.rect(pos, panel, kPanel);
    ui.rect(pos, {panel.x, 3.0f}, kAmber);
    const float title = std::round(bh * 0.7f);
    ui.text("PAUZA", {pos.x + bh * 0.6f, pos.y + bh * 0.5f}, title, kBone, true);
    const glm::vec2 size(panel.x - bh * 0.3f, bh);
    float y = pos.y + bh * 1.7f;
    const float x = pos.x + bh * 0.15f;
    if (button(ui, {x, y}, size, "Pokračovat"))
        action_ = Action::Resume;
    y += bh * 1.1f;
    if (button(ui, {x, y}, size, "Uložit hru", canSave_))
        open(Screen::Save);
    y += bh * 1.1f;
    if (button(ui, {x, y}, size, "Načíst hru", !saves_.empty()))
        open(Screen::Load);
    y += bh * 1.1f;
    if (button(ui, {x, y}, size, "Nastavení"))
        open(Screen::Settings);
    y += bh * 1.1f;
    if (button(ui, {x, y}, size, "Autoři"))
        open(Screen::Credits);
    y += bh * 1.1f;
    if (button(ui, {x, y}, size, "Hlavní menu"))
        action_ = Action::MainMenu;
    y += bh * 1.1f;
    if (button(ui, {x, y}, size, "Ukončit hru"))
        action_ = Action::Quit;
}

void GameMenu::drawSettings(engine::Ui2D& ui, float w, float h)
{
    ui.rect({0, 0}, {w, h}, kShade);
    const float row = std::round(std::clamp(h * 0.055f, 30.0f, 56.0f));
    const float text = std::round(row * 0.45f);
    const glm::vec2 panel(std::min(w - row, std::max(w * 0.5f, 640.0f)), std::min(h - row, row * 13.5f));
    const glm::vec2 pos = glm::round((glm::vec2(w, h) - panel) * 0.5f);
    ui.rect(pos, panel, kPanel);
    ui.rect(pos, {panel.x, 3.0f}, kAmber);
    ui.text("NASTAVENÍ", {pos.x + row * 0.6f, pos.y + row * 0.4f}, std::round(row * 0.6f), kBone, true);

    // Which settings exist (registered by the game) and on which page.
    engine::CVarRegistry& cvars = engine::CVarRegistry::global();
    std::vector<std::pair<const engine::CVar*, const SettingInfo*>> pages[4];
    for (const SettingInfo& info : settingInfos())
        if (const engine::CVar* v = cvars.find(info.cvar))
            pages[info.page].push_back({v, &info});
    for (const engine::CVar* v : cvars.list())
        if ((v->flags & engine::CVarConfig) && !(v->flags & engine::CVarDev) &&
            std::none_of(settingInfos().begin(), settingInfos().end(), [&](const SettingInfo& i) { return v->name == i.cvar; }))
            pages[v->name.rfind("in_", 0) == 0 ? 1 : 3].push_back({v, nullptr}); // key bindings: Controls

    // Page tabs.
    float tx = pos.x + row * 0.6f;
    const float tabY = pos.y + row * 1.4f;
    for (int p = 0; p < 4; ++p) {
        if (pages[p].empty())
            continue;
        const float tw = ui.textWidth(kPages[p], text, true) + row * 0.8f;
        const bool on = settingsPage_ == p;
        const bool hover = mouse_.x >= tx && mouse_.x < tx + tw && mouse_.y >= tabY && mouse_.y < tabY + row * 0.8f;
        if (on)
            ui.rect({tx, tabY + row * 0.75f}, {tw, 3.0f}, kAmber);
        ui.text(kPages[p], {tx + row * 0.4f, tabY + row * 0.15f}, text, on ? kAmber : hover ? kBone : kDim, true);
        if (hover && released_ && settingsPage_ != p) {
            settingsPage_ = p;
            settingsScroll_ = 0;
            capturing_.clear();
        }
        tx += tw + row * 0.2f;
    }
    if (pages[settingsPage_].empty())
        settingsPage_ = 0;

    // Rows: label on the left, control on the right; the wheel scrolls long pages.
    const int visibleRows = std::max(1, static_cast<int>((panel.y - row * 4.2f) / row));
    const int pageRows = static_cast<int>(pages[settingsPage_].size());
    settingsScroll_ = std::clamp(settingsScroll_ - static_cast<int>(scroll_), 0, std::max(0, pageRows - visibleRows));
    if (pageRows > visibleRows) {
        char more[48];
        std::snprintf(more, sizeof more, "%d-%d z %d", settingsScroll_ + 1, std::min(pageRows, settingsScroll_ + visibleRows), pageRows);
        ui.text(more, {pos.x + panel.x - ui.textWidth(more, text * 0.85f) - row * 0.8f, tabY + row * 0.2f}, text * 0.85f, kDim);
    }
    float y = pos.y + row * 2.6f;
    int rowIndex = -1;
    const float labelX = pos.x + row * 0.8f;
    const float controlX = pos.x + panel.x * 0.55f, controlW = panel.x * 0.45f - row * 0.8f;
    for (const auto& [v, info] : pages[settingsPage_]) {
        if (++rowIndex < settingsScroll_)
            continue;
        const std::string label = info ? info->label : v->description;
        ui.text(label, {labelX, y + (row - text) * 0.5f}, text, kBone);
        const float cy = y + row * 0.5f;
        const bool hoverRow = mouse_.y >= y && mouse_.y < y + row && mouse_.x >= controlX && mouse_.x < controlX + controlW;
        if (v->type == engine::CVarType::Bool) {
            const float box = std::round(row * 0.45f);
            const glm::vec2 b(controlX, cy - box * 0.5f);
            ui.rect(b, {box, box}, hoverRow ? glm::vec4(0.3f, 0.28f, 0.22f, 1.0f) : kTrack);
            if (v->boolean())
                ui.rect(b + glm::vec2(box * 0.22f), glm::vec2(box * 0.56f), kAmber);
            ui.text(v->boolean() ? "Zapnuto" : "Vypnuto", {b.x + box * 1.6f, y + (row - text) * 0.5f}, text, kDim);
            if (hoverRow && released_)
                cvars.set(v->name, v->boolean() ? "0" : "1");
        } else if (info && !info->choices.empty() && v->type == engine::CVarType::Int) {
            // Named choices: < Value >
            const int value = v->integer();
            const int count = static_cast<int>(info->choices.size());
            const std::string name = value >= 0 && value < count ? info->choices[static_cast<std::size_t>(value)] : v->value;
            const float arrow = row * 0.8f;
            const bool left = hoverRow && mouse_.x < controlX + arrow, right = hoverRow && mouse_.x > controlX + controlW - arrow;
            ui.text("<", {controlX + arrow * 0.3f, y + (row - text) * 0.5f}, text, left ? kAmber : kDim, true);
            ui.text(">", {controlX + controlW - arrow * 0.6f, y + (row - text) * 0.5f}, text, right ? kAmber : kDim, true);
            ui.text(name, {controlX + (controlW - ui.textWidth(name, text, true)) * 0.5f, y + (row - text) * 0.5f}, text, kBone, true);
            if (released_ && (left || right))
                cvars.set(v->name, std::to_string(std::clamp(value + (right ? 1 : -1), 0, count - 1)));
        } else if (v->type == engine::CVarType::Float || v->type == engine::CVarType::Int) {
            // Slider over the variable's range; the value is printed after it.
            const float valueW = row * 1.8f;
            const float trackW = controlW - valueW;
            const glm::vec2 t(controlX, cy - 2.0f);
            const double lo = v->min, hi = v->max > v->min ? v->max : v->min + 1.0;
            const float f = static_cast<float>(std::clamp((v->number() - lo) / (hi - lo), 0.0, 1.0));
            ui.rect(t, {trackW, 4.0f}, kTrack);
            ui.rect(t, {trackW * f, 4.0f}, kAmber);
            const float knob = std::round(row * 0.32f);
            ui.rect({t.x + trackW * f - knob * 0.5f, cy - knob * 0.5f}, {knob, knob}, hoverRow || dragging_ == v->name ? kAmber : kBone);
            ui.text(formatValue(*v), {controlX + trackW + row * 0.4f, y + (row - text) * 0.5f}, text, kDim);
            if (hoverRow && pressed_ && mouse_.x < controlX + trackW + knob)
                dragging_ = v->name;
            if (dragging_ == v->name) {
                const double k = std::clamp(static_cast<double>((mouse_.x - t.x) / trackW), 0.0, 1.0);
                double value = lo + k * (hi - lo);
                if (v->type == engine::CVarType::Int)
                    value = std::round(value);
                else if (hi - lo > 20.0)
                    value = std::round(value);
                else
                    value = std::round(value * 100.0) / 100.0;
                char buf[32];
                std::snprintf(buf, sizeof buf, "%g", value);
                if (buf != v->value)
                    cvars.set(v->name, buf);
            }
        } else if (v->name.rfind("in_", 0) == 0) {
            // Key binding: click, then press the key (or a mouse button); Esc cancels.
            const bool waiting = capturing_ == v->name;
            const glm::vec2 b(controlX, y + row * 0.12f), bs(controlW, row * 0.76f);
            ui.rect(b, bs, waiting ? glm::vec4(0.93f, 0.62f, 0.2f, 0.25f) : hoverRow ? glm::vec4(0.3f, 0.28f, 0.22f, 1.0f) : kTrack);
            const std::string shown = waiting ? "Stiskni klávesu…" : keyLabel(keyCode(v->value));
            ui.text(shown, {b.x + row * 0.3f, y + (row - text) * 0.5f}, text, waiting ? kAmber : kBone, !waiting);
            if (hoverRow && released_ && !waiting && capturing_.empty())
                capturing_ = v->name;
        } else {
            ui.text(v->value, {controlX, y + (row - text) * 0.5f}, text, kDim);
        }
        y += row;
        if (rowIndex + 1 >= settingsScroll_ + visibleRows)
            break;
    }

    const glm::vec2 bsize(row * 3.2f, row * 0.9f);
    if (button(ui, {pos.x + panel.x - bsize.x - row * 0.3f, pos.y + panel.y - bsize.y - row * 0.3f}, bsize, "Zpět")) {
        const std::string file = (engine::userDirectory("config") / "game.cfg").string();
        cvars.saveConfig(file); // saved now, not only at exit
        open(returnTo_);
    }
    if (button(ui, {pos.x + row * 0.3f, pos.y + panel.y - bsize.y - row * 0.3f}, {row * 4.5f, row * 0.9f}, "Výchozí")) {
        for (const auto& [v, info] : pages[settingsPage_])
            cvars.set(v->name, v->defaultValue);
    }
}

void GameMenu::drawCredits(engine::Ui2D& ui, float w, float h)
{
    ui.rect({0, 0}, {w, h}, {0.03f, 0.03f, 0.025f, 0.94f});
    const float line = std::round(std::clamp(h * 0.024f, 13.0f, 24.0f));
    const float x = std::round(w * 0.08f);
    const float top = line * 6.0f, bottom = h - line * 4.5f;
    ui.text("AUTOŘI", {x, line * 1.5f}, std::round(line * 1.8f), kBone, true);
    ui.text("Modely, textury, zvuky a knihovny těchto autorů, použité podle jejich licencí.", {x, line * 3.8f}, line * 0.85f, kDim);

    float total = 0.0f;
    for (const CreditLine& c : credits_)
        total += c.kind == 0 ? line * 1.6f : c.kind == 2 ? line * 0.6f : line * 1.2f;
    const float maxScroll = std::max(0.0f, total - (bottom - top));
    creditsScroll_ = std::clamp(creditsScroll_ - static_cast<float>(scroll_) * line * 3.0f, 0.0f, maxScroll);
    float y = top - creditsScroll_;
    const float maxWidth = w - x * 2.0f;
    for (const CreditLine& c : credits_) {
        const float step = c.kind == 0 ? line * 1.6f : c.kind == 2 ? line * 0.6f : line * 1.2f;
        if (y + step > top && y < bottom) {
            if (c.kind == 0) {
                ui.text(c.text, {x, y}, line * 1.15f, kAmber, true);
            } else if (c.kind == 1) {
                // Long rows are cut with an ellipsis rather than wrapped.
                std::string t = c.text;
                const float size = line * 0.85f;
                while (t.size() > 4 && ui.textWidth(t, size) > maxWidth) {
                    t.resize(t.size() - 4);
                    while (!t.empty() && (static_cast<unsigned char>(t.back()) & 0xC0) == 0x80) // whole UTF-8 characters
                        t.pop_back();
                    if (!t.empty())
                        t.pop_back();
                    t += "...";
                }
                ui.text(t, {x + line, y}, size, kBone);
            }
        }
        y += step;
    }
    // Fade the cut edges.
    ui.rect({0, top - line}, {w, line}, {0.03f, 0.03f, 0.025f, 0.8f});
    ui.rect({0, bottom}, {w, line}, {0.03f, 0.03f, 0.025f, 0.8f});
    if (maxScroll > 0.0f)
        ui.text("Posouvání kolečkem myši", {x, bottom + line * 1.2f}, line * 0.8f, kDim);
    const glm::vec2 bsize(line * 6.0f, line * 2.0f);
    if (button(ui, {w - x - bsize.x, bottom + line * 0.8f}, bsize, "Zpět"))
        open(returnTo_);
}

void GameMenu::drawDeath(engine::Ui2D& ui, float w, float h, double time)
{
    // Red-black fade in over about a second, then the choices.
    const float t = static_cast<float>(std::clamp((time - openedAt_) / 1.2, 0.0, 1.0));
    ui.rect({0, 0}, {w, h}, {0.18f * t, 0.01f, 0.01f, 0.65f * t});
    const float big = std::round(std::clamp(h * 0.08f, 32.0f, 96.0f));
    const std::string title = "SMRT";
    ui.text(title, {std::round((w - ui.textWidth(title, big, true)) * 0.5f), h * 0.3f}, big, glm::vec4(0.85f, 0.2f, 0.15f, t), true);
    if (t < 1.0f)
        return;
    const float bh = std::round(std::clamp(h * 0.055f, 32.0f, 58.0f));
    const glm::vec2 size(std::max(w * 0.22f, 300.0f), bh);
    const float x = std::round((w - size.x) * 0.5f);
    float y = h * 0.3f + big * 2.0f;
    if (button(ui, {x, y}, size, "Zkusit znovu"))
        action_ = Action::Respawn;
    y += bh * 1.1f;
    if (button(ui, {x, y}, size, "Začít lokaci znovu"))
        action_ = Action::Restart;
    y += bh * 1.1f;
    const SaveInfo* last = nullptr;
    for (const SaveInfo& s : saves_)
        if (s.valid) {
            last = &s;
            break;
        }
    if (button(ui, {x, y}, size, "Načíst poslední uložení", last != nullptr)) {
        chosenSave_ = last->file;
        action_ = Action::LoadSave;
    }
    y += bh * 1.1f;
    if (button(ui, {x, y}, size, "Hlavní menu"))
        action_ = Action::MainMenu;
}

void GameMenu::drawLevels(engine::Ui2D& ui, float w, float h)
{
    ui.rect({0, 0}, {w, h}, kShade);
    const float row = std::round(std::clamp(h * 0.07f, 40.0f, 72.0f));
    const float text = std::round(row * 0.38f);
    const glm::vec2 panel(std::min(w - row, std::max(w * 0.5f, 640.0f)), std::min(h - row, row * (static_cast<float>(levels_.size()) + 2.8f)));
    const glm::vec2 pos = glm::round((glm::vec2(w, h) - panel) * 0.5f);
    ui.rect(pos, panel, kPanel);
    ui.rect(pos, {panel.x, 3.0f}, kAmber);
    ui.text("VYBRAT LOKACI", {pos.x + row * 0.4f, pos.y + row * 0.3f}, std::round(row * 0.45f), kBone, true);
    float y = pos.y + row * 1.2f;
    for (const LevelEntry& level : levels_) {
        if (y > pos.y + panel.y - row * 1.6f)
            break;
        const glm::vec2 p(pos.x + row * 0.15f, y), size(panel.x - row * 0.3f, row * 0.95f);
        const bool hover = mouse_.x >= p.x && mouse_.x < p.x + size.x && mouse_.y >= p.y && mouse_.y < p.y + size.y;
        if (hover) {
            ui.rect(p, size, {0.93f, 0.62f, 0.2f, 0.12f});
            ui.rect(p, {3.0f, size.y}, kAmber);
        }
        ui.text(level.title, {p.x + row * 0.3f, p.y + row * 0.12f}, text, hover ? kAmber : kBone, true);
        const std::string sub = level.file.filename().string() + (level.hint.empty() ? "" : "  ·  " + level.hint);
        ui.text(sub, {p.x + row * 0.3f, p.y + row * 0.12f + text * 1.25f}, std::round(text * 0.8f), kDim);
        if (hover && released_ && !justOpened_) {
            chosenLevel_ = level.file;
            action_ = Action::LoadLevel;
        }
        y += row;
    }
    const glm::vec2 bsize(row * 2.4f, row * 0.7f);
    if (button(ui, {pos.x + panel.x - bsize.x - row * 0.2f, pos.y + panel.y - bsize.y - row * 0.2f}, bsize, "Zpět"))
        open(returnTo_);
}

void GameMenu::drawSaves(engine::Ui2D& ui, float w, float h, bool saving)
{
    ui.rect({0, 0}, {w, h}, kShade);
    const float row = std::round(std::clamp(h * 0.07f, 40.0f, 72.0f));
    const float text = std::round(row * 0.38f);
    // Saving: the three numbered slots (filled or empty). Loading: every save, newest first.
    std::vector<SaveInfo> rows;
    if (saving) {
        for (const char* slot : {"slot1", "slot2", "slot3"}) {
            auto it = std::find_if(saves_.begin(), saves_.end(), [&](const SaveInfo& s) { return s.slot == slot; });
            rows.push_back(it != saves_.end() ? *it : SaveInfo{slot, saveFile(slot), "", "", false});
        }
    } else {
        rows = saves_;
    }
    const glm::vec2 panel(std::min(w - row, std::max(w * 0.45f, 600.0f)), row * (static_cast<float>(std::max<std::size_t>(rows.size(), 1)) + 2.8f));
    const glm::vec2 pos = glm::round((glm::vec2(w, h) - panel) * 0.5f);
    ui.rect(pos, panel, kPanel);
    ui.rect(pos, {panel.x, 3.0f}, kAmber);
    ui.text(saving ? "ULOŽIT HRU" : "NAČÍST HRU", {pos.x + row * 0.4f, pos.y + row * 0.3f}, std::round(row * 0.45f), kBone, true);
    float y = pos.y + row * 1.2f;
    if (rows.empty())
        ui.text("Zatím žádné uložené hry.", {pos.x + row * 0.45f, y + row * 0.25f}, text, kDim);
    for (const SaveInfo& s : rows) {
        const bool usable = saving || s.valid;
        const glm::vec2 p(pos.x + row * 0.15f, y), size(panel.x - row * 0.3f, row * 0.95f);
        const bool hover = usable && mouse_.x >= p.x && mouse_.x < p.x + size.x && mouse_.y >= p.y && mouse_.y < p.y + size.y;
        if (hover) {
            ui.rect(p, size, {0.93f, 0.62f, 0.2f, 0.12f});
            ui.rect(p, {3.0f, size.y}, kAmber);
        }
        const bool empty = saving && s.title.empty() && !s.valid;
        const std::string head = slotLabel(s.slot) + (empty ? "  ·  prázdná" : "  ·  " + s.title);
        ui.text(head, {p.x + row * 0.3f, p.y + row * 0.12f}, text, hover ? kAmber : usable ? kBone : kDim, true);
        if (!s.created.empty())
            ui.text(s.created, {p.x + row * 0.3f, p.y + row * 0.12f + text * 1.25f}, std::round(text * 0.8f), kDim);
        if (hover && released_ && !justOpened_) {
            if (saving) {
                chosenSlot_ = s.slot;
                action_ = Action::SaveSlot;
            } else {
                chosenSave_ = s.file;
                action_ = Action::LoadSave;
            }
        }
        y += row;
    }
    const glm::vec2 bsize(row * 2.4f, row * 0.7f);
    if (button(ui, {pos.x + panel.x - bsize.x - row * 0.2f, pos.y + panel.y - bsize.y - row * 0.2f}, bsize, "Zpět"))
        open(returnTo_);
}
