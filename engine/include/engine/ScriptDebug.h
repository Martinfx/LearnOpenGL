#pragma once
// Lua debugger: breakpoints, stepping and a look at the variables of a paused script.
//
// Attached to one lua_State (the ScriptSystem's). While no breakpoint is set and nothing is
// stepping, no hook is installed and scripts run at full speed. When a script reaches a
// breakpoint (or the next line after a step / pause request) the hook gathers the call stack
// with its locals, upvalues and the entity's props, and calls the pause handler, which blocks
// until the user decides how to go on (the game keeps its window alive and waits for the
// editor). Files are matched by their path below "assets/" ("scripts/enemy.lua"), so the
// editor's source tree and the game's copy of the assets name the same breakpoints.
#include <filesystem>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

struct lua_State;
struct lua_Debug;

namespace engine {

struct ScriptDebugVar {
    std::string scope; // "local", "upvalue", "props"
    std::string name, type, value;
};

struct ScriptDebugFrame {
    std::string function; // "on_update", "?" for the main chunk
    std::string file;     // breakpoint key ("scripts/enemy.lua"), "[C]" for native code
    int line = 0;
    std::vector<ScriptDebugVar> vars;
};

struct ScriptDebugPause {
    std::string reason; // "breakpoint", "step", "pause", "error"
    std::string file;
    int line = 0;
    std::string entity; // entity whose script runs, "" = unknown
    std::vector<ScriptDebugFrame> frames; // innermost first
};

enum class ScriptDebugAction { Continue, StepInto, StepOver, StepOut };

class ScriptDebugger {
public:
    using PauseFn = std::function<ScriptDebugAction(const ScriptDebugPause&)>;
    // Text for a userdata value (the engine's entity objects), empty = generic.
    using DescribeFn = std::function<std::string(lua_State*, int index)>;

    explicit ScriptDebugger(lua_State* L);
    ~ScriptDebugger();
    ScriptDebugger(const ScriptDebugger&) = delete;
    ScriptDebugger& operator=(const ScriptDebugger&) = delete;

    void setPauseHandler(PauseFn fn) { pause_ = std::move(fn); }
    void setDescriber(DescribeFn fn) { describe_ = std::move(fn); }
    void setEntity(std::string name) { entity_ = std::move(name); } // whose script runs now

    // `file` may be any path to the script; it is reduced to its key.
    void setBreakpoint(const std::string& file, int line, bool on);
    void clearBreakpoints();
    const std::map<std::string, std::set<int>>& breakpoints() const { return breakpoints_; }
    // Stops at the next line any script runs.
    void requestPause();

    // "scripts/enemy.lua" from "/x/assets/scripts/enemy.lua", "@...", "C:\\x\\assets\\scripts\\enemy.lua".
    static std::string fileKey(std::string path);
    // One-line text of a Lua value (tables: their first fields).
    static std::string valueText(lua_State* L, int index, int depth = 1, const DescribeFn* describe = nullptr);

    // Protocol lines (game stdout -> editor), "@dbg ..." with quoted fields; parsePause reads them back.
    static std::vector<std::string> pauseLines(const ScriptDebugPause& pause);
    // Feeds one line; true when the pause is complete ("@dbg end") and stored in `out`.
    static bool parsePauseLine(const std::string& line, ScriptDebugPause& out);

private:
    static void hook(lua_State* L, lua_Debug* ar);
    void onLine(lua_State* L, lua_Debug* ar);
    void updateHook();
    int depth(lua_State* L) const;
    ScriptDebugPause gather(lua_State* L, const std::string& reason);

    lua_State* L_;
    PauseFn pause_;
    DescribeFn describe_;
    std::string entity_;
    std::map<std::string, std::set<int>> breakpoints_;
    enum class Step { None, Into, Over, Out, Pause } step_ = Step::None;
    int stepDepth_ = 0;
    lua_State* stepThread_ = nullptr;
    int lastLine_ = -1;
    std::string lastFile_;
    bool inPause_ = false;
};

} // namespace engine
