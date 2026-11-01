#include "engine/AnimGraph.h"

#include <fstream>
#include <iomanip>
#include <map>
#include <mutex>
#include <sstream>

namespace engine {

namespace {

void setError(std::string* error, const std::string& message)
{
    if (error)
        *error = message;
}

const char* paramTypeName(AnimGraph::ParamType t)
{
    return t == AnimGraph::ParamType::Bool ? "bool" : t == AnimGraph::ParamType::Trigger ? "trigger" : "float";
}

const char* stateKindName(AnimGraph::StateKind k)
{
    switch (k) {
    case AnimGraph::StateKind::Blend1D: return "blend1d";
    case AnimGraph::StateKind::Blend2D: return "blend2d";
    case AnimGraph::StateKind::Empty: return "empty";
    default: return "clip";
    }
}

bool parseOp(const std::string& s, AnimGraph::Op& op)
{
    static const std::pair<const char*, AnimGraph::Op> ops[] = {{">", AnimGraph::Op::Greater},   {"<", AnimGraph::Op::Less},
                                                                {">=", AnimGraph::Op::GreaterEq}, {"<=", AnimGraph::Op::LessEq},
                                                                {"==", AnimGraph::Op::Equal},     {"!=", AnimGraph::Op::NotEqual}};
    for (const auto& [text, o] : ops)
        if (s == text) {
            op = o;
            return true;
        }
    return false;
}

std::mutex& cacheMutex()
{
    static std::mutex m;
    return m;
}

std::map<std::filesystem::path, std::weak_ptr<AnimGraph>>& cache()
{
    static std::map<std::filesystem::path, std::weak_ptr<AnimGraph>> c;
    return c;
}

} // namespace

const char* animOpText(AnimGraph::Op op)
{
    switch (op) {
    case AnimGraph::Op::IsFalse: return "!";
    case AnimGraph::Op::Greater: return ">";
    case AnimGraph::Op::Less: return "<";
    case AnimGraph::Op::GreaterEq: return ">=";
    case AnimGraph::Op::LessEq: return "<=";
    case AnimGraph::Op::Equal: return "==";
    case AnimGraph::Op::NotEqual: return "!=";
    default: return "";
    }
}

int AnimGraph::findParam(const std::string& name) const
{
    for (std::size_t i = 0; i < params.size(); ++i)
        if (params[i].name == name)
            return static_cast<int>(i);
    return -1;
}

int AnimGraph::findState(int layer, const std::string& name) const
{
    if (layer < 0 || layer >= static_cast<int>(layers.size()))
        return -1;
    const auto& states = layers[static_cast<std::size_t>(layer)].states;
    for (std::size_t i = 0; i < states.size(); ++i)
        if (states[i].name == name)
            return static_cast<int>(i);
    return -1;
}

bool loadAnimGraphFile(AnimGraph& graph, const std::filesystem::path& file, std::string* error)
{
    std::ifstream in(file);
    if (!in) {
        setError(error, "Cannot open " + file.string());
        return false;
    }
    AnimGraph g;
    g.sourcePath = std::filesystem::absolute(file).lexically_normal();
    // Transitions and entries name states that may come later in the layer: resolved at the end.
    struct PendingTransition {
        int layer;
        std::string from, to;
        AnimGraph::Transition t;
    };
    std::vector<PendingTransition> pending;
    std::vector<std::string> entries;
    std::string line;
    int lineNo = 0;
    auto fail = [&](const std::string& what) {
        setError(error, file.string() + ":" + std::to_string(lineNo) + ": " + what);
        return false;
    };
    while (std::getline(in, line)) {
        ++lineNo;
        std::istringstream ls(line);
        std::string key;
        if (!(ls >> key) || key[0] == '#')
            continue;
        if (key == "param") {
            AnimGraph::Param p;
            std::string type;
            if (!(ls >> std::quoted(p.name) >> type))
                return fail("bad param line");
            p.type = type == "bool" ? AnimGraph::ParamType::Bool : type == "trigger" ? AnimGraph::ParamType::Trigger : AnimGraph::ParamType::Float;
            ls >> p.value;
            g.params.push_back(p);
        } else if (key == "layer") {
            AnimGraph::Layer l;
            if (!(ls >> std::quoted(l.name)))
                return fail("bad layer line");
            ls >> std::quoted(l.maskBone) >> l.weight >> std::quoted(l.weightParam);
            g.layers.push_back(l);
            entries.emplace_back();
        } else if (g.layers.empty()) {
            if (key == "event") {
                AnimGraph::Event e;
                if (!(ls >> std::quoted(e.clip) >> e.time >> std::quoted(e.name)))
                    return fail("bad event line");
                g.events.push_back(e);
                continue;
            }
            return fail("'" + key + "' before any layer");
        } else if (key == "entry") {
            if (!(ls >> std::quoted(entries.back())))
                return fail("bad entry line");
        } else if (key == "state") {
            AnimGraph::State s;
            std::string kind;
            if (!(ls >> std::quoted(s.name) >> kind))
                return fail("bad state line");
            s.kind = kind == "blend1d" ? AnimGraph::StateKind::Blend1D : kind == "blend2d" ? AnimGraph::StateKind::Blend2D
                   : kind == "empty"   ? AnimGraph::StateKind::Empty   : AnimGraph::StateKind::Clip;
            ls >> std::quoted(s.paramX) >> std::quoted(s.paramY);
            std::string word;
            while (ls >> word) {
                if (word == "loop")
                    ls >> s.loop;
                else if (word == "speed")
                    ls >> s.speed;
                else if (word == "at")
                    ls >> s.editorX >> s.editorY;
            }
            g.layers.back().states.push_back(s);
        } else if (key == "sample") {
            if (g.layers.back().states.empty())
                return fail("sample before any state");
            AnimGraph::Sample s;
            if (!(ls >> std::quoted(s.clip)))
                return fail("bad sample line");
            ls >> s.x >> s.y >> s.speed;
            g.layers.back().states.back().samples.push_back(s);
        } else if (key == "transition") {
            PendingTransition p{static_cast<int>(g.layers.size()) - 1, {}, {}, {}};
            if (!(ls >> std::quoted(p.from) >> std::quoted(p.to) >> p.t.duration >> p.t.exitTime))
                return fail("bad transition line");
            std::vector<std::string> tokens;
            for (std::string t; ls >> t;)
                tokens.push_back(t);
            for (std::size_t i = 0; i < tokens.size(); ++i) {
                AnimGraph::Condition c;
                c.param = tokens[i];
                if (!c.param.empty() && c.param[0] == '!') {
                    c.param.erase(0, 1);
                    c.op = AnimGraph::Op::IsFalse;
                } else if (i + 2 < tokens.size() + 0 && parseOp(tokens[i + 1], c.op)) {
                    try {
                        c.value = std::stof(tokens[i + 2]);
                    } catch (...) {
                        return fail("bad condition value");
                    }
                    i += 2;
                }
                p.t.conditions.push_back(c);
            }
            if (p.t.conditions.empty() && p.t.exitTime < 0.0f)
                p.t.exitTime = 1.0f; // no condition: leaves when the clip is done
            pending.push_back(std::move(p));
        } else if (key == "event") {
            AnimGraph::Event e;
            if (!(ls >> std::quoted(e.clip) >> e.time >> std::quoted(e.name)))
                return fail("bad event line");
            g.events.push_back(e);
        }
        // unknown keys: a newer editor wrote them, skipped
    }
    for (std::size_t l = 0; l < g.layers.size(); ++l) {
        const int entry = g.findState(static_cast<int>(l), entries[l]);
        g.layers[l].entry = entry >= 0 ? entry : 0;
    }
    for (PendingTransition& p : pending) {
        p.t.from = p.from == "*" ? -1 : g.findState(p.layer, p.from);
        p.t.to = g.findState(p.layer, p.to);
        if ((p.from != "*" && p.t.from < 0) || p.t.to < 0) {
            setError(error, file.string() + ": transition between unknown states '" + p.from + "' -> '" + p.to + "'");
            return false;
        }
        g.layers[static_cast<std::size_t>(p.layer)].transitions.push_back(p.t);
    }
    graph = std::move(g);
    return true;
}

bool saveAnimGraphFile(const AnimGraph& g, const std::filesystem::path& file, std::string* error)
{
    std::ofstream out(file);
    if (!out) {
        setError(error, "Cannot write " + file.string());
        return false;
    }
    out << std::setprecision(6);
    out << "# Animation graph (editor: Animation editor). Clip names come from the character's glTF file.\n";
    for (const AnimGraph::Param& p : g.params)
        out << "param " << std::quoted(p.name) << ' ' << paramTypeName(p.type) << ' ' << p.value << '\n';
    for (const AnimGraph::Event& e : g.events)
        out << "event " << std::quoted(e.clip) << ' ' << e.time << ' ' << std::quoted(e.name) << '\n';
    for (const AnimGraph::Layer& l : g.layers) {
        out << "\nlayer " << std::quoted(l.name) << ' ' << std::quoted(l.maskBone) << ' ' << l.weight << ' ' << std::quoted(l.weightParam) << '\n';
        if (l.entry >= 0 && l.entry < static_cast<int>(l.states.size()))
            out << "entry " << std::quoted(l.states[static_cast<std::size_t>(l.entry)].name) << '\n';
        for (const AnimGraph::State& s : l.states) {
            out << "state " << std::quoted(s.name) << ' ' << stateKindName(s.kind) << ' ' << std::quoted(s.paramX) << ' ' << std::quoted(s.paramY)
                << " loop " << s.loop << " speed " << s.speed << " at " << s.editorX << ' ' << s.editorY << '\n';
            for (const AnimGraph::Sample& smp : s.samples)
                out << "sample " << std::quoted(smp.clip) << ' ' << smp.x << ' ' << smp.y << ' ' << smp.speed << '\n';
        }
        for (const AnimGraph::Transition& t : l.transitions) {
            const auto name = [&](int i) {
                return i >= 0 && i < static_cast<int>(l.states.size()) ? l.states[static_cast<std::size_t>(i)].name : std::string("*");
            };
            out << "transition " << std::quoted(name(t.from)) << ' ' << std::quoted(name(t.to)) << ' ' << t.duration << ' ' << t.exitTime;
            for (const AnimGraph::Condition& c : t.conditions) {
                if (c.op == AnimGraph::Op::IsTrue)
                    out << ' ' << c.param;
                else if (c.op == AnimGraph::Op::IsFalse)
                    out << " !" << c.param;
                else
                    out << ' ' << c.param << ' ' << animOpText(c.op) << ' ' << c.value;
            }
            out << '\n';
        }
    }
    if (!out) {
        setError(error, "Cannot write " + file.string());
        return false;
    }
    return true;
}

std::shared_ptr<AnimGraph> loadAnimGraph(const std::filesystem::path& file, std::string* error)
{
    const std::filesystem::path key = std::filesystem::absolute(file).lexically_normal();
    const std::lock_guard lock(cacheMutex());
    if (auto it = cache().find(key); it != cache().end())
        if (auto shared = it->second.lock())
            return shared;
    auto g = std::make_shared<AnimGraph>();
    if (!loadAnimGraphFile(*g, key, error))
        return nullptr;
    cache()[key] = g;
    return g;
}

} // namespace engine
