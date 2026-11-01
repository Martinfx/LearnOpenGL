#include <engine/AssetCredits.h>


#include <algorithm>
#include <cctype>
#include <fstream>
#include <iterator>
#include <map>
#include <regex>
#include <set>

namespace engine {

namespace {

std::string lower(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

std::string readText(const std::filesystem::path& file)
{
    std::ifstream in(file, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

} // namespace

bool needsCredits(const std::filesystem::path& file)
{
    static const std::set<std::string> media = {".glb", ".gltf", ".png", ".jpg", ".jpeg", ".tga", ".hdr", ".exr", ".ktx2", ".dds",
                                                ".wav", ".ogg", ".mp3", ".flac", ".ttf", ".otf"};
    return media.count(lower(file.extension().string())) != 0;
}

std::filesystem::path creditsFileFor(const std::filesystem::path& file, const std::filesystem::path& root)
{
    const std::filesystem::path top = root.lexically_normal();
    std::error_code ec;
    for (std::filesystem::path dir = file.parent_path().lexically_normal();; dir = dir.parent_path()) {
        const std::filesystem::path credits = dir / "CREDITS.md";
        if (std::filesystem::is_regular_file(credits, ec))
            return credits;
        if (dir == top || dir.empty() || dir == dir.parent_path() || dir.lexically_relative(top).string().rfind("..", 0) == 0)
            return {};
    }
}

std::vector<std::string> licencesIn(const std::string& creditsText)
{
    const std::string t = lower(creditsText);
    std::vector<std::string> out;
    auto has = [&](const char* re) { return std::regex_search(t, std::regex(re)); };
    if (has(R"(\bcc0\b|cc-zero|creative commons zero|publicdomain/zero)"))
        out.push_back("CC0");
    if (has(R"(\bcc[- ]?by\b(?![- ]?(sa|nc|nd)\b)|licenses/by/)"))
        out.push_back("CC-BY");
    if (has(R"(\bcc[- ]?by[- ]sa\b|licenses/by-sa/|sharealike)"))
        out.push_back("CC-BY-SA");
    if (has(R"(\bcc[- ]?by[- ](sa[- ])?nc\b|licenses/by-nc|noncommercial|non-commercial use only)"))
        out.push_back("NC");
    if (has(R"(\bcc[- ]?by[- ](nc[- ])?nd\b|licenses/by-nd|licenses/by-nc-nd|noderivatives)"))
        out.push_back("ND");
    if (has(R"(public domain)"))
        out.push_back("public domain");
    if (has(R"(\bofl\b|open font license)"))
        out.push_back("OFL");
    if (has(R"(bitstream[- ]vera)"))
        out.push_back("Bitstream Vera");
    if (has(R"(\bodbl\b|open database license)"))
        out.push_back("ODbL");
    if (has(R"(\bmit license\b|\|\s*mit\s*\|)"))
        out.push_back("MIT");
    if (has(R"(apache[- ]2\.0|apache license)"))
        out.push_back("Apache-2.0");
    if (has(R"((^|[^l])\bgpl\b)"))
        out.push_back("GPL");
    return out;
}

std::vector<AssetIssue> checkAssetCredits(const std::filesystem::path& root)
{
    std::vector<AssetIssue> issues;
    std::map<std::filesystem::path, std::filesystem::path> coveredBy; // folder -> its CREDITS.md
    std::set<std::filesystem::path> creditFiles;
    std::error_code ec;
    for (auto it = std::filesystem::recursive_directory_iterator(root, std::filesystem::directory_options::skip_permission_denied, ec);
         !ec && it != std::filesystem::recursive_directory_iterator(); it.increment(ec)) {
        const std::filesystem::path& p = it->path();
        const std::string name = p.filename().string();
        if (it->is_directory(ec)) {
            if (name == "source" || (!name.empty() && name[0] == '.'))
                it.disable_recursion_pending(); // not shipped
            continue;
        }
        if (name == "CREDITS.md") {
            creditFiles.insert(p.lexically_normal());
            continue;
        }
        if (!needsCredits(p))
            continue;
        const std::filesystem::path dir = p.parent_path().lexically_normal();
        auto found = coveredBy.find(dir);
        if (found == coveredBy.end())
            found = coveredBy.emplace(dir, creditsFileFor(p, root)).first;
        if (found->second.empty())
            issues.push_back({AssetIssue::Severity::Error, p.lexically_normal(),
                              "no CREDITS.md in its folder or above: who made it and under which licence?"});
    }
    for (const std::filesystem::path& file : creditFiles) {
        const std::vector<std::string> licences = licencesIn(readText(file));
        auto named = [&](const char* id) { return std::find(licences.begin(), licences.end(), id) != licences.end(); };
        if (licences.empty())
            issues.push_back({AssetIssue::Severity::Error, file, "names no licence (CC0, CC-BY, public domain, OFL...)"});
        if (named("NC") || named("ND"))
            issues.push_back({AssetIssue::Severity::Error, file,
                              "names a NonCommercial or NoDerivatives licence: not usable in a released game"});
        if (named("CC-BY-SA") || named("GPL"))
            issues.push_back({AssetIssue::Severity::Warning, file,
                              "names a share-alike licence (CC-BY-SA / GPL): what is made from it must use the same licence"});
    }
    return issues;
}

} // namespace engine
