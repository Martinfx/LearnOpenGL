#pragma once
#include <filesystem>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace engine {

// Quests: one .quest file each in assets/quests/ (editor: Assets > Quests / double click).
//   id "camp_stash"
//   title "Skrýš v nemocnici"
//   description "Velitel tábora chce zpátky zásoby ze skrýše v nemocnici."
//   giver "Camp leader"                        entity who gave it (marked on the map when done)
//   stage "find" "Najdi skrýš v nemocnici" "Hospital stash"
//                                              id, text in the PDA, entity marked on the map ("" = none)
//   reward medkits 2                           given when completed: medkits n, ammo "<type>" n,
//   reward goodwill "stalker" 10               goodwill "<faction>" n (the game applies them)
// Stages run in order unless a dialog or script jumps to another one.
struct QuestStage {
    std::string id, text, target;
    bool operator==(const QuestStage&) const = default;
};

struct QuestDef {
    std::string id, title, description, giver;
    std::vector<QuestStage> stages;
    std::vector<std::vector<std::string>> rewards; // words after "reward"
    std::filesystem::path file;
    const QuestStage* stage(std::string_view id) const;
    bool operator==(const QuestDef& o) const
    {
        return id == o.id && title == o.title && description == o.description && giver == o.giver && stages == o.stages && rewards == o.rewards;
    }
};

bool loadQuest(QuestDef& quest, const std::filesystem::path& file, std::string* error = nullptr);
bool saveQuest(const QuestDef& quest, const std::filesystem::path& file, std::string* error = nullptr);

// The player's quest log: every quest defined in the game (from a folder) and what the
// player did with them, plus named flags dialogs and scripts set ("met_leader").
class Quests {
public:
    enum class Status { None, Active, Completed, Failed };
    struct Entry {
        Status status = Status::None;
        std::string stage; // current stage id while active
        double started = 0.0; // order in the PDA (newest first)
    };
    struct Event {
        enum Kind { Started, Stage, Completed, Failed } kind;
        std::string quest;
    };

    // Loads all .quest files in `folder` (sub folders too); returns how many, errors in `errors`.
    int loadFolder(const std::filesystem::path& folder, std::vector<std::string>* errors = nullptr);
    void add(QuestDef quest);
    const QuestDef* find(std::string_view id) const;
    const std::map<std::string, QuestDef>& all() const { return defs_; }

    // Play state.
    Status status(std::string_view id) const;
    std::string stage(std::string_view id) const; // "" unless active
    bool start(std::string_view id);              // false: unknown or already started
    bool setStage(std::string_view id, std::string_view stage); // starts it too; false: unknown stage
    bool advance(std::string_view id);            // next stage, or completes it after the last one
    bool complete(std::string_view id);
    bool fail(std::string_view id);
    bool flag(std::string_view name) const { return flags_.count(std::string(name)) > 0; }
    void setFlag(std::string_view name, bool on = true);
    const std::map<std::string, Entry>& entries() const { return log_; }
    std::vector<Event> takeEvents(); // what happened since the last call (HUD messages, rewards)
    void clearProgress();            // new game

    // Saved games: "quest <id> <status> <stage>" and "flag <name>" lines.
    std::vector<std::string> save() const;
    void restore(const std::vector<std::string>& lines);

    // Condition used by dialogs and scripts: state is "none", "active", "done", "failed" or a stage id.
    bool is(std::string_view id, std::string_view state) const;

private:
    std::map<std::string, QuestDef> defs_;
    std::map<std::string, Entry> log_;
    std::set<std::string> flags_;
    std::vector<Event> events_;
    double counter_ = 0.0;
};

const char* questStatusName(Quests::Status s); // "none", "active", "done", "failed"

} // namespace engine
