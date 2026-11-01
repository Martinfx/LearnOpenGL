// Packs the game's assets into an archive the game reads through its VFS (engine/Vfs.h):
//   fps_pack <assets folder> <out.pak> [--skip-unused] [--list]
// Source files the game does not read (.blend, sounds/source, Python and shell scripts) are
// left out; --skip-unused also leaves out what asset_check reports as unused. A .pak next to
// fps_game is laid over its assets folder; one in mods/ over both. --list prints an archive.
#include <engine/AssetRegistry.h>
#include <engine/Vfs.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <set>
#include <string>

namespace {
bool sourceOnly(const std::filesystem::path& rel)
{
    const std::string ext = rel.extension().string();
    if (ext == ".blend" || ext == ".blend1" || ext == ".py" || ext == ".sh" || ext == ".pak" || ext == ".tmp")
        return true;
    for (const auto& part : rel)
        if (part == "source" || part == ".git")
            return true;
    return false;
}
} // namespace

int main(int argc, char** argv)
{
    std::vector<std::string> args;
    bool skipUnused = false, list = false;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--skip-unused") == 0)
            skipUnused = true;
        else if (std::strcmp(argv[i], "--list") == 0)
            list = true;
        else
            args.push_back(argv[i]);
    }
    if (list && args.size() != 1) {
        std::fprintf(stderr, "usage: fps_pack <file.pak> --list\n");
        return 2;
    }
    if (list) {
        std::vector<std::pair<std::string, std::uint64_t>> entries;
        std::string error;
        if (!engine::listArchive(args[0], entries, &error)) {
            std::fprintf(stderr, "fps_pack: %s\n", error.c_str());
            return 1;
        }
        std::uint64_t total = 0;
        for (const auto& [name, size] : entries) {
            std::printf("%12llu  %s\n", static_cast<unsigned long long>(size), name.c_str());
            total += size;
        }
        std::printf("%zu files, %.1f MB\n", entries.size(), static_cast<double>(total) / (1024.0 * 1024.0));
        return 0;
    }
    if (args.size() != 2) {
        std::fprintf(stderr, "usage: fps_pack <assets folder> <out.pak> [--skip-unused]\n       fps_pack <file.pak> --list\n");
        return 2;
    }
    const std::filesystem::path root = std::filesystem::absolute(args[0]).lexically_normal();
    std::set<std::filesystem::path> unused;
    if (skipUnused) {
        engine::AssetRegistry registry;
        std::string error;
        if (!registry.scan(root, &error)) {
            std::fprintf(stderr, "fps_pack: %s\n", error.c_str());
            return 1;
        }
        for (const engine::AssetIssue& issue : registry.validate(false))
            if (issue.severity == engine::AssetIssue::Severity::Info)
                unused.insert(std::filesystem::absolute(issue.file).lexically_normal());
    }
    std::vector<std::filesystem::path> files;
    std::size_t skipped = 0;
    std::uint64_t bytes = 0;
    std::error_code ec;
    for (auto it = std::filesystem::recursive_directory_iterator(root, ec); !ec && it != std::filesystem::recursive_directory_iterator();
         it.increment(ec)) {
        if (!it->is_regular_file(ec))
            continue;
        const auto rel = it->path().lexically_relative(root);
        if (sourceOnly(rel) || unused.count(it->path().lexically_normal())) {
            ++skipped;
            continue;
        }
        files.push_back(rel);
        bytes += it->file_size(ec);
    }
    std::sort(files.begin(), files.end());
    // Room for the archive: a full disk would end in a half-written file.
    if (const auto space = std::filesystem::space(std::filesystem::absolute(args[1]).parent_path(), ec); !ec && space.available < bytes + (64u << 20)) {
        std::fprintf(stderr, "fps_pack: %.1f MB to pack, only %.1f MB free\n", static_cast<double>(bytes) / (1024.0 * 1024.0),
                     static_cast<double>(space.available) / (1024.0 * 1024.0));
        return 1;
    }
    std::string error;
    if (!engine::writeArchive(args[1], root, files, &error)) {
        std::fprintf(stderr, "fps_pack: %s\n", error.c_str());
        return 1;
    }
    std::printf("fps_pack: %zu files, %.1f MB -> %s (%zu left out)\n", files.size(), static_cast<double>(bytes) / (1024.0 * 1024.0),
                args[1].c_str(), skipped);
    return 0;
}
