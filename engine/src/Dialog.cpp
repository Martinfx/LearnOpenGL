#include "engine/Dialog.h"
#include "engine/Quests.h"

#include <fstream>
#include <iomanip>
#include <set>
#include <sstream>

namespace engine {

namespace {

void stripComment(std::string& line)
{
    bool quoted = false;
    for (std::size_t i = 0; i < line.size(); ++i) {
        if (line[i] == '"' && (i == 0 || line[i - 1] != '\\'))
            quoted = !quoted;
        else if (line[i] == '#' && !quoted) {
            line.erase(i);
            return;
        }
    }
}

std::string q(const std::string& s)
{
    std::ostringstream o;
    o << std::quoted(s);
    return o.str();
}

std::string word(const std::string& w)
{
    return !w.empty() && w.find_first_of(" \t\"#") == std::string::npos ? w : q(w);
}

const std::set<std::string> kQuestActions = {"start_quest", "advance_quest", "complete_quest", "fail_quest", "quest_stage", "set_flag",
                                             "clear_flag"};
const std::set<std::string> kGameActions = {"give", "fire", "hostile", "trade"};

} // namespace

const DialogNode* DialogDef::node(std::string_view id) const
{
    for (const DialogNode& n : nodes)
        if (n.id == id)
            return &n;
    return nullptr;
}

bool loadDialog(DialogDef& dialog, const std::filesystem::path& file, std::string* error)
{
    std::ifstream in(file);
    if (!in) {
        if (error)
            *error = "cannot open " + file.string();
        return false;
    }
    DialogDef d;
    d.file = file;
    std::string line;
    int lineNo = 0;
    auto fail = [&](const std::string& what) {
        if (error)
            *error = file.filename().string() + ":" + std::to_string(lineNo) + ": " + what;
        return false;
    };
    while (std::getline(in, line)) {
        ++lineNo;
        stripComment(line);
        std::istringstream s(line);
        std::string key;
        if (!(s >> key))
            continue;
        std::vector<std::string> w;
        for (std::string x; s >> std::quoted(x);)
            w.push_back(x);
        DialogNode* node = d.nodes.empty() ? nullptr : &d.nodes.back();
        DialogChoice* choice = node && !node->choices.empty() ? &node->choices.back() : nullptr;
        if (key == "speaker") {
            d.speaker = w.empty() ? std::string() : w[0];
        } else if (key == "node") {
            if (w.empty() || w[0].empty())
                return fail("node needs an id");
            if (d.node(w[0]))
                return fail("node " + w[0] + " twice");
            d.nodes.push_back({});
            d.nodes.back().id = w[0];
        } else if (!node) {
            return fail(key + " before the first node");
        } else if (key == "text") {
            node->text = w.empty() ? std::string() : w[0];
        } else if (key == "choice") {
            if (w.empty())
                return fail("choice needs a text");
            node->choices.push_back({w[0], w.size() > 1 ? w[1] : std::string(), {}, {}});
        } else if (key == "if") {
            DialogCondition c;
            if (!w.empty() && w[0] == "not") {
                c.negate = true;
                w.erase(w.begin());
            }
            if (w.empty())
                return fail("if needs a condition");
            c.words = std::move(w);
            (choice ? choice->conditions : node->conditions).push_back(std::move(c));
        } else if (key == "do") {
            if (w.empty())
                return fail("do needs an action");
            (choice ? choice->actions : node->actions).push_back({std::move(w)});
        } else {
            return fail("unknown key " + key);
        }
    }
    if (d.nodes.empty())
        return fail("a dialog needs at least one node");
    dialog = std::move(d);
    return true;
}

bool saveDialog(const DialogDef& d, const std::filesystem::path& file, std::string* error)
{
    std::ofstream out(file);
    if (!out) {
        if (error)
            *error = "cannot write " + file.string();
        return false;
    }
    auto words = [](const std::vector<std::string>& w) {
        std::string s;
        for (const std::string& x : w)
            s += ' ' + word(x);
        return s;
    };
    out << "# Dialog (editor: Assets > Dialogs). The talk starts at the first node whose conditions hold.\n";
    out << "speaker " << q(d.speaker) << '\n';
    for (const DialogNode& n : d.nodes) {
        out << "\nnode " << q(n.id) << '\n';
        for (const DialogCondition& c : n.conditions)
            out << "  if" << (c.negate ? " not" : "") << words(c.words) << '\n';
        out << "  text " << q(n.text) << '\n';
        for (const DialogAction& a : n.actions)
            out << "  do" << words(a.words) << '\n';
        for (const DialogChoice& c : n.choices) {
            out << "  choice " << q(c.text) << ' ' << q(c.next) << '\n';
            for (const DialogCondition& k : c.conditions)
                out << "    if" << (k.negate ? " not" : "") << words(k.words) << '\n';
            for (const DialogAction& a : c.actions)
                out << "    do" << words(a.words) << '\n';
        }
    }
    return static_cast<bool>(out);
}

std::vector<std::string> checkDialog(const DialogDef& d, const Quests* quests)
{
    std::vector<std::string> out;
    auto checkQuest = [&](const std::string& where, const std::vector<std::string>& w, std::size_t at) {
        if (quests && w.size() > at && !quests->find(w[at]))
            out.push_back(where + ": no quest " + w[at]);
    };
    auto checkCondition = [&](const std::string& where, const DialogCondition& c) {
        if (c.words[0] == "quest") {
            if (c.words.size() < 3)
                out.push_back(where + ": quest needs an id and a state");
            checkQuest(where, c.words, 1);
            if (quests && c.words.size() >= 3)
                if (const QuestDef* qd = quests->find(c.words[1]); qd && !qd->stage(c.words[2]) && c.words[2] != "none" &&
                                                                  c.words[2] != "active" && c.words[2] != "done" && c.words[2] != "failed")
                    out.push_back(where + ": quest " + c.words[1] + " has no stage " + c.words[2]);
        } else if (c.words[0] != "flag" || c.words.size() < 2) {
            out.push_back(where + ": unknown condition " + c.words[0]);
        }
    };
    auto checkAction = [&](const std::string& where, const DialogAction& a) {
        const std::string& k = a.words[0];
        if (!kQuestActions.count(k) && !kGameActions.count(k))
            out.push_back(where + ": unknown action " + k);
        else if (k.ends_with("_quest") || k == "quest_stage")
            checkQuest(where, a.words, 1);
    };
    for (const DialogNode& n : d.nodes) {
        const std::string at = "node " + n.id;
        for (const DialogCondition& c : n.conditions)
            checkCondition(at, c);
        for (const DialogAction& a : n.actions)
            checkAction(at, a);
        for (const DialogChoice& c : n.choices) {
            const std::string where = at + " \"" + c.text + "\"";
            if (!c.next.empty() && !d.node(c.next))
                out.push_back(where + ": no node " + c.next);
            for (const DialogCondition& k : c.conditions)
                checkCondition(where, k);
            for (const DialogAction& a : c.actions)
                checkAction(where, a);
        }
    }
    return out;
}

bool dialogConditionsHold(const std::vector<DialogCondition>& conditions, const Quests& quests)
{
    for (const DialogCondition& c : conditions) {
        bool holds = false;
        if (c.words[0] == "quest" && c.words.size() >= 3)
            holds = quests.is(c.words[1], c.words[2]);
        else if (c.words[0] == "flag" && c.words.size() >= 2)
            holds = quests.flag(c.words[1]);
        if (holds == c.negate)
            return false;
    }
    return true;
}

std::vector<DialogAction> runDialogActions(const std::vector<DialogAction>& actions, Quests& quests)
{
    std::vector<DialogAction> game;
    for (const DialogAction& a : actions) {
        const std::string& k = a.words[0];
        const std::string arg = a.words.size() > 1 ? a.words[1] : std::string();
        if (k == "start_quest")
            quests.start(arg);
        else if (k == "advance_quest")
            quests.advance(arg);
        else if (k == "complete_quest")
            quests.complete(arg);
        else if (k == "fail_quest")
            quests.fail(arg);
        else if (k == "quest_stage" && a.words.size() > 2)
            quests.setStage(arg, a.words[2]);
        else if (k == "set_flag")
            quests.setFlag(arg, true);
        else if (k == "clear_flag")
            quests.setFlag(arg, false);
        else
            game.push_back(a);
    }
    return game;
}

void DialogSession::enter(const DialogNode* n, Quests& quests)
{
    node_ = n;
    visible_.clear();
    if (!n)
        return;
    for (const DialogAction& a : runDialogActions(n->actions, quests))
        pending_.push_back(a);
    for (std::size_t i = 0; i < n->choices.size(); ++i)
        if (dialogConditionsHold(n->choices[i].conditions, quests))
            visible_.push_back(static_cast<int>(i));
}

bool DialogSession::start(const DialogDef& dialog, Quests& quests)
{
    dialog_ = &dialog;
    pending_.clear();
    node_ = nullptr;
    for (const DialogNode& n : dialog.nodes)
        if (dialogConditionsHold(n.conditions, quests)) {
            enter(&n, quests);
            return true;
        }
    return false;
}

std::vector<DialogAction> DialogSession::choose(int index, Quests& quests)
{
    std::vector<DialogAction> out = takeGameActions();
    if (!node_ || index < 0 || index >= static_cast<int>(visible_.size())) {
        node_ = nullptr; // no answers: the talk ends
        return out;
    }
    const DialogChoice& c = node_->choices[static_cast<std::size_t>(visible_[static_cast<std::size_t>(index)])];
    for (const DialogAction& a : runDialogActions(c.actions, quests))
        out.push_back(a);
    enter(c.next.empty() ? nullptr : dialog_->node(c.next), quests);
    for (const DialogAction& a : takeGameActions())
        out.push_back(a);
    return out;
}

std::vector<DialogAction> DialogSession::takeGameActions()
{
    std::vector<DialogAction> out;
    out.swap(pending_);
    return out;
}

} // namespace engine
