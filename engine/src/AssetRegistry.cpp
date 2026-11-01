#include "engine/AssetRegistry.h"
#include "engine/AssetCredits.h"

#include "engine/JobSystem.h"
#include "engine/Paths.h"
#include "engine/Scene.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>
#include <mutex>
#include <random>
#include <set>
#include <sstream>

namespace engine {

namespace fs = std::filesystem;

namespace {

std::string lower(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

std::string extensionOf(const std::string& text)
{
    const std::size_t dot = text.find_last_of('.');
    const std::size_t slash = text.find_last_of("/\\");
    if (dot == std::string::npos || (slash != std::string::npos && dot < slash))
        return {};
    return lower(text.substr(dot));
}

bool readFile(const fs::path& file, std::string& out)
{
    std::ifstream in(file, std::ios::binary);
    if (!in)
        return false;
    std::ostringstream ss;
    ss << in.rdbuf();
    out = ss.str();
    return true;
}

bool writeFile(const fs::path& file, const std::string& text)
{
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    out << text;
    return static_cast<bool>(out);
}

std::string generic(const fs::path& p) { return p.generic_string(); }

bool isGlb(const fs::path& file) { return lower(file.extension().string()) == ".glb"; }

std::uint32_t readU32(const std::string& s, std::size_t at)
{
    return static_cast<std::uint32_t>(static_cast<unsigned char>(s[at])) | static_cast<std::uint32_t>(static_cast<unsigned char>(s[at + 1])) << 8 |
           static_cast<std::uint32_t>(static_cast<unsigned char>(s[at + 2])) << 16 |
           static_cast<std::uint32_t>(static_cast<unsigned char>(s[at + 3])) << 24;
}

void writeU32(std::string& s, std::uint32_t v)
{
    for (int i = 0; i < 4; ++i)
        s.push_back(static_cast<char>((v >> (8 * i)) & 0xff));
}

// The text references are searched in: the whole file, or the JSON chunk of a .glb
// (`rest` gets the chunks after it, to write the file back).
bool readReferenceText(const fs::path& file, std::string& text, std::string* rest = nullptr)
{
    if (!isGlb(file)) {
        std::error_code ec;
        return fs::file_size(file, ec) <= 32u * 1024u * 1024u && readFile(file, text);
    }
    std::ifstream in(file, std::ios::binary);
    std::string head(20, '\0');
    if (!in.read(head.data(), 20) || head.compare(0, 4, "glTF") != 0 || head.compare(16, 4, "JSON") != 0)
        return false;
    const std::uint32_t length = readU32(head, 12);
    if (length > 64u * 1024u * 1024u)
        return false;
    text.assign(length, '\0');
    if (!in.read(text.data(), length))
        return false;
    if (rest) {
        std::ostringstream ss;
        ss << in.rdbuf();
        *rest = ss.str();
    }
    return true;
}

bool writeReferenceText(const fs::path& file, std::string text, const std::string& rest)
{
    if (!isGlb(file))
        return writeFile(file, text);
    while (text.size() % 4 != 0)
        text.push_back(' ');
    std::string out = "glTF";
    writeU32(out, 2);
    writeU32(out, static_cast<std::uint32_t>(12 + 8 + text.size() + rest.size()));
    writeU32(out, static_cast<std::uint32_t>(text.size()));
    out += "JSON";
    out += text;
    out += rest;
    return writeFile(file, out);
}

const char* kRegistryFile = "asset_registry.txt";

fs::path pathFromUtf8(const std::string& s) { return fs::path(std::u8string(s.begin(), s.end())); }
std::string pathToUtf8(const fs::path& p)
{
    const std::u8string u = p.generic_u8string();
    return std::string(u.begin(), u.end());
}

} // namespace

bool isAssetExtension(const std::string& e)
{
    static const std::set<std::string> known = {".png", ".jpg", ".jpeg", ".tga", ".bmp", ".hdr", ".ktx", ".ktx2", ".dds",
                                                ".glb", ".gltf", ".bin", ".material", ".weapon", ".particle", ".sound",
                                                ".ambience", ".ogg", ".wav", ".mp3", ".flac", ".lua", ".level", ".terrain", ".prefab"};
    return known.count(e) != 0;
}

bool canReference(const fs::path& file)
{
    static const std::set<std::string> text = {".level", ".prefab", ".material", ".weapon", ".particle", ".sound", ".ambience",
                                               ".gltf", ".glb", ".lua", ".cfg"};
    return text.count(lower(file.extension().string())) != 0;
}

std::vector<AssetReference> findReferences(const fs::path& file, const fs::path& root)
{
    std::vector<AssetReference> out;
    std::string text;
    std::error_code ec;
    if (!readReferenceText(file, text))
        return out;
    const fs::path dir = fs::absolute(file).parent_path();
    const bool lua = lower(file.extension().string()) == ".lua";
    const bool gltf = lower(file.extension().string()) == ".gltf" || isGlb(file);
    // glTF: only "uri" values are files ("name" values often look like file names too).
    auto isUriValue = [&](std::size_t quote) {
        std::size_t k = quote;
        auto back = [&] {
            while (k > 0 && std::isspace(static_cast<unsigned char>(text[k - 1])))
                --k;
        };
        back();
        if (k == 0 || text[k - 1] != ':')
            return false;
        --k;
        back();
        return k >= 5 && text.compare(k - 5, 5, "\"uri\"") == 0;
    };
    auto consider = [&](std::size_t begin, std::size_t end) {
        if (end <= begin || end - begin > 400)
            return;
        if (gltf && (begin == 0 || !isUriValue(begin - 1)))
            return;
        const std::string token = text.substr(begin, end - begin);
        if (token.find_first_of("\n\r%*?<>|") != std::string::npos || token.rfind("data:", 0) == 0 || token.find("://") != std::string::npos)
            return;
        if (!isAssetExtension(extensionOf(token)))
            return;
        AssetReference r;
        r.offset = begin;
        r.length = end - begin;
        r.text = token;
        const fs::path p = pathFromUtf8(token);
        if (p.is_absolute()) {
            r.base = AssetReference::Base::Absolute;
            r.target = p.lexically_normal();
        } else {
            const fs::path a = (dir / p).lexically_normal();
            const fs::path b = (root / p).lexically_normal();
            if (fs::exists(a, ec) || !fs::exists(b, ec)) {
                r.base = AssetReference::Base::File;
                r.target = a;
            } else {
                r.base = AssetReference::Base::Root;
                r.target = b;
            }
        }
        r.exists = fs::is_regular_file(r.target, ec);
        out.push_back(std::move(r));
    };
    const std::size_t n = text.size();
    std::size_t i = 0;
    while (i < n) {
        const char c = text[i];
        if (c == '"' || (lua && c == '\'')) {
            std::size_t j = i + 1;
            while (j < n && text[j] != c && text[j] != '\n') {
                if (text[j] == '\\')
                    ++j;
                ++j;
            }
            consider(i + 1, std::min(j, n));
            i = j + 1;
        } else if (lua && c == '-' && i + 1 < n && text[i + 1] == '-') { // Lua comment: skip the line
            while (i < n && text[i] != '\n')
                ++i;
        } else if (std::isspace(static_cast<unsigned char>(c)) || std::strchr("=,()[]{};", c)) {
            ++i;
        } else {
            std::size_t j = i;
            while (j < n && !std::isspace(static_cast<unsigned char>(text[j])) && !std::strchr("\"'=,()[]{};", text[j]))
                ++j;
            if (!lua) // bare words are paths only in data files, in Lua they are code
                consider(i, j);
            i = std::max(j, i + 1);
        }
    }
    return out;
}

std::string AssetRegistry::key(const fs::path& file) const
{
    fs::path p = file.is_absolute() ? file.lexically_normal().lexically_relative(root_) : file.lexically_normal();
    const std::string s = generic(p);
    if (s.empty() || s.rfind("..", 0) == 0)
        return {};
    return s;
}

bool AssetRegistry::scan(const fs::path& rootIn, std::string* error, std::atomic<float>* progress, const std::atomic<bool>* cancel)
{
    std::error_code ec;
    root_ = fs::absolute(rootIn, ec).lexically_normal();
    if (!fs::is_directory(root_, ec)) {
        if (error)
            *error = "not a folder: " + root_.string();
        return false;
    }
    // Known GUIDs.
    std::map<std::string, std::uint64_t> known;
    if (std::ifstream in(root_ / kRegistryFile); in) {
        std::string line;
        while (std::getline(in, line)) {
            if (line.empty() || line[0] == '#')
                continue;
            std::istringstream ls(line);
            std::string guid, path;
            if (ls >> guid >> std::ws && std::getline(ls, path))
                known[path] = std::stoull(guid, nullptr, 16);
        }
    }
    std::vector<fs::path> files;
    for (fs::recursive_directory_iterator it(root_, fs::directory_options::skip_permission_denied, ec), end; it != end;
         it.increment(ec)) {
        if (ec)
            break;
        const std::string name = it->path().filename().string();
        if (it->is_directory(ec)) {
            if (!name.empty() && name[0] == '.')
                it.disable_recursion_pending();
            continue;
        }
        if (!it->is_regular_file(ec) || name == kRegistryFile)
            continue;
        if (isAssetExtension(lower(it->path().extension().string())) || canReference(it->path()))
            files.push_back(it->path().lexically_normal());
    }
    std::sort(files.begin(), files.end());
    // References of the text assets, in parallel.
    std::vector<std::vector<AssetReference>> refs(files.size());
    std::atomic<std::size_t> done{0};
    JobSystem::global().parallelFor(files.size(), 8, [&](std::size_t begin, std::size_t end) {
        for (std::size_t i = begin; i < end; ++i) {
            if (cancel && *cancel)
                return;
            if (canReference(files[i]))
                refs[i] = findReferences(files[i], root_);
            if (progress)
                *progress = static_cast<float>(++done) / static_cast<float>(std::max<std::size_t>(files.size(), 1));
        }
    });
    if (cancel && *cancel)
        return false;
    records_.clear();
    byPath_.clear();
    std::mt19937_64 rng{std::random_device{}() ^ (static_cast<std::uint64_t>(files.size()) << 32)};
    std::set<std::uint64_t> used;
    for (const auto& [path, guid] : known)
        used.insert(guid);
    for (const fs::path& f : files) {
        Record r;
        r.path = fs::path(key(f));
        r.size = fs::file_size(f, ec);
        const auto k = known.find(generic(r.path));
        if (k != known.end()) {
            r.guid = k->second;
        } else {
            do
                r.guid = rng();
            while (r.guid == 0 || used.count(r.guid));
            used.insert(r.guid);
        }
        byPath_[generic(r.path)] = r.guid;
        records_[r.guid] = std::move(r);
    }
    for (std::size_t i = 0; i < files.size(); ++i) {
        Record& r = records_[byPath_[key(files[i])]];
        for (const AssetReference& ref : refs[i]) {
            if (!ref.exists) {
                r.missing.push_back(ref.text);
                continue;
            }
            const auto it = byPath_.find(key(ref.target));
            if (it != byPath_.end() && it->second != r.guid &&
                std::find(r.dependencies.begin(), r.dependencies.end(), it->second) == r.dependencies.end())
                r.dependencies.push_back(it->second);
        }
    }
    if (progress)
        *progress = 1.0f;
    return true;
}

bool AssetRegistry::save(std::string* error) const
{
    std::ostringstream out;
    out << "# Asset GUIDs (engine/AssetRegistry.h): kept when an asset is renamed in the editor.\n";
    std::vector<std::pair<std::string, std::uint64_t>> sorted(byPath_.begin(), byPath_.end());
    char hex[24];
    for (const auto& [path, guid] : sorted) {
        std::snprintf(hex, sizeof hex, "%016llx", static_cast<unsigned long long>(guid));
        out << hex << ' ' << path << '\n';
    }
    if (!writeFile(root_ / kRegistryFile, out.str())) {
        if (error)
            *error = "cannot write " + (root_ / kRegistryFile).string();
        return false;
    }
    return true;
}

const AssetRegistry::Record* AssetRegistry::find(std::uint64_t guid) const
{
    const auto it = records_.find(guid);
    return it == records_.end() ? nullptr : &it->second;
}

const AssetRegistry::Record* AssetRegistry::find(const fs::path& file) const
{
    const auto it = byPath_.find(key(file.is_absolute() ? file : root_ / file));
    return it == byPath_.end() ? nullptr : find(it->second);
}

std::vector<const AssetRegistry::Record*> AssetRegistry::referencedBy(const fs::path& file) const
{
    std::vector<const Record*> out;
    const Record* target = find(file);
    if (!target)
        return out;
    for (const auto& [guid, r] : records_)
        if (std::find(r.dependencies.begin(), r.dependencies.end(), target->guid) != r.dependencies.end())
            out.push_back(&r);
    return out;
}

bool AssetRegistry::rename(const fs::path& fromIn, const fs::path& toIn, std::string* error, std::vector<fs::path>* changed)
{
    auto fail = [&](const std::string& message) {
        if (error)
            *error = message;
        return false;
    };
    const fs::path from = (fromIn.is_absolute() ? fromIn : root_ / fromIn).lexically_normal();
    const fs::path to = (toIn.is_absolute() ? toIn : root_ / toIn).lexically_normal();
    std::error_code ec;
    if (!fs::is_regular_file(from, ec))
        return fail("no such file: " + from.string());
    if (fs::exists(to, ec))
        return fail("already exists: " + to.string());
    if (key(to).empty())
        return fail("the new path is outside the asset folder");
    const Record* record = find(from);
    if (!record)
        return fail("not in the asset registry (rescan): " + from.string());
    const std::uint64_t guid = record->guid;

    auto textFor = [&](const AssetReference& ref, const fs::path& target, const fs::path& fileDir) {
        switch (ref.base) {
        case AssetReference::Base::Absolute: return pathToUtf8(target);
        case AssetReference::Base::Root: return generic(target.lexically_relative(root_));
        default: return generic(target.lexically_relative(fileDir));
        }
    };
    auto rewrite = [&](const std::string& text, std::vector<std::pair<const AssetReference*, std::string>>& edits) {
        std::sort(edits.begin(), edits.end(), [](const auto& a, const auto& b) { return a.first->offset > b.first->offset; });
        std::string out = text;
        for (const auto& [ref, replacement] : edits)
            out.replace(ref->offset, ref->length, replacement);
        return out;
    };
    // New contents first; nothing is written before the move succeeds.
    struct Write {
        fs::path file;
        std::string text, rest;
    };
    std::vector<Write> writes;
    for (const Record* user : referencedBy(from)) {
        const fs::path file = absolute(*user);
        if (file == from)
            continue;
        std::string text, rest;
        if (!readReferenceText(file, text, &rest))
            return fail("cannot read " + file.string());
        const std::vector<AssetReference> refs = findReferences(file, root_);
        std::vector<std::pair<const AssetReference*, std::string>> edits;
        for (const AssetReference& ref : refs)
            if (ref.target == from)
                edits.emplace_back(&ref, textFor(ref, to, file.parent_path()));
        if (!edits.empty())
            writes.push_back({file, rewrite(text, edits), rest});
    }
    // The moved file's own relative references, when it changes folder.
    if (canReference(from) && from.parent_path() != to.parent_path()) {
        std::string text, rest;
        if (!readReferenceText(from, text, &rest))
            return fail("cannot read " + from.string());
        const std::vector<AssetReference> refs = findReferences(from, root_);
        std::vector<std::pair<const AssetReference*, std::string>> edits;
        for (const AssetReference& ref : refs)
            if (ref.base == AssetReference::Base::File)
                edits.emplace_back(&ref, textFor(ref, ref.target, to.parent_path()));
        if (!edits.empty())
            writes.push_back({to, rewrite(text, edits), rest});
    }
    fs::create_directories(to.parent_path(), ec);
    fs::rename(from, to, ec);
    if (ec)
        return fail("cannot move " + from.string() + ": " + ec.message());
    for (const Write& w : writes) {
        if (!writeReferenceText(w.file, w.text, w.rest))
            return fail("moved, but cannot rewrite " + w.file.string());
        if (changed)
            changed->push_back(w.file);
    }
    Record& r = records_[guid];
    byPath_.erase(generic(r.path));
    r.path = fs::path(key(to));
    byPath_[generic(r.path)] = guid;
    return save(error);
}

std::vector<AssetIssue> AssetRegistry::validate(bool checkModels, std::atomic<float>* progress) const
{
    std::vector<AssetIssue> issues;
    std::set<std::uint64_t> usedSet;
    for (const auto& [guid, r] : records_) {
        usedSet.insert(r.dependencies.begin(), r.dependencies.end());
        const bool script = lower(r.path.extension().string()) == ".lua";
        for (const std::string& m : r.missing)
            issues.push_back({script ? AssetIssue::Severity::Warning : AssetIssue::Severity::Error, absolute(r),
                              "refers to a missing file: " + m});
    }
    // Not used by anything (levels, scripts and weapons are loaded by the game itself).
    for (const auto& [guid, r] : records_) {
        const std::string e = lower(r.path.extension().string());
        if (usedSet.count(guid) || e == ".level" || e == ".lua" || e == ".weapon" || e == ".cfg" || e == ".terrain")
            continue;
        if (e == ".glb" || e == ".gltf" || e == ".material" || e == ".png" || e == ".jpg" || e == ".jpeg")
            issues.push_back({AssetIssue::Severity::Info, absolute(r), "not used by any level or asset (code may still load it)"});
    }
    if (checkModels) {
        std::vector<const Record*> models;
        for (const auto& [guid, r] : records_) {
            const std::string e = lower(r.path.extension().string());
            if (e == ".glb" || e == ".gltf")
                models.push_back(&r);
        }
        std::mutex mutex;
        std::atomic<std::size_t> done{0};
        JobSystem::global().parallelFor(models.size(), 2, [&](std::size_t begin, std::size_t end) {
            for (std::size_t i = begin; i < end; ++i) {
                const Record& r = *models[i];
                const fs::path file = absolute(r);
                Model m;
                std::string err;
                AssetIssue issue;
                issue.file = file;
                const std::string relPath = generic(r.path);
                if (!loadModelFile(m, file, &err)) {
                    // An empty far LOD (a detail that vanishes in the distance) is fine to keep, but say so.
                    const bool empty = err.find("no triangle meshes") != std::string::npos;
                    issue.severity = empty ? AssetIssue::Severity::Info : AssetIssue::Severity::Error;
                    issue.message = empty ? "no triangles (empty LOD?)" : "cannot load: " + err;
                } else if (relPath.find("weapons/fps/") != std::string::npos) {
                    // First-person arms: sized by the .weapon's view_scale, not the world scale.
                } else {
                    glm::vec3 lo(1e30f), hi(-1e30f);
                    for (const MeshPart& part : m.parts)
                        for (const glm::vec3& p : part.positions) {
                            lo = glm::min(lo, p);
                            hi = glm::max(hi, p);
                        }
                    const glm::vec3 size = hi - lo;
                    const float largest = std::max({size.x, size.y, size.z});
                    const std::string rel = generic(r.path);
                    char text[160];
                    if (m.parts.empty() || lo.x > hi.x) {
                        issue.message.clear();
                    } else if (largest > 600.0f) {
                        std::snprintf(text, sizeof text, "%.0f m across: probably exported in centimetres (1 unit = 1 m)", largest);
                        issue.message = text;
                    } else if (largest < 0.01f) {
                        std::snprintf(text, sizeof text, "%.4f m across: far too small (1 unit = 1 m)", largest);
                        issue.message = text;
                    } else if (rel.find("characters/") != std::string::npos && rel.find("/anims") == std::string::npos &&
                               (size.y < 1.3f || size.y > 2.3f)) {
                        std::snprintf(text, sizeof text, "character %.2f m tall: a person is about 1.8 m", size.y);
                        issue.message = text;
                    }
                }
                if (!issue.message.empty()) {
                    std::lock_guard<std::mutex> lock(mutex);
                    issues.push_back(issue);
                }
                if (progress)
                    *progress = static_cast<float>(++done) / static_cast<float>(models.size());
            }
        });
    }
    // Author and licence of every shipped model, texture, sound and font (CREDITS.md).
    for (AssetIssue& issue : checkAssetCredits(root_))
        issues.push_back(std::move(issue));
    std::sort(issues.begin(), issues.end(), [](const AssetIssue& a, const AssetIssue& b) {
        if (a.severity != b.severity)
            return a.severity > b.severity;
        return a.file < b.file;
    });
    return issues;
}

} // namespace engine
