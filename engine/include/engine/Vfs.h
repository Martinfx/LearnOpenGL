#pragma once
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

namespace engine {

// Virtual file system: what the game reads from its assets folder can come from layers, the
// last mounted wins (by path below the assets folder, e.g. "textures/rust.png"):
//   1. the assets folder itself (loose files, what the editor works on)
//   2. archives (.pak: the shipped game packs its assets, see writeArchive)
//   3. mods: folders or archives in mods/ that replace files by path or add new ones
// Paths outside the assets folder (saves, config, cache) are read from disk as they are.
// Every loader reads through vfs().read / exists / list with the paths it always used
// (absolute paths into the assets folder), so a mod needs no changes to the code.
class Vfs {
public:
    void setRoot(const std::filesystem::path& assets); // folder the virtual paths are relative to
    const std::filesystem::path& root() const { return root_; }
    bool mountArchive(const std::filesystem::path& pak, std::string* error = nullptr);
    void mountFolder(const std::filesystem::path& dir);
    // The game's default layers: <exe>/assets, <exe>/*.pak, then <exe>/mods/* (folders and
    // .pak files, alphabetical). Returns a line per mounted layer for the log.
    std::vector<std::string> mountGame(const std::filesystem::path& exeDir);
    void clear();                                      // back to plain disk access

    // The path below the root ("" when `file` is not inside it).
    std::string virtualPath(const std::filesystem::path& file) const;
    bool read(const std::filesystem::path& file, std::string& bytes) const;
    bool exists(const std::filesystem::path& file) const;
    // A file on disk with this content: the mod's loose copy, the asset itself, or for a
    // file only in an archive an extracted copy in the cache (for libraries that open files
    // by name). Empty when there is no such file.
    std::filesystem::path diskPath(const std::filesystem::path& file) const;
    // Which concrete file the content comes from and its size and time (or the archive's):
    // changes when a mod replaces the file or the source changes. Cache keys use it.
    std::string identity(const std::filesystem::path& file) const;
    // Files directly in `dir` (or below with `recursive`) from all layers, as paths below the
    // real root (as if everything were loose files), sorted, without duplicates.
    std::vector<std::filesystem::path> list(const std::filesystem::path& dir, bool recursive = false) const;
    std::size_t layerCount() const;

    struct Archive; // an opened .pak (Vfs.cpp)

private:
    struct Layer {
        std::filesystem::path folder;           // a loose folder layer, or
        std::shared_ptr<const Archive> archive; // an archive layer
    };
    std::filesystem::path root_;
    std::vector<Layer> layers_; // lowest priority first (after the root itself)
    mutable std::mutex extractMutex_;
};

Vfs& vfs();

// Reads a whole file through vfs() (any path; assets go through the layers).
bool readFile(const std::filesystem::path& file, std::string& bytes);
bool fileExists(const std::filesystem::path& file);

// Drop-in for std::ifstream: the whole file read through vfs(); `!stream` when it is missing.
class VfsStream : public std::istringstream {
public:
    explicit VfsStream(const std::filesystem::path& file)
    {
        std::string bytes;
        if (readFile(file, bytes))
            str(std::move(bytes));
        else
            setstate(std::ios::failbit);
    }
    bool is_open() const { return !fail(); }
};

// Packs files into an archive; `files` are paths below `root`. Returns false with a reason.
bool writeArchive(const std::filesystem::path& pak, const std::filesystem::path& root,
                  const std::vector<std::filesystem::path>& files, std::string* error = nullptr);
// Table of contents of an archive: virtual path and size.
bool listArchive(const std::filesystem::path& pak, std::vector<std::pair<std::string, std::uint64_t>>& entries,
                 std::string* error = nullptr);

} // namespace engine
