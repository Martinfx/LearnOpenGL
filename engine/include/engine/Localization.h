#pragma once
#include <engine/Localization.h>
#include <filesystem>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace engine {

// Translated texts: one table per language in assets/text/<language>.strings, lines
//   key = text
// with `{0}`, `{1}` for arguments, `\n` for a line break, and plural forms as key.one /
// key.few / key.other (Czech: 1 / 2-4 / other, English: 1 / other; key.many is used for
// fractions where a language has it). Czech (cs) is the base: a key missing in another
// language shows the Czech text, a key missing everywhere shows the key itself.
struct StringTable {
    std::map<std::string, std::string> texts;
    bool load(const std::filesystem::path& file, std::string* error = nullptr);
    bool save(const std::filesystem::path& file, std::string* error = nullptr) const; // sorted, comments dropped
};

class Localization {
public:
    static constexpr const char* kBase = "cs";
    // Loads <folder>/cs.strings and <folder>/<language>.strings (through the VFS).
    bool load(const std::filesystem::path& folder, const std::string& language, std::string* error = nullptr);
    const std::string& language() const { return language_; }
    std::string text(std::string_view key) const;
    // Plural form for n, with {0} replaced by n.
    std::string plural(std::string_view key, long n) const;
    // Data strings (dialogs, quests, level titles): "@key" is looked up, anything else stays.
    std::string localize(const std::string& s) const;
    // The current language's text for key if it has one, else `fallback`. For data that
    // carries its base text itself (level titles) and is translated by key.
    std::string textOr(std::string_view key, const std::string& fallback) const;
    const StringTable& base() const { return base_; }
    const StringTable& current() const { return current_; }

private:
    std::string language_ = kBase;
    StringTable base_, current_;
};

Localization& localization();
inline std::string tr(std::string_view key) { return localization().text(key); }
// Data text shown to the player (item, weapon, dialog, quest, level names): "@key" is
// looked up in the current language, anything else is shown as written.
inline std::string localize(const std::string& text) { return localization().localize(text); }
// A level's loading_title / loading_hint ("title" / "hint"): "@key" as written, otherwise
// level.<stem>.<field> of the current language if it has one, else the written text (else
// `fallback`). Titles and hints of generated levels are translated without editing the files.
std::string levelText(const std::string& stem, const char* field, const std::string& written,
                      const std::string& fallback = {});
inline std::string trn(std::string_view key, long n) { return localization().plural(key, n); }
// Replaces {0}, {1}, ... with the arguments.
std::string formatText(std::string text, const std::vector<std::string>& args);
template <class... A>
std::string trf(std::string_view key, const A&... args)
{
    return formatText(tr(key), {args...});
}

// The plural category of n in a language: "one", "few", "many" or "other".
const char* pluralCategory(const std::string& language, double n);
// Languages with a table in the folder (file names without .strings), sorted, cs first.
std::vector<std::string> availableLanguages(const std::filesystem::path& folder);
// Keys of `base` that `other` lacks or leaves empty (untranslated). Plural keys count by base
// key: a language without "few" (English) needs only .one and .other.
std::vector<std::string> untranslatedKeys(const StringTable& base, const StringTable& other, const std::string& language);

} // namespace engine
