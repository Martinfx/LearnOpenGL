#include "engine/ScriptDebug.h"

extern "C" {
#include <lauxlib.h>
#include <lua.h>
}

#include <algorithm>
#include <cstdio>
#include <iomanip>
#include <sstream>
#include <unordered_map>

namespace engine {

namespace {

constexpr int kMaxFrames = 16;
constexpr std::size_t kMaxText = 160;

std::string oneLine(std::string s)
{
    for (char& c : s)
        if (c == '\n' || c == '\r' || c == '\t')
            c = ' ';
    if (s.size() > kMaxText)
        s = s.substr(0, kMaxText - 3) + "...";
    return s;
}

ScriptDebugger*& slot(lua_State* L) { return *static_cast<ScriptDebugger**>(lua_getextraspace(L)); }

} // namespace

ScriptDebugger::ScriptDebugger(lua_State* L)
    : L_(L)
{
    slot(L_) = this;
}

ScriptDebugger::~ScriptDebugger()
{
    lua_sethook(L_, nullptr, 0, 0);
    slot(L_) = nullptr;
}

std::string ScriptDebugger::fileKey(std::string path)
{
    if (!path.empty() && (path[0] == '@' || path[0] == '='))
        path.erase(0, 1);
    std::replace(path.begin(), path.end(), '\\', '/');
    if (const std::size_t a = path.rfind("assets/"); a != std::string::npos)
        return path.substr(a + 7);
    while (path.rfind("./", 0) == 0)
        path.erase(0, 2);
    while (path.rfind("../", 0) == 0)
        path.erase(0, 3);
    return path;
}

void ScriptDebugger::setBreakpoint(const std::string& file, int line, bool on)
{
    const std::string key = fileKey(file);
    if (on) {
        breakpoints_[key].insert(line);
    } else if (auto it = breakpoints_.find(key); it != breakpoints_.end()) {
        it->second.erase(line);
        if (it->second.empty())
            breakpoints_.erase(it);
    }
    updateHook();
}

void ScriptDebugger::clearBreakpoints()
{
    breakpoints_.clear();
    updateHook();
}

void ScriptDebugger::requestPause()
{
    step_ = Step::Pause;
    updateHook();
}

void ScriptDebugger::updateHook()
{
    // No hook while nothing can stop: scripts run at full speed.
    if (breakpoints_.empty() && step_ == Step::None)
        lua_sethook(L_, nullptr, 0, 0);
    else
        lua_sethook(L_, &ScriptDebugger::hook, LUA_MASKLINE, 0);
}

void ScriptDebugger::hook(lua_State* L, lua_Debug* ar)
{
    if (ar->event != LUA_HOOKLINE)
        return;
    if (ScriptDebugger* d = slot(L))
        d->onLine(L, ar);
}

int ScriptDebugger::depth(lua_State* L) const
{
    lua_Debug ar;
    int n = 0;
    while (lua_getstack(L, n, &ar))
        ++n;
    return n;
}

void ScriptDebugger::onLine(lua_State* L, lua_Debug* ar)
{
    if (inPause_ || !pause_)
        return;
    lua_getinfo(L, "S", ar);
    if (ar->source[0] != '@' && ar->source[0] != '=')
        return; // code from a string (saved-game data)
    static thread_local std::unordered_map<const char*, std::string> keys; // the source string lives with its function
    auto k = keys.find(ar->source);
    if (k == keys.end())
        k = keys.emplace(ar->source, fileKey(ar->source)).first;
    const std::string& file = k->second;
    const int line = ar->currentline;
    const char* reason = nullptr;
    switch (step_) {
    case Step::Pause:
    case Step::Into:
        reason = step_ == Step::Pause ? "pause" : "step";
        break;
    case Step::Over:
        if (depth(L) <= stepDepth_ || L != stepThread_)
            reason = "step";
        break;
    case Step::Out:
        if (depth(L) < stepDepth_ || L != stepThread_)
            reason = "step";
        break;
    case Step::None:
        break;
    }
    if (!reason)
        if (auto b = breakpoints_.find(file); b != breakpoints_.end() && b->second.count(line))
            reason = "breakpoint";
    if (!reason)
        return;
    const ScriptDebugPause p = gather(L, reason);
    inPause_ = true;
    const ScriptDebugAction action = pause_(p);
    inPause_ = false;
    switch (action) {
    case ScriptDebugAction::Continue: step_ = Step::None; break;
    case ScriptDebugAction::StepInto: step_ = Step::Into; break;
    case ScriptDebugAction::StepOver: step_ = Step::Over; break;
    case ScriptDebugAction::StepOut: step_ = Step::Out; break;
    }
    stepDepth_ = depth(L);
    stepThread_ = L;
    lastFile_ = file;
    lastLine_ = line;
    updateHook();
}

std::string ScriptDebugger::valueText(lua_State* L, int index, int depth, const DescribeFn* describe)
{
    index = lua_absindex(L, index);
    switch (lua_type(L, index)) {
    case LUA_TNIL: return "nil";
    case LUA_TBOOLEAN: return lua_toboolean(L, index) ? "true" : "false";
    case LUA_TNUMBER: {
        char buf[48];
        if (lua_isinteger(L, index))
            std::snprintf(buf, sizeof buf, "%lld", static_cast<long long>(lua_tointeger(L, index)));
        else
            std::snprintf(buf, sizeof buf, "%.6g", static_cast<double>(lua_tonumber(L, index)));
        return buf;
    }
    case LUA_TSTRING: {
        std::size_t n = 0;
        const char* s = lua_tolstring(L, index, &n);
        return oneLine("\"" + std::string(s, std::min<std::size_t>(n, kMaxText)) + "\"");
    }
    case LUA_TTABLE: {
        if (depth <= 0)
            return "{...}";
        std::string out = "{";
        int shown = 0, count = 0;
        lua_pushnil(L);
        while (lua_next(L, index)) {
            if (shown < 6) {
                lua_pushvalue(L, -2); // a copy: tostring on the real key would break lua_next
                std::string key = lua_type(L, -1) == LUA_TSTRING ? lua_tostring(L, -1) : "[" + valueText(L, -1, 0) + "]";
                lua_pop(L, 1);
                out += (shown ? ", " : "") + key + " = " + valueText(L, -1, depth - 1, describe);
                ++shown;
            }
            ++count;
            lua_pop(L, 1);
        }
        if (count > shown)
            out += ", ... (" + std::to_string(count) + ")";
        return oneLine(out + "}");
    }
    case LUA_TFUNCTION: return lua_iscfunction(L, index) ? "native function" : "function";
    case LUA_TUSERDATA:
    case LUA_TLIGHTUSERDATA:
        if (describe && *describe) {
            std::string d = (*describe)(L, index);
            if (!d.empty())
                return oneLine(d);
        }
        return "userdata";
    case LUA_TTHREAD: return "coroutine";
    default: return "?";
    }
}

ScriptDebugPause ScriptDebugger::gather(lua_State* L, const std::string& reason)
{
    ScriptDebugPause p;
    p.reason = reason;
    p.entity = entity_;
    lua_Debug ar;
    for (int level = 0; level < kMaxFrames && lua_getstack(L, level, &ar); ++level) {
        ScriptDebugFrame f;
        lua_getinfo(L, "nSlf", &ar); // pushes the function
        f.line = ar.currentline;
        f.file = ar.what && std::string(ar.what) == "C" ? "[C]" : fileKey(ar.source);
        f.function = ar.name ? ar.name : (ar.what && std::string(ar.what) == "main" ? "main chunk" : "?");
        for (int i = 1;; ++i) {
            const char* name = lua_getlocal(L, &ar, i);
            if (!name)
                break;
            if (name[0] != '(')
                f.vars.push_back({"local", name, luaL_typename(L, -1), valueText(L, -1, 1, &describe_)});
            lua_pop(L, 1);
        }
        for (int i = 1;; ++i) {
            const char* name = lua_getupvalue(L, -1, i);
            if (!name)
                break;
            if (std::string(name) == "_ENV") {
                // The entity's environment: its props, one per row.
                if (lua_type(L, -1) == LUA_TTABLE && f.function == "?") {
                    // Called from the engine (on_update...): the global that holds this function.
                    const int env = lua_gettop(L);
                    lua_pushnil(L);
                    while (lua_next(L, env)) {
                        if (lua_type(L, -2) == LUA_TSTRING && lua_rawequal(L, -1, env - 1))
                            f.function = lua_tostring(L, -2);
                        lua_pop(L, 1);
                    }
                }
                if (lua_type(L, -1) == LUA_TTABLE) {
                    lua_pushstring(L, "props");
                    lua_rawget(L, -2);
                    if (lua_type(L, -1) == LUA_TTABLE) {
                        const int t = lua_gettop(L);
                        lua_pushnil(L);
                        while (lua_next(L, t)) {
                            if (lua_type(L, -2) == LUA_TSTRING)
                                f.vars.push_back({"props", lua_tostring(L, -2), luaL_typename(L, -1), valueText(L, -1, 1, &describe_)});
                            lua_pop(L, 1);
                        }
                    }
                    lua_pop(L, 1);
                }
            } else if (name[0] != '(') {
                f.vars.push_back({"upvalue", name, luaL_typename(L, -1), valueText(L, -1, 1, &describe_)});
            }
            lua_pop(L, 1);
        }
        lua_pop(L, 1); // the function
        if (level == 0) {
            p.file = f.file;
            p.line = f.line;
        }
        p.frames.push_back(std::move(f));
    }
    return p;
}

namespace {
std::string q(const std::string& s)
{
    std::ostringstream o;
    o << std::quoted(s);
    return o.str();
}
} // namespace

std::vector<std::string> ScriptDebugger::pauseLines(const ScriptDebugPause& p)
{
    std::vector<std::string> out;
    out.push_back("@dbg paused " + q(p.reason) + ' ' + q(p.file) + ' ' + std::to_string(p.line) + ' ' + q(p.entity));
    for (const ScriptDebugFrame& f : p.frames) {
        out.push_back("@dbg frame " + q(f.function) + ' ' + q(f.file) + ' ' + std::to_string(f.line));
        for (const ScriptDebugVar& v : f.vars)
            out.push_back("@dbg var " + q(v.scope) + ' ' + q(v.name) + ' ' + q(v.type) + ' ' + q(v.value));
    }
    out.push_back("@dbg end");
    return out;
}

bool ScriptDebugger::parsePauseLine(const std::string& line, ScriptDebugPause& out)
{
    std::istringstream s(line);
    std::string tag, kind;
    if (!(s >> tag >> kind) || tag != "@dbg")
        return false;
    if (kind == "paused") {
        out = {};
        s >> std::quoted(out.reason) >> std::quoted(out.file) >> out.line >> std::quoted(out.entity);
    } else if (kind == "frame") {
        ScriptDebugFrame f;
        s >> std::quoted(f.function) >> std::quoted(f.file) >> f.line;
        out.frames.push_back(std::move(f));
    } else if (kind == "var" && !out.frames.empty()) {
        ScriptDebugVar v;
        s >> std::quoted(v.scope) >> std::quoted(v.name) >> std::quoted(v.type) >> std::quoted(v.value);
        out.frames.back().vars.push_back(std::move(v));
    } else if (kind == "end") {
        return true;
    }
    return false;
}

} // namespace engine
