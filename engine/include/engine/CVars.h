#pragma once
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace engine {

// Console variables: named settings with a type, description and flags, set from
// the in-game console (~), the editor's Cmd field, the command line (+name value),
// Lua (game.cvar / game.set_cvar) and the user config file. Main thread only.
enum CVarFlags : unsigned {
    CVarNone = 0,
    CVarConfig = 1,   // saved in the user config (settings that survive a restart)
    CVarDev = 2,      // developer setting, hidden from `list` unless asked
    CVarCheat = 4,    // only when `cheats 1`
    CVarReadOnly = 8, // shown, not settable
};

enum class CVarType { Bool, Int, Float, String };

struct CVar {
    std::string name;
    std::string description;
    CVarType type = CVarType::Float;
    unsigned flags = CVarNone;
    std::string value;        // current value as text (canonical form)
    std::string defaultValue;
    double min = 0.0, max = 0.0; // numeric range, ignored when min >= max
    std::function<void(const CVar&)> onChange; // after a successful set

    bool boolean() const { return value == "1"; }
    int integer() const;
    float number() const;
};

// Result of a console line: text to show and whether it worked.
struct ConsoleResult {
    bool ok = true;
    std::string text;
};

class CVarRegistry {
public:
    static CVarRegistry& global();

    // Adds a variable (or returns the existing one of that name, keeping its value).
    CVar& add(const std::string& name, CVarType type, const std::string& defaultValue, const std::string& description,
              unsigned flags = CVarNone, double min = 0.0, double max = 0.0);
    CVar* find(const std::string& name);
    const CVar* find(const std::string& name) const;
    // Parses and range-checks `text` for the type; false + reason on a bad value,
    // a read-only variable or a cheat without cheats on. `force` skips the cheat/read-only checks.
    bool set(const std::string& name, const std::string& text, std::string* error = nullptr, bool force = false);
    std::vector<const CVar*> list(const std::string& prefix = {}) const; // sorted by name

    using Command = std::function<ConsoleResult(const std::vector<std::string>& args)>;
    void addCommand(const std::string& name, const std::string& description, Command command, unsigned flags = CVarNone);
    // "name" prints a variable, "name value" sets it, otherwise runs a command.
    void removeCommand(const std::string& name);
    // Drops every onChange except the registry's own (cheats): callbacks often point at
    // objects of one level, which the game destroys when it loads another.
    void clearCallbacks();
    ConsoleResult execute(const std::string& line);
    // Names starting with `prefix` (variables and commands), for Tab completion.
    std::vector<std::string> complete(const std::string& prefix) const;

    // `+name value` pairs from the command line (other arguments are left alone).
    // Returns the arguments that were not consumed.
    std::vector<std::string> applyCommandLine(const std::vector<std::string>& args);
    // User config: `name "value"` lines of the CVarConfig variables.
    bool saveConfig(const std::string& file) const;
    bool loadConfig(const std::string& file);

    bool cheats() const;

private:
    struct CommandEntry {
        std::string description;
        Command command;
        unsigned flags = CVarNone;
    };
    std::map<std::string, CVar> vars_;
    std::map<std::string, CommandEntry> commands_;
};

std::vector<std::string> splitConsoleLine(const std::string& line); // words, "quoted strings" kept whole

} // namespace engine
