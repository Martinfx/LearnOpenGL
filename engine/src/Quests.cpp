#include "engine/Quests.h"
#include "engine/Vfs.h"

#include <algorithm>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace engine {

namespace {

// Removes a # comment (outside quotes).
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

std::vector<std::string> words(std::istream& s)
{
    std::vector<std::string> out;
    std::string w;
    while (s >> std::quoted(w))
        out.push_back(w);
    return out;
}

std::string q(const std::string& s)
{
    std::ostringstream o;
    o << std::quoted(s);
    return o.str();
}

} // namespace

const QuestStage* QuestDef::stage(std::string_view id) const
{
    for (const QuestStage& s : stages)
        if (s.id == id)
            return &s;
    return nullptr;
}

const char* questStatusName(Quests::Status s)
{
    switch (s) {
    case Quests::Status::Active: return "active";
    case Quests::Status::Completed: return "done";
    case Quests::Status::Failed: return "failed";
    default: return "none";
    }
}

bool loadQuest(QuestDef& quest, const std::filesystem::path& file, std::string* error)
{
    engine::VfsStream in(file);
    if (!in) {
        if (error)
            *error = "cannot open " + file.string();
        return false;
    }
    QuestDef d;
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
        std::vector<std::string> w = words(s);
        auto one = [&](std::string& out) {
            if (w.empty())
                return false;
            out = w[0];
            return true;
        };
        if (key == "id") {
            if (!one(d.id))
                return fail("id needs a value");
        } else if (key == "title") {
            one(d.title);
        } else if (key == "description") {
            one(d.description);
        } else if (key == "giver") {
            one(d.giver);
        } else if (key == "stage") {
            if (w.empty() || w[0].empty())
                return fail("stage needs an id");
            if (d.stage(w[0]))
                return fail("stage " + w[0] + " twice");
            d.stages.push_back({w[0], w.size() > 1 ? w[1] : std::string(), w.size() > 2 ? w[2] : std::string()});
        } else if (key == "reward") {
            if (w.empty())
                return fail("reward needs a kind");
            d.rewards.push_back(std::move(w));
        } else {
            return fail("unknown key " + key);
        }
    }
    if (d.id.empty())
        d.id = file.stem().string();
    if (d.stages.empty())
        return fail("a quest needs at least one stage");
    quest = std::move(d);
    return true;
}

bool saveQuest(const QuestDef& d, const std::filesystem::path& file, std::string* error)
{
    std::ofstream out(file);
    if (!out) {
        if (error)
            *error = "cannot write " + file.string();
        return false;
    }
    out << "# Quest (editor: Assets > Quests). Stages run in order; dialogs and scripts can jump.\n";
    out << "id " << q(d.id) << '\n';
    out << "title " << q(d.title) << '\n';
    out << "description " << q(d.description) << '\n';
    if (!d.giver.empty())
        out << "giver " << q(d.giver) << '\n';
    for (const QuestStage& s : d.stages)
        out << "stage " << q(s.id) << ' ' << q(s.text) << ' ' << q(s.target) << '\n';
    for (const auto& r : d.rewards) {
        out << "reward";
        for (const std::string& w : r)
            out << ' ' << (w.find_first_of(" \"#") == std::string::npos && !w.empty() ? w : q(w));
        out << '\n';
    }
    return static_cast<bool>(out);
}

int Quests::loadFolder(const std::filesystem::path& folder, std::vector<std::string>* errors)
{
    int n = 0;
    std::error_code ec;
    std::vector<std::filesystem::path> files;
    for (const std::filesystem::path& f : vfs().list(folder, true)) // loose, archives and mods
        if (f.extension() == ".quest")
            files.push_back(f);
    std::sort(files.begin(), files.end());
    for (const auto& f : files) {
        QuestDef d;
        std::string error;
        if (loadQuest(d, f, &error)) {
            add(std::move(d));
            ++n;
        } else if (errors) {
            errors->push_back(error);
        }
    }
    return n;
}

void Quests::add(QuestDef quest)
{
    std::string id = quest.id;
    defs_[id] = std::move(quest);
}

const QuestDef* Quests::find(std::string_view id) const
{
    const auto it = defs_.find(std::string(id));
    return it == defs_.end() ? nullptr : &it->second;
}

Quests::Status Quests::status(std::string_view id) const
{
    const auto it = log_.find(std::string(id));
    return it == log_.end() ? Status::None : it->second.status;
}

std::string Quests::stage(std::string_view id) const
{
    const auto it = log_.find(std::string(id));
    return it == log_.end() || it->second.status != Status::Active ? std::string() : it->second.stage;
}

bool Quests::start(std::string_view id)
{
    const QuestDef* d = find(id);
    if (!d || status(id) != Status::None)
        return false;
    Entry& e = log_[d->id];
    e.status = Status::Active;
    e.stage = d->stages.front().id;
    e.started = ++counter_;
    events_.push_back({Event::Started, d->id});
    return true;
}

bool Quests::setStage(std::string_view id, std::string_view stage)
{
    const QuestDef* d = find(id);
    if (!d || !d->stage(stage))
        return false;
    if (status(id) == Status::None)
        start(id);
    Entry& e = log_[d->id];
    if (e.status != Status::Active || e.stage == stage)
        return e.status == Status::Active;
    e.stage = std::string(stage);
    events_.push_back({Event::Stage, d->id});
    return true;
}

bool Quests::advance(std::string_view id)
{
    const QuestDef* d = find(id);
    if (!d || status(id) != Status::Active)
        return false;
    const std::string now = stage(id);
    for (std::size_t i = 0; i + 1 < d->stages.size(); ++i)
        if (d->stages[i].id == now)
            return setStage(id, d->stages[i + 1].id);
    return complete(id);
}

bool Quests::complete(std::string_view id)
{
    const QuestDef* d = find(id);
    if (!d)
        return false;
    const Status s = status(id);
    if (s == Status::Completed || s == Status::Failed)
        return false;
    Entry& e = log_[d->id];
    if (s == Status::None)
        e.started = ++counter_;
    e.status = Status::Completed;
    e.stage.clear();
    events_.push_back({Event::Completed, d->id});
    return true;
}

bool Quests::fail(std::string_view id)
{
    const QuestDef* d = find(id);
    if (!d || status(id) != Status::Active)
        return false;
    Entry& e = log_[d->id];
    e.status = Status::Failed;
    e.stage.clear();
    events_.push_back({Event::Failed, d->id});
    return true;
}

void Quests::setFlag(std::string_view name, bool on)
{
    if (on)
        flags_.insert(std::string(name));
    else
        flags_.erase(std::string(name));
}

std::vector<Quests::Event> Quests::takeEvents()
{
    std::vector<Event> out;
    out.swap(events_);
    return out;
}

void Quests::clearProgress()
{
    log_.clear();
    flags_.clear();
    events_.clear();
    counter_ = 0.0;
}

std::vector<std::string> Quests::save() const
{
    std::vector<std::string> out;
    std::vector<std::pair<double, const std::string*>> order;
    for (const auto& [id, e] : log_)
        order.emplace_back(e.started, &id);
    std::sort(order.begin(), order.end());
    for (const auto& [started, id] : order) {
        const Entry& e = log_.at(*id);
        out.push_back("quest " + q(*id) + ' ' + questStatusName(e.status) + ' ' + q(e.stage));
    }
    for (const std::string& f : flags_)
        out.push_back("flag " + q(f));
    return out;
}

void Quests::restore(const std::vector<std::string>& lines)
{
    clearProgress();
    for (const std::string& line : lines) {
        std::istringstream s(line);
        std::string key;
        s >> key;
        const std::vector<std::string> w = words(s);
        if (key == "flag" && !w.empty()) {
            flags_.insert(w[0]);
        } else if (key == "quest" && w.size() >= 2) {
            Entry e;
            e.status = w[1] == "active" ? Status::Active : w[1] == "done" ? Status::Completed : w[1] == "failed" ? Status::Failed : Status::None;
            e.stage = w.size() > 2 ? w[2] : std::string();
            e.started = ++counter_;
            if (e.status != Status::None)
                log_[w[0]] = e;
        }
    }
}

bool Quests::is(std::string_view id, std::string_view state) const
{
    const Status s = status(id);
    if (state == "none")
        return s == Status::None;
    if (state == "active")
        return s == Status::Active;
    if (state == "done")
        return s == Status::Completed;
    if (state == "failed")
        return s == Status::Failed;
    return s == Status::Active && stage(id) == state;
}

} // namespace engine
