#pragma once
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace engine {

// A path to another asset written in a text asset (level, material, .weapon,
// .particle, .sound, .ambience, glTF, Lua...): where it is in the file and what it
// points to. Found by looking at every quoted string and word that ends with an
// asset extension, so new formats need no parser here.
struct AssetReference {
    std::size_t offset = 0, length = 0; // of the path text in the file (without quotes)
    std::string text;                   // as written
    std::filesystem::path target;       // resolved, absolute and normal
    enum class Base { File, Root, Absolute } base = Base::File; // what the text is relative to
    bool exists = false;
};
// The asset extensions references are recognised by (".png", ".glb", ".material"...).
bool isAssetExtension(const std::string& extensionLower);
// Text files that can hold references.
bool canReference(const std::filesystem::path& file);
// References in `file` (relative ones resolve against its folder, then against `root`).
std::vector<AssetReference> findReferences(const std::filesystem::path& file, const std::filesystem::path& root);

// One problem the validation found.
struct AssetIssue {
    enum class Severity { Info, Warning, Error } severity = Severity::Warning;
    std::filesystem::path file;  // the asset with the problem (absolute)
    std::string message;
};

// Every asset under a root folder (assets/) with a persistent GUID and the assets it
// uses. GUIDs live in `<root>/asset_registry.txt` ("guid path" lines) and follow a
// rename, so what refers to an asset by GUID (save games, later prefabs) keeps working.
class AssetRegistry {
public:
    struct Record {
        std::uint64_t guid = 0;
        std::filesystem::path path;               // relative to the root, '/' separators
        std::vector<std::uint64_t> dependencies;  // assets it refers to
        std::vector<std::string> missing;         // references to files that do not exist (as written)
        std::uintmax_t size = 0;
    };

    // Lists the files (reference scanning runs on the job system). `progress` 0..1 and
    // `cancel` are optional (a worker thread in the editor).
    bool scan(const std::filesystem::path& root, std::string* error = nullptr, std::atomic<float>* progress = nullptr,
              const std::atomic<bool>* cancel = nullptr);
    bool save(std::string* error = nullptr) const; // writes the GUID file
    const std::filesystem::path& root() const { return root_; }

    const std::map<std::uint64_t, Record>& records() const { return records_; }
    const Record* find(std::uint64_t guid) const;
    const Record* find(const std::filesystem::path& file) const; // absolute or relative to the root
    std::filesystem::path absolute(const Record& r) const { return (root_ / r.path).lexically_normal(); }
    // Assets that refer to `file`.
    std::vector<const Record*> referencedBy(const std::filesystem::path& file) const;

    // Moves or renames an asset and rewrites every reference to it (and the relative
    // references inside it when it changes folder). `changed` gets the files rewritten.
    // The GUID stays. Fails without changing anything if `to` exists.
    bool rename(const std::filesystem::path& from, const std::filesystem::path& to, std::string* error = nullptr,
                std::vector<std::filesystem::path>* changed = nullptr);

    // Missing references (errors), unused assets (info). With `checkModels`, glTF models
    // are also loaded and checked against the world scale (1 unit = 1 m): far too big or
    // tiny bounds and characters that are not about a person tall.
    std::vector<AssetIssue> validate(bool checkModels = false, std::atomic<float>* progress = nullptr) const;

private:
    std::filesystem::path root_;
    std::map<std::uint64_t, Record> records_;
    std::map<std::string, std::uint64_t> byPath_; // generic relative path -> guid
    std::string key(const std::filesystem::path& file) const;
};

} // namespace engine
