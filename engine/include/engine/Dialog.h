#pragma once
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace engine {

class Quests;

// Conversations: one .dialog file per character (assets/dialogs/), a tree of nodes. A node is
// what the character says and the player's answers; an answer leads to another node or ends
// the talk. The editor shows it as a tree (Assets > Dialogs / double click).
//   speaker "Velitel tábora"
//   node "start"                         the talk begins at the first node whose `if` lines hold
//     if not flag "met_leader"           conditions of the node (before its first choice)
//     text "Nový? Tak to se drž u ohně."
//     do set_flag "met_leader"           actions when the node is shown
//     choice "Máš pro mě práci?" "job"   answer text, next node ("" = end of the talk)
//       if quest "camp_stash" none       conditions of the answer (shown only when all hold)
//       do start_quest "camp_stash"      actions when picked
// Conditions: quest <id> none|active|done|failed|<stage>, flag <name>; "not" in front negates.
// Actions: start_quest, advance_quest, complete_quest, fail_quest <id>; quest_stage <id> <stage>;
// set_flag / clear_flag <name>; and for the game: give medkits <n> | ammo <type> <n> |
// goodwill <faction> <n>, fire <entity> <input> [parameter], hostile (the speaker attacks).
struct DialogCondition {
    bool negate = false;
    std::vector<std::string> words;
    bool operator==(const DialogCondition&) const = default;
};
struct DialogAction {
    std::vector<std::string> words;
    bool operator==(const DialogAction&) const = default;
};
struct DialogChoice {
    std::string text, next;
    std::vector<DialogCondition> conditions;
    std::vector<DialogAction> actions;
    bool operator==(const DialogChoice&) const = default;
};
struct DialogNode {
    std::string id, text;
    std::vector<DialogCondition> conditions; // to start here
    std::vector<DialogAction> actions;       // when shown
    std::vector<DialogChoice> choices;
    bool operator==(const DialogNode&) const = default;
};
struct DialogDef {
    std::string speaker;
    std::vector<DialogNode> nodes;
    std::filesystem::path file;
    const DialogNode* node(std::string_view id) const;
    bool operator==(const DialogDef& o) const { return speaker == o.speaker && nodes == o.nodes; }
};

bool loadDialog(DialogDef& dialog, const std::filesystem::path& file, std::string* error = nullptr);
bool saveDialog(const DialogDef& dialog, const std::filesystem::path& file, std::string* error = nullptr);
// Problems a writer should know about: answers leading to missing nodes, unknown conditions
// or actions, quests that do not exist (`quests` may be null).
std::vector<std::string> checkDialog(const DialogDef& dialog, const Quests* quests);

bool dialogConditionsHold(const std::vector<DialogCondition>& conditions, const Quests& quests);
// Runs the quest and flag actions; returns the ones for the game (give, fire, hostile).
std::vector<DialogAction> runDialogActions(const std::vector<DialogAction>& actions, Quests& quests);

// One conversation in progress.
class DialogSession {
public:
    // Starts at the first node whose conditions hold; false when none does (nothing to say).
    bool start(const DialogDef& dialog, Quests& quests);
    bool active() const { return node_ != nullptr; }
    const DialogNode* node() const { return node_; }
    const std::vector<int>& choices() const { return visible_; } // indices into node()->choices
    // Picks visible answer `index` (0-based among choices()); moves on or ends the talk.
    // Returns the game's actions of the answer and of the next node.
    std::vector<DialogAction> choose(int index, Quests& quests);
    std::vector<DialogAction> takeGameActions(); // of the first node (after start)
    void end() { node_ = nullptr; }

private:
    void enter(const DialogNode* n, Quests& quests);
    const DialogDef* dialog_ = nullptr;
    const DialogNode* node_ = nullptr;
    std::vector<int> visible_;
    std::vector<DialogAction> pending_;
};

} // namespace engine
