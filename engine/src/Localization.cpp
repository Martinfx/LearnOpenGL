#include "engine/Localization.h"
#include "engine/Vfs.h"

#include <algorithm>
#include <cmath>
#include <fstream>

namespace engine {

namespace {
std::string trim(std::string s)
{
    const auto a = s.find_first_not_of(" \t\r");
    const auto b = s.find_last_not_of(" \t\r");
    return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
}
std::string unescape(const std::string& s)
{
    std::string out;
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\\' && i + 1 < s.size()) {
            const char c = s[++i];
            out += c == 'n' ? '\n' : c == 't' ? '\t' : c;
        } else {
            out += s[i];
        }
    }
    return out;
}
std::string escape(const std::string& s)
{
    std::string out;
    for (char c : s)
        out += c == '\n' ? std::string("\\n") : c == '\\' ? std::string("\\\\") : std::string(1, c);
    return out;
}
bool isPluralSuffix(std::string_view s)
{
    return s == "one" || s == "few" || s == "many" || s == "other";
}
} // namespace

bool StringTable::load(const std::filesystem::path& file, std::string* error)
{
    VfsStream in(file);
    if (!in) {
        if (error)
            *error = "Cannot open " + file.string();
        return false;
    }
    texts.clear();
    int lineNo = 0;
    for (std::string line; std::getline(in, line);) {
        ++lineNo;
        if (lineNo == 1 && line.rfind("\xEF\xBB\xBF", 0) == 0)
            line.erase(0, 3); // UTF-8 BOM from Windows editors
        const std::string t = trim(line);
        if (t.empty() || t[0] == '#')
            continue;
        const auto eq = t.find('=');
        if (eq == std::string::npos || trim(t.substr(0, eq)).empty()) {
            if (error)
                *error = file.string() + ":" + std::to_string(lineNo) + ": expected key = text";
            return false;
        }
        texts[trim(t.substr(0, eq))] = unescape(trim(t.substr(eq + 1)));
    }
    return true;
}

bool StringTable::save(const std::filesystem::path& file, std::string* error) const
{
    std::ofstream out(file, std::ios::binary);
    if (!out) {
        if (error)
            *error = "Cannot write " + file.string();
        return false;
    }
    out << "# Texts of the game (key = text). {0} are arguments, key.one / .few / .other plural forms.\n";
    std::string group;
    for (const auto& [key, text] : texts) {
        const std::string g = key.substr(0, key.find('.'));
        if (g != group && !group.empty())
            out << '\n';
        group = g;
        out << key << " = " << escape(text) << '\n';
    }
    return static_cast<bool>(out);
}

Localization& localization()
{
    static Localization l;
    return l;
}

bool Localization::load(const std::filesystem::path& folder, const std::string& language, std::string* error)
{
    StringTable base, current;
    if (!base.load(folder / (std::string(kBase) + ".strings"), error))
        return false;
    if (language != kBase && !current.load(folder / (language + ".strings"), error))
        return false;
    base_ = std::move(base);
    current_ = std::move(current);
    language_ = language.empty() ? kBase : language;
    return true;
}

std::string Localization::text(std::string_view key) const
{
    const std::string k(key);
    if (auto it = current_.texts.find(k); it != current_.texts.end() && !it->second.empty())
        return it->second;
    if (auto it = base_.texts.find(k); it != base_.texts.end())
        return it->second;
    return k;
}

const char* pluralCategory(const std::string& language, double n)
{
    const bool whole = std::floor(n) == n;
    const double a = std::abs(n);
    if (language == "cs" || language == "sk") {
        if (!whole)
            return "many";
        if (a == 1)
            return "one";
        if (a >= 2 && a <= 4)
            return "few";
        return "other";
    }
    if (language == "pl") {
        const long i = static_cast<long>(a);
        if (i == 1)
            return "one";
        if (i % 10 >= 2 && i % 10 <= 4 && (i % 100 < 12 || i % 100 > 14))
            return "few";
        return "many";
    }
    return a == 1 && whole ? "one" : "other"; // English and most others
}

std::string Localization::plural(std::string_view key, long n) const
{
    const std::string k(key);
    auto has = [&](const std::string& full) {
        return current_.texts.count(full) || base_.texts.count(full);
    };
    std::string full = k + "." + pluralCategory(language_, static_cast<double>(n));
    if (!has(full))
        full = k + ".other";
    if (!has(full))
        full = k;
    return formatText(text(full), {std::to_string(n)});
}

std::string Localization::localize(const std::string& s) const
{
    return !s.empty() && s[0] == '@' ? text(std::string_view(s).substr(1)) : s;
}

std::string formatText(std::string text, const std::vector<std::string>& args)
{
    for (std::size_t i = 0; i < args.size(); ++i) {
        const std::string mark = "{" + std::to_string(i) + "}";
        for (std::size_t at = text.find(mark); at != std::string::npos; at = text.find(mark, at + args[i].size()))
            text.replace(at, mark.size(), args[i]);
    }
    return text;
}

std::vector<std::string> availableLanguages(const std::filesystem::path& folder)
{
    std::vector<std::string> out;
    for (const auto& f : vfs().list(folder))
        if (f.extension() == ".strings")
            out.push_back(f.stem().string());
    std::sort(out.begin(), out.end(), [](const std::string& a, const std::string& b) {
        return (a == Localization::kBase) != (b == Localization::kBase) ? a == Localization::kBase : a < b;
    });
    return out;
}

std::vector<std::string> untranslatedKeys(const StringTable& base, const StringTable& other, const std::string& language)
{
    std::vector<std::string> out;
    auto present = [&](const std::string& k) {
        const auto it = other.texts.find(k);
        return it != other.texts.end() && !it->second.empty();
    };
    for (const auto& [key, text] : base.texts) {
        const auto dot = key.rfind('.');
        if (dot != std::string::npos && isPluralSuffix(std::string_view(key).substr(dot + 1))) {
            // A plural form: the forms this language uses for 1, 2, 5 and 1.5 are needed.
            const std::string form = key.substr(dot + 1);
            bool needed = false;
            for (double n : {1.0, 2.0, 5.0, 1.5})
                needed = needed || form == pluralCategory(language, n);
            if (needed && !present(key))
                out.push_back(key);
            continue;
        }
        if (!present(key))
            out.push_back(key);
    }
    return out;
}

} // namespace engine
