#include "engine/CVars.h"
#include "engine/Log.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace engine {

int CVar::integer() const { return static_cast<int>(std::lround(std::strtod(value.c_str(), nullptr))); }
float CVar::number() const { return static_cast<float>(std::strtod(value.c_str(), nullptr)); }

std::vector<std::string> splitConsoleLine(const std::string& line)
{
    std::vector<std::string> words;
    std::size_t i = 0;
    while (i < line.size()) {
        while (i < line.size() && std::isspace(static_cast<unsigned char>(line[i])))
            ++i;
        if (i >= line.size())
            break;
        std::string w;
        if (line[i] == '"') {
            ++i;
            while (i < line.size() && line[i] != '"')
                w += line[i++];
            ++i;
        } else {
            while (i < line.size() && !std::isspace(static_cast<unsigned char>(line[i])))
                w += line[i++];
        }
        words.push_back(std::move(w));
    }
    return words;
}

CVarRegistry& CVarRegistry::global()
{
    static CVarRegistry registry = [] {
        CVarRegistry r;
        r.add("cheats", CVarType::Bool, "0", "Allows cheat variables and commands (god, noclip, give...)", CVarNone);
        r.addCommand("help", "Lists commands and variables: help [prefix]", [](const std::vector<std::string>& a) {
            CVarRegistry& reg = CVarRegistry::global();
            const std::string prefix = a.empty() ? std::string() : a[0];
            std::string out;
            for (const auto& [name, c] : reg.commands_)
                if (name.rfind(prefix, 0) == 0)
                    out += name + "  - " + c.description + "\n";
            for (const CVar* v : reg.list(prefix))
                if (!(v->flags & CVarDev) || !prefix.empty())
                    out += v->name + " = " + v->value + "  - " + v->description + "\n";
            if (!out.empty())
                out.pop_back();
            return ConsoleResult{true, out};
        });
        r.addCommand("reset", "Sets a variable back to its default: reset <name>", [](const std::vector<std::string>& a) {
            CVarRegistry& reg = CVarRegistry::global();
            const CVar* v = a.empty() ? nullptr : reg.find(a[0]);
            if (!v)
                return ConsoleResult{false, "usage: reset <variable>"};
            std::string error;
            const std::string def = v->defaultValue;
            const bool ok = reg.set(a[0], def, &error);
            return ConsoleResult{ok, ok ? a[0] + " = " + def : error};
        });
        return r;
    }();
    return registry;
}

CVar& CVarRegistry::add(const std::string& name, CVarType type, const std::string& defaultValue, const std::string& description,
                        unsigned flags, double min, double max)
{
    auto [it, inserted] = vars_.try_emplace(name);
    CVar& v = it->second;
    if (inserted) {
        v.name = name;
        v.type = type;
        v.value = defaultValue;
    }
    v.description = description;
    v.flags = flags;
    v.defaultValue = defaultValue;
    v.min = min;
    v.max = max;
    return v;
}

CVar* CVarRegistry::find(const std::string& name)
{
    auto it = vars_.find(name);
    return it == vars_.end() ? nullptr : &it->second;
}

const CVar* CVarRegistry::find(const std::string& name) const
{
    auto it = vars_.find(name);
    return it == vars_.end() ? nullptr : &it->second;
}

bool CVarRegistry::cheats() const
{
    const CVar* c = find("cheats");
    return c && c->boolean();
}

bool CVarRegistry::set(const std::string& name, const std::string& text, std::string* error, bool force)
{
    auto fail = [&](const std::string& why) {
        if (error)
            *error = why;
        return false;
    };
    CVar* v = find(name);
    if (!v)
        return fail("unknown variable " + name);
    if (!force && (v->flags & CVarReadOnly))
        return fail(name + " is read-only");
    if (!force && (v->flags & CVarCheat) && !cheats())
        return fail(name + " is a cheat: set cheats 1 first");
    std::string canonical = text;
    switch (v->type) {
    case CVarType::Bool: {
        std::string t = text;
        std::transform(t.begin(), t.end(), t.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (t == "1" || t == "on" || t == "true" || t == "yes")
            canonical = "1";
        else if (t == "0" || t == "off" || t == "false" || t == "no")
            canonical = "0";
        else
            return fail(name + " takes 0 or 1");
        break;
    }
    case CVarType::Int:
    case CVarType::Float: {
        char* end = nullptr;
        double d = std::strtod(text.c_str(), &end);
        if (text.empty() || end == text.c_str() || *end != '\0' || !std::isfinite(d))
            return fail(name + " takes a number");
        if (v->min < v->max)
            d = std::clamp(d, v->min, v->max);
        if (v->type == CVarType::Int) {
            canonical = std::to_string(static_cast<long long>(std::llround(d)));
        } else {
            char buf[32];
            std::snprintf(buf, sizeof buf, "%g", d);
            canonical = buf;
        }
        break;
    }
    case CVarType::String:
        break;
    }
    if (v->value == canonical)
        return true;
    v->value = canonical;
    if (v->onChange)
        v->onChange(*v);
    return true;
}

std::vector<const CVar*> CVarRegistry::list(const std::string& prefix) const
{
    std::vector<const CVar*> out;
    for (const auto& [name, v] : vars_)
        if (name.rfind(prefix, 0) == 0)
            out.push_back(&v);
    return out;
}

void CVarRegistry::addCommand(const std::string& name, const std::string& description, Command command, unsigned flags)
{
    commands_[name] = CommandEntry{description, std::move(command), flags};
}

ConsoleResult CVarRegistry::execute(const std::string& line)
{
    std::vector<std::string> words = splitConsoleLine(line);
    if (words.empty())
        return {};
    const std::string name = words[0];
    words.erase(words.begin());
    if (CVar* v = find(name)) {
        if (words.empty())
            return {true, v->name + " = " + v->value + "  (default " + v->defaultValue + ") - " + v->description};
        std::string value = words[0];
        for (std::size_t i = 1; i < words.size(); ++i) // strings may have spaces
            value += " " + words[i];
        std::string error;
        if (!set(name, value, &error))
            return {false, error};
        return {true, v->name + " = " + v->value};
    }
    if (auto it = commands_.find(name); it != commands_.end()) {
        if ((it->second.flags & CVarCheat) && !cheats())
            return {false, name + " is a cheat: set cheats 1 first"};
        return it->second.command(words);
    }
    return {false, "unknown command or variable: " + name + " (help lists them)"};
}

std::vector<std::string> CVarRegistry::complete(const std::string& prefix) const
{
    std::vector<std::string> out;
    for (const auto& [name, v] : vars_)
        if (name.rfind(prefix, 0) == 0)
            out.push_back(name);
    for (const auto& [name, c] : commands_)
        if (name.rfind(prefix, 0) == 0)
            out.push_back(name);
    std::sort(out.begin(), out.end());
    return out;
}

std::vector<std::string> CVarRegistry::applyCommandLine(const std::vector<std::string>& args)
{
    std::vector<std::string> rest;
    for (std::size_t i = 0; i < args.size(); ++i) {
        if (args[i].size() > 1 && args[i][0] == '+' && i + 1 < args.size()) {
            std::string error;
            if (!set(args[i].substr(1), args[i + 1], &error, true))
                LOG_WARN("cvar", "Command line: %s", error.c_str());
            ++i;
        } else {
            rest.push_back(args[i]);
        }
    }
    return rest;
}

bool CVarRegistry::saveConfig(const std::string& file) const
{
    std::ofstream out(file);
    if (!out)
        return false;
    out << "# User settings (console variables). Edited by the game's settings and console.\n";
    for (const auto& [name, v] : vars_)
        if ((v.flags & CVarConfig) && v.value != v.defaultValue)
            out << name << ' ' << std::quoted(v.value) << '\n';
    return static_cast<bool>(out);
}

bool CVarRegistry::loadConfig(const std::string& file)
{
    std::ifstream in(file);
    if (!in)
        return false;
    std::string line;
    while (std::getline(in, line)) {
        std::istringstream ls(line);
        std::string name, value;
        if (!(ls >> name) || name[0] == '#')
            continue;
        ls >> std::quoted(value);
        const CVar* v = find(name);
        if (!v || !(v->flags & CVarConfig))
            continue; // only settings; unknown names come from newer versions
        std::string error;
        if (!set(name, value, &error, true))
            LOG_WARN("cvar", "%s: %s", file.c_str(), error.c_str());
    }
    return true;
}

} // namespace engine
