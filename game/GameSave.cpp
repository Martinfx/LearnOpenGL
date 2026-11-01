#include <engine/Localization.h>
#include "GameSave.h"

#include <engine/Log.h>
#include <engine/Paths.h>
#include <engine/Scene.h>

#include <algorithm>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <sstream>

namespace {

std::uint64_t fnv1a(const std::string& text)
{
    std::uint64_t h = 1469598103934665603ull;
    for (unsigned char c : text)
        h = (h ^ c) * 1099511628211ull;
    return h;
}

void setError(std::string* error, const std::string& text)
{
    if (error)
        *error = text;
}

// Quoted string with \" and \\ escapes (titles, paths).
std::string quote(const std::string& s)
{
    std::string out = "\"";
    for (char c : s) {
        if (c == '"' || c == '\\')
            out += '\\';
        out += c == '\n' ? ' ' : c;
    }
    return out + '"';
}
bool unquote(std::istream& in, std::string& out)
{
    out.clear();
    in >> std::ws;
    if (in.get() != '"')
        return false;
    for (int c; (c = in.get()) != EOF;) {
        if (c == '"')
            return true;
        if (c == '\\')
            c = in.get();
        out += static_cast<char>(c);
    }
    return false;
}

} // namespace

bool SaveGame::write(const std::filesystem::path& file, std::string* error) const
{
    std::ostringstream body;
    body.precision(9);
    body << "fps_save " << kVersion << "\n";
    body << "level " << quote(level.generic_string()) << "\n";
    body << "title " << quote(title) << "\n";
    body << "created " << quote(created) << "\n";
    body << "time " << playTime << "\n";
    body << "player " << feet.x << ' ' << feet.y << ' ' << feet.z << ' ' << yaw << ' ' << pitch << ' ' << health << ' ' << dose << "\n";
    body << "weapon " << weapon << "\n";
    body << "ammo";
    for (int a : ammo)
        body << ' ' << a;
    body << "\n";
    body << "kills " << kills << "\n";
    for (const auto& [faction, value] : goodwill)
        body << "goodwill " << quote(faction) << ' ' << value << "\n";
    for (const auto& q : squads)
        body << "squad " << quote(q.name) << ' ' << q.position.x << ' ' << q.position.y << ' ' << q.position.z << ' ' << q.target << ' '
             << (q.arrived ? 1 : 0) << ' ' << q.wait << "\n";
    for (const std::filesystem::path& f : weaponFiles)
        body << "weapon_file " << quote(f.generic_string()) << "\n";
    for (const auto& [type, rounds] : stock)
        body << "stock " << quote(type) << ' ' << rounds << "\n";
    body << "medkits " << medkits << "\n";
    if (money >= 0)
        body << "money " << money << "\n";
    if (!weaponCondition.empty()) {
        body << "weapon_condition";
        for (float c : weaponCondition)
            body << ' ' << c;
        body << "\n";
    }
    if (!armor.empty())
        body << "armor " << quote(armor) << ' ' << armorCondition << "\n";
    for (const std::string& t : traders)
        body << "trader " << t << "\n";
    for (const std::string& a : artifacts)
        body << "artifact_carried " << quote(a) << "\n";
    if (artifactsSaved)
        body << "artifacts_saved 1\n";
    for (const LyingArtifact& a : lyingArtifacts)
        body << "artifact_lying " << quote(a.id) << ' ' << a.position.x << ' ' << a.position.y << ' ' << a.position.z << ' ' << a.anomaly << "\n";
    body << "flashlight " << (flashlightOn ? 1 : 0) << ' ' << flashlightCharge << "\n";
    if (timeOfDay >= 0.0f)
        body << "time_of_day " << timeOfDay << ' ' << dayMinutes << ' ' << (timeEnabled ? 1 : 0) << "\n";
    body << "wetness " << wetness << "\n";
    if (!weather.empty())
        body << "weather " << quote(weather) << ' ' << weatherHold << "\n";
    for (const std::string& q : quests)
        body << "questlog " << q << "\n";
    if (stormPhase >= 0)
        body << "anomaly_storm " << stormPhase << ' ' << stormSeconds << "\n";
    for (const Prop& p : props)
        body << "prop " << engine::objectIdToString(p.id) << ' ' << p.position.x << ' ' << p.position.y << ' ' << p.position.z << ' '
             << p.rotation.x << ' ' << p.rotation.y << ' ' << p.rotation.z << "\n";
    for (const auto& e : entities) {
        body << "entity " << engine::objectIdToString(e.id) << ' ' << (e.alive ? 1 : 0) << ' ' << e.health << ' ' << e.position.x
             << ' ' << e.position.y << ' ' << e.position.z << ' ' << e.yaw << "\n";
        if (!e.data.empty())
            body << "data " << e.data << "\n"; // one-line Lua literal, belongs to the entity above
        if (e.propertiesChanged) { // properties changed by inputs (lights switched, triggers used up)
            body << "props";
            for (const auto& [k, v] : e.properties)
                body << ' ' << quote(k) << ' ' << quote(v);
            body << "\n";
        }
        if (e.touching || !e.firedOutputs.empty()) {
            body << "io " << (e.touching ? 1 : 0);
            for (int i : e.firedOutputs)
                body << ' ' << i;
            body << "\n";
        }
    }
    for (const auto& p : pendingInputs)
        body << "pending " << p.delay << ' ' << quote(p.target) << ' ' << quote(p.input) << ' ' << quote(p.parameter) << ' '
             << engine::objectIdToString(p.caller) << ' ' << engine::objectIdToString(p.activator) << "\n";
    const std::string text = body.str();
    char sum[40];
    std::snprintf(sum, sizeof sum, "checksum %016llx\n", static_cast<unsigned long long>(fnv1a(text)));

    std::error_code ec;
    std::filesystem::create_directories(file.parent_path(), ec);
    const std::filesystem::path temp = file.string() + ".tmp";
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        out << text << sum;
        out.flush();
        if (!out) {
            setError(error, "cannot write " + temp.string());
            return false;
        }
    }
    std::filesystem::rename(temp, file, ec); // replaces the old save in one step
    if (ec) {
        setError(error, "cannot replace " + file.string() + ": " + ec.message());
        return false;
    }
    return true;
}

bool SaveGame::read(const std::filesystem::path& file, std::string* error)
{
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        setError(error, "cannot open " + file.string());
        return false;
    }
    std::string all((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    const std::size_t sumPos = all.rfind("checksum ");
    if (sumPos == std::string::npos) {
        setError(error, "save is incomplete (no checksum)");
        return false;
    }
    const std::string text = all.substr(0, sumPos);
    const unsigned long long stored = std::strtoull(all.c_str() + sumPos + 9, nullptr, 16);
    if (stored != fnv1a(text)) {
        setError(error, "save is damaged (checksum mismatch)");
        return false;
    }
    *this = SaveGame{};
    std::istringstream lines(text);
    int version = 0;
    for (std::string line; std::getline(lines, line);) {
        std::istringstream ls(line);
        std::string key;
        ls >> key;
        std::string str;
        if (key == "fps_save") {
            ls >> version;
            if (version < 1 || version > kVersion) {
                setError(error, "save version " + std::to_string(version) + " is not supported by this build");
                return false;
            }
        } else if (key == "weapon_file" && unquote(ls, str)) {
            weaponFiles.emplace_back(str);
        } else if (key == "stock" && unquote(ls, str)) {
            int rounds = 0;
            if (ls >> rounds)
                stock[str] = rounds;
        } else if (key == "medkits") {
            ls >> medkits;
        } else if (key == "money") {
            ls >> money;
        } else if (key == "weapon_condition") {
            for (float c; ls >> c;)
                weaponCondition.push_back(c);
        } else if (key == "armor" && unquote(ls, str)) {
            armor = str;
            ls >> armorCondition;
        } else if (key == "artifact_carried" && unquote(ls, str)) {
            artifacts.push_back(str);
        } else if (key == "artifacts_saved") {
            artifactsSaved = true;
        } else if (key == "artifact_lying" && unquote(ls, str)) {
            LyingArtifact a;
            a.id = str;
            ls >> a.position.x >> a.position.y >> a.position.z >> a.anomaly;
            lyingArtifacts.push_back(a);
        } else if (key == "trader") {
            std::string rest;
            std::getline(ls >> std::ws, rest);
            traders.push_back(rest);
        } else if (key == "level" && unquote(ls, str)) {
            level = std::filesystem::path(str);
        } else if (key == "title" && unquote(ls, str)) {
            title = str;
        } else if (key == "created" && unquote(ls, str)) {
            created = str;
        } else if (key == "time") {
            ls >> playTime;
        } else if (key == "player") {
            ls >> feet.x >> feet.y >> feet.z >> yaw >> pitch >> health >> dose;
        } else if (key == "weapon") {
            ls >> weapon;
        } else if (key == "ammo") {
            for (int a; ls >> a;)
                ammo.push_back(a);
        } else if (key == "kills") {
            ls >> kills;
        } else if (key == "squad" && unquote(ls, str)) {
            engine::ALife::SquadState q;
            q.name = str;
            int arrived = 1;
            if (ls >> q.position.x >> q.position.y >> q.position.z >> q.target >> arrived >> q.wait) {
                q.arrived = arrived != 0;
                squads.push_back(std::move(q));
            }
        } else if (key == "goodwill" && unquote(ls, str)) {
            float v = 0.0f;
            if (ls >> v)
                goodwill[str] = v;
        } else if (key == "questlog") {
            std::string rest;
            std::getline(ls >> std::ws, rest);
            quests.push_back(rest);
        } else if (key == "anomaly_storm") {
            ls >> stormPhase >> stormSeconds;
        } else if (key == "wetness") {
            ls >> wetness;
        } else if (key == "weather" && unquote(ls, str)) {
            weather = str;
            ls >> weatherHold;
        } else if (key == "time_of_day") {
            int on = 0;
            ls >> timeOfDay >> dayMinutes >> on;
            timeEnabled = on != 0;
        } else if (key == "flashlight") {
            int on = 0;
            ls >> on >> flashlightCharge;
            flashlightOn = on != 0;
        } else if (key == "prop") {
            Prop p;
            std::string id;
            if (ls >> id >> p.position.x >> p.position.y >> p.position.z >> p.rotation.x >> p.rotation.y >> p.rotation.z) {
                p.id = engine::objectIdFromString(id);
                props.push_back(p);
            }
        } else if (key == "entity") {
            engine::ScriptSystem::EntityState e;
            std::string id;
            int alive = 1;
            ls >> id >> alive >> e.health >> e.position.x >> e.position.y >> e.position.z >> e.yaw;
            e.id = engine::objectIdFromString(id);
            e.alive = alive != 0;
            entities.push_back(e);
        } else if (key == "data" && !entities.empty()) {
            entities.back().data = line.substr(5);
        } else if (key == "props" && !entities.empty()) {
            auto& e = entities.back();
            e.propertiesChanged = true;
            for (std::string k, v; unquote(ls, k) && unquote(ls, v);)
                e.properties[k] = v;
        } else if (key == "io" && !entities.empty()) {
            int touching = 0;
            ls >> touching;
            entities.back().touching = touching != 0;
            for (int i; ls >> i;)
                entities.back().firedOutputs.push_back(i);
        } else if (key == "pending") {
            engine::ScriptSystem::PendingInput p;
            std::string caller, activator;
            if (ls >> p.delay && unquote(ls, p.target) && unquote(ls, p.input) && unquote(ls, p.parameter) && ls >> caller >> activator) {
                p.caller = engine::objectIdFromString(caller);
                p.activator = engine::objectIdFromString(activator);
                pendingInputs.push_back(std::move(p));
            }
        }
    }
    if (version == 0 || level.empty()) {
        setError(error, "not a saved game");
        return false;
    }
    return true;
}

std::filesystem::path saveFile(const std::string& slot)
{
    return engine::userDirectory("saves") / (slot + ".sav");
}

std::string slotLabel(const std::string& slot)
{
    if (slot == "quick")
        return engine::tr("save.quick");
    if (slot == "auto")
        return engine::tr("save.auto");
    if (slot.rfind("slot", 0) == 0)
        return engine::trf("save.slot", slot.substr(4));
    return slot;
}

std::vector<SaveInfo> listSaves()
{
    std::vector<std::pair<std::filesystem::file_time_type, SaveInfo>> found;
    for (const char* slot : {"quick", "auto", "slot1", "slot2", "slot3"}) {
        const std::filesystem::path file = saveFile(slot);
        std::error_code ec;
        if (!std::filesystem::exists(file, ec))
            continue;
        SaveInfo info;
        info.slot = slot;
        info.file = file;
        SaveGame save;
        info.valid = save.read(file);
        info.title = info.valid ? save.title : "poškozená pozice";
        info.created = save.created;
        found.push_back({std::filesystem::last_write_time(file, ec), info});
    }
    std::sort(found.begin(), found.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
    std::vector<SaveInfo> out;
    for (auto& f : found)
        out.push_back(std::move(f.second));
    return out;
}
