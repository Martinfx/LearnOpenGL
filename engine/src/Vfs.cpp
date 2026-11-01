#include "engine/Vfs.h"
#include "engine/TextureCook.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <set>

namespace engine {

namespace {
constexpr char kMagic[8] = {'F', 'P', 'S', 'P', 'A', 'K', '0', '1'};

std::string utf8(const std::filesystem::path& p)
{
    const std::u8string s = p.generic_u8string();
    return std::string(s.begin(), s.end());
}
std::filesystem::path fromUtf8(const std::string& s) { return std::filesystem::path(std::u8string(s.begin(), s.end())); }

bool readDisk(const std::filesystem::path& file, std::string& bytes)
{
    std::ifstream in(file, std::ios::binary);
    if (!in)
        return false;
    in.seekg(0, std::ios::end);
    const std::streamoff size = in.tellg();
    if (size < 0)
        return false;
    in.seekg(0);
    bytes.resize(static_cast<std::size_t>(size));
    return size == 0 || static_cast<bool>(in.read(bytes.data(), size));
}
} // namespace

// An archive: header, table of contents (path, offset, size), then the data. Files are
// stored as they are (textures and sounds are compressed already); reads seek into it.
struct Vfs::Archive {
    std::filesystem::path file;
    std::map<std::string, std::pair<std::uint64_t, std::uint64_t>> entries; // path -> offset, size
    mutable std::mutex mutex;
    mutable std::ifstream in;

    bool open(const std::filesystem::path& pak, std::string* error)
    {
        auto fail = [&](const char* why) {
            if (error)
                *error = pak.string() + ": " + why;
            return false;
        };
        file = pak;
        in.open(pak, std::ios::binary);
        if (!in)
            return fail("cannot open");
        char magic[8] = {};
        std::uint32_t count = 0;
        if (!in.read(magic, 8) || std::memcmp(magic, kMagic, 8) != 0)
            return fail("not an archive");
        if (!in.read(reinterpret_cast<char*>(&count), 4) || count > 10000000u)
            return fail("damaged table");
        for (std::uint32_t i = 0; i < count; ++i) {
            std::uint32_t len = 0;
            std::uint64_t offset = 0, size = 0;
            if (!in.read(reinterpret_cast<char*>(&len), 4) || len > 4096)
                return fail("damaged table");
            std::string name(len, '\0');
            if (!in.read(name.data(), len) || !in.read(reinterpret_cast<char*>(&offset), 8) || !in.read(reinterpret_cast<char*>(&size), 8))
                return fail("damaged table");
            entries[name] = {offset, size};
        }
        return true;
    }
    bool read(const std::string& name, std::string& bytes) const
    {
        const auto it = entries.find(name);
        if (it == entries.end())
            return false;
        std::lock_guard lock(mutex);
        in.clear();
        in.seekg(static_cast<std::streamoff>(it->second.first));
        bytes.resize(static_cast<std::size_t>(it->second.second));
        return bytes.empty() || static_cast<bool>(in.read(bytes.data(), static_cast<std::streamsize>(bytes.size())));
    }
};

Vfs& vfs()
{
    static Vfs v;
    return v;
}

void Vfs::setRoot(const std::filesystem::path& assets)
{
    root_ = assets.empty() ? std::filesystem::path() : std::filesystem::absolute(assets).lexically_normal();
}

bool Vfs::mountArchive(const std::filesystem::path& pak, std::string* error)
{
    auto archive = std::make_shared<Archive>();
    if (!archive->open(pak, error))
        return false;
    layers_.push_back({{}, std::move(archive)});
    return true;
}

void Vfs::mountFolder(const std::filesystem::path& dir)
{
    layers_.push_back({std::filesystem::absolute(dir).lexically_normal(), nullptr});
}

void Vfs::clear()
{
    layers_.clear();
}

std::size_t Vfs::layerCount() const
{
    return layers_.size();
}

std::vector<std::string> Vfs::mountGame(const std::filesystem::path& exeDir)
{
    std::vector<std::string> log;
    setRoot(exeDir / "assets");
    auto sorted = [](const std::filesystem::path& dir, bool folders) {
        std::vector<std::filesystem::path> out;
        std::error_code ec;
        for (const auto& e : std::filesystem::directory_iterator(dir, ec))
            if (folders ? e.is_directory() : e.path().extension() == ".pak")
                out.push_back(e.path());
        std::sort(out.begin(), out.end());
        return out;
    };
    for (const auto& pak : sorted(exeDir, false)) {
        std::string error;
        log.push_back(mountArchive(pak, &error) ? "archive " + pak.filename().string() : error);
    }
    // Mods: each folder in mods/ is laid over the assets folder, .pak files there too.
    std::vector<std::filesystem::path> mods = sorted(exeDir / "mods", true);
    for (const auto& pak : sorted(exeDir / "mods", false))
        mods.push_back(pak);
    std::sort(mods.begin(), mods.end());
    for (const auto& mod : mods) {
        if (mod.extension() == ".pak") {
            std::string error;
            log.push_back(mountArchive(mod, &error) ? "mod " + mod.filename().string() : error);
        } else {
            mountFolder(mod);
            log.push_back("mod " + mod.filename().string() + "/");
        }
    }
    return log;
}

std::string Vfs::virtualPath(const std::filesystem::path& file) const
{
    std::error_code ec;
    if (root_.empty() || file.empty())
        return {};
    const std::filesystem::path abs = std::filesystem::absolute(file, ec).lexically_normal();
    const std::filesystem::path rel = abs.lexically_relative(root_);
    if (rel.empty() || *rel.begin() == "..")
        return {};
    return utf8(rel);
}

bool Vfs::read(const std::filesystem::path& file, std::string& bytes) const
{
    const std::string v = layers_.empty() ? std::string() : virtualPath(file);
    if (!v.empty())
        for (auto it = layers_.rbegin(); it != layers_.rend(); ++it) {
            if (it->archive) {
                if (it->archive->read(v, bytes))
                    return true;
            } else if (readDisk(it->folder / fromUtf8(v), bytes)) {
                return true;
            }
        }
    return readDisk(file, bytes);
}

bool Vfs::exists(const std::filesystem::path& file) const
{
    const std::string v = layers_.empty() ? std::string() : virtualPath(file);
    std::error_code ec;
    if (!v.empty())
        for (const Layer& l : layers_)
            if (l.archive ? l.archive->entries.count(v) > 0 : std::filesystem::is_regular_file(l.folder / fromUtf8(v), ec))
                return true;
    return std::filesystem::exists(file, ec);
}

std::filesystem::path Vfs::diskPath(const std::filesystem::path& file) const
{
    const std::string v = layers_.empty() ? std::string() : virtualPath(file);
    std::error_code ec;
    if (!v.empty())
        for (auto it = layers_.rbegin(); it != layers_.rend(); ++it) {
            if (!it->archive) {
                const auto loose = it->folder / fromUtf8(v);
                if (std::filesystem::is_regular_file(loose, ec))
                    return loose;
                continue;
            }
            const auto entry = it->archive->entries.find(v);
            if (entry == it->archive->entries.end())
                continue;
            // Extracted once per archive and size into the cache folder.
            const auto out = textureCookConfig().cacheDir.parent_path() / "extracted" / it->archive->file.stem() / fromUtf8(v);
            std::lock_guard lock(extractMutex_);
            if (std::filesystem::file_size(out, ec) == entry->second.second && !ec)
                return out;
            std::string bytes;
            if (!it->archive->read(v, bytes))
                return {};
            std::filesystem::create_directories(out.parent_path(), ec);
            std::ofstream(out, std::ios::binary).write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
            return out;
        }
    return std::filesystem::exists(file, ec) ? file : std::filesystem::path();
}

std::string Vfs::identity(const std::filesystem::path& file) const
{
    std::error_code ec;
    auto stamped = [&](const std::filesystem::path& f) -> std::string {
        const auto size = std::filesystem::file_size(f, ec);
        if (ec)
            return {};
        const auto time = std::filesystem::last_write_time(f, ec).time_since_epoch().count();
        return utf8(std::filesystem::absolute(f, ec).lexically_normal()) + '|' + std::to_string(static_cast<unsigned long long>(size)) + '|' +
               std::to_string(static_cast<long long>(time));
    };
    const std::string v = layers_.empty() ? std::string() : virtualPath(file);
    if (!v.empty())
        for (auto it = layers_.rbegin(); it != layers_.rend(); ++it) {
            if (!it->archive) {
                if (std::string s = stamped(it->folder / fromUtf8(v)); !s.empty())
                    return s;
            } else if (const auto e = it->archive->entries.find(v); e != it->archive->entries.end()) {
                return stamped(it->archive->file) + '#' + v + '@' + std::to_string(e->second.first);
            }
        }
    const std::string s = stamped(file);
    return s.empty() ? utf8(std::filesystem::absolute(file, ec).lexically_normal()) + "|missing" : s;
}

std::vector<std::filesystem::path> Vfs::list(const std::filesystem::path& dir, bool recursive) const
{
    std::set<std::filesystem::path> out;
    std::error_code ec;
    auto addFolder = [&](const std::filesystem::path& base, const std::filesystem::path& real) {
        auto add = [&](const std::filesystem::directory_entry& e) {
            if (e.is_regular_file(ec))
                out.insert(real / e.path().lexically_relative(base));
        };
        if (recursive) {
            for (auto it = std::filesystem::recursive_directory_iterator(base, ec); !ec && it != std::filesystem::recursive_directory_iterator();
                 it.increment(ec))
                add(*it);
        } else {
            for (auto it = std::filesystem::directory_iterator(base, ec); !ec && it != std::filesystem::directory_iterator(); it.increment(ec))
                add(*it);
        }
    };
    addFolder(dir, dir);
    const std::string v = layers_.empty() ? std::string() : virtualPath(dir);
    const bool atRoot = !layers_.empty() && !dir.empty() && std::filesystem::absolute(dir, ec).lexically_normal() == root_;
    if (!v.empty() || atRoot) {
        const std::string prefix = atRoot ? std::string() : v + "/";
        for (const Layer& l : layers_) {
            if (!l.archive) {
                addFolder(l.folder / fromUtf8(v), dir);
                continue;
            }
            for (auto it = l.archive->entries.lower_bound(prefix); it != l.archive->entries.end(); ++it) {
                if (it->first.compare(0, prefix.size(), prefix) != 0)
                    break;
                const std::string rest = it->first.substr(prefix.size());
                if (recursive || rest.find('/') == std::string::npos)
                    out.insert(dir / fromUtf8(rest));
            }
        }
    }
    return {out.begin(), out.end()};
}

bool readFile(const std::filesystem::path& file, std::string& bytes)
{
    return vfs().read(file, bytes);
}

bool fileExists(const std::filesystem::path& file)
{
    return vfs().exists(file);
}

bool writeArchive(const std::filesystem::path& pak, const std::filesystem::path& root,
                  const std::vector<std::filesystem::path>& files, std::string* error)
{
    auto fail = [&](const std::string& why) {
        if (error)
            *error = why;
        return false;
    };
    std::vector<std::string> names;
    std::vector<std::uint64_t> sizes;
    std::error_code ec;
    for (const auto& f : files) {
        const auto rel = f.is_absolute() ? f.lexically_relative(root) : f;
        names.push_back(utf8(rel.lexically_normal()));
        sizes.push_back(std::filesystem::file_size(root / rel, ec));
        if (ec)
            return fail("cannot read " + (root / rel).string());
    }
    std::uint64_t offset = 8 + 4;
    for (const auto& n : names)
        offset += 4 + n.size() + 16;
    std::ofstream out(pak, std::ios::binary);
    if (!out)
        return fail("cannot write " + pak.string());
    out.write(kMagic, 8);
    const auto count = static_cast<std::uint32_t>(names.size());
    out.write(reinterpret_cast<const char*>(&count), 4);
    for (std::size_t i = 0; i < names.size(); ++i) {
        const auto len = static_cast<std::uint32_t>(names[i].size());
        out.write(reinterpret_cast<const char*>(&len), 4);
        out.write(names[i].data(), len);
        out.write(reinterpret_cast<const char*>(&offset), 8);
        out.write(reinterpret_cast<const char*>(&sizes[i]), 8);
        offset += sizes[i];
    }
    for (std::size_t i = 0; i < names.size(); ++i) {
        std::string bytes;
        if (!readDisk(root / fromUtf8(names[i]), bytes) || bytes.size() != sizes[i])
            return fail("cannot read " + names[i]);
        out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    }
    if (!out) {
        out.close();
        std::filesystem::remove(pak, ec); // no half archive left behind
        return fail("cannot write " + pak.string());
    }
    return true;
}

bool listArchive(const std::filesystem::path& pak, std::vector<std::pair<std::string, std::uint64_t>>& entries, std::string* error)
{
    Vfs::Archive archive;
    if (!archive.open(pak, error))
        return false;
    entries.clear();
    for (const auto& [name, where] : archive.entries)
        entries.emplace_back(name, where.second);
    return true;
}

} // namespace engine
