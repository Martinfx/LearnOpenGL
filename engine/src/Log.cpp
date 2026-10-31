#include "engine/Log.h"
#include "engine/Paths.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <map>
#include <mutex>
#include <thread>

namespace engine {

namespace {

constexpr std::size_t kRingSize = 2000;
constexpr int kKeptRuns = 4;

LogLevel levelFromEnv()
{
    const char* v = std::getenv("FPS_ENGINE_LOG");
    if (!v)
        return LogLevel::Info;
    const std::string s(v);
    if (s == "trace")
        return LogLevel::Trace;
    if (s == "debug")
        return LogLevel::Debug;
    if (s == "warning" || s == "warn")
        return LogLevel::Warning;
    if (s == "error")
        return LogLevel::Error;
    return LogLevel::Info;
}

struct LogState {
    std::mutex mutex;
    std::deque<LogLine> ring;
    std::FILE* file = nullptr;
    std::filesystem::path filePath;
    std::map<int, LogListener> listeners;
    int nextListener = 1;
    std::atomic<int> minimum{static_cast<int>(levelFromEnv())};
    std::atomic<bool> console{true};
    std::map<std::thread::id, std::uint64_t> threads;
    const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
};

LogState& state()
{
    static LogState* s = new LogState(); // never destroyed: logging works during static destruction
    return *s;
}

} // namespace

const char* logLevelName(LogLevel level)
{
    switch (level) {
    case LogLevel::Trace: return "trace";
    case LogLevel::Debug: return "debug";
    case LogLevel::Info: return "info";
    case LogLevel::Warning: return "warning";
    case LogLevel::Error: return "error";
    case LogLevel::Fatal: return "fatal";
    }
    return "info";
}

std::string formatLogLine(const LogLine& line)
{
    char head[96];
    std::snprintf(head, sizeof head, "[%9.3f] %-7s %-8s ", line.time, logLevelName(line.level),
                  line.category.empty() ? "-" : line.category.c_str());
    return head + line.text;
}

bool openLogFile(const std::string& app, const std::filesystem::path& directory)
{
    LogState& s = state();
    std::lock_guard lock(s.mutex);
    if (s.file)
        std::fclose(s.file);
    s.file = nullptr;
    const std::filesystem::path dir = directory.empty() ? userDirectory("logs") : directory;
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    // Rotate: app.log -> app.1.log -> ... -> app.N.log (the oldest is dropped).
    auto name = [&](int i) { return dir / (i == 0 ? app + ".log" : app + "." + std::to_string(i) + ".log"); };
    std::filesystem::remove(name(kKeptRuns), ec);
    for (int i = kKeptRuns - 1; i >= 0; --i)
        if (std::filesystem::exists(name(i), ec))
            std::filesystem::rename(name(i), name(i + 1), ec);
    s.filePath = name(0);
#ifdef _WIN32
    s.file = _wfopen(s.filePath.wstring().c_str(), L"w");
#else
    s.file = std::fopen(s.filePath.c_str(), "w");
#endif
    if (!s.file)
        return false;
    // Lines logged before the file existed go in first.
    for (const LogLine& l : s.ring)
        std::fprintf(s.file, "%s\n", formatLogLine(l).c_str());
    std::fflush(s.file);
    return true;
}

std::filesystem::path logFilePath()
{
    LogState& s = state();
    std::lock_guard lock(s.mutex);
    return s.filePath;
}

void closeLogFile()
{
    LogState& s = state();
    std::lock_guard lock(s.mutex);
    if (s.file)
        std::fclose(s.file);
    s.file = nullptr;
}

void setLogLevel(LogLevel minimum) { state().minimum = static_cast<int>(minimum); }
LogLevel logLevel() { return static_cast<LogLevel>(state().minimum.load()); }
void setLogConsole(bool on) { state().console = on; }

namespace {
std::atomic<std::size_t> g_levelCounts[8];
}

std::size_t logCount(LogLevel level)
{
    const int i = static_cast<int>(level);
    return i >= 0 && i < 8 ? g_levelCounts[i].load() : 0;
}

void logMessage(LogLevel level, const char* category, const std::string& text)
{
    if (const int i = static_cast<int>(level); i >= 0 && i < 8)
        ++g_levelCounts[i]; // counted even when filtered out (smoke tests check for errors)
    LogState& s = state();
    if (static_cast<int>(level) < s.minimum.load())
        return;
    LogLine line;
    line.level = level;
    line.category = category ? category : "";
    // One line per message; trailing newlines from printf-style callers are dropped.
    line.text = text;
    while (!line.text.empty() && (line.text.back() == '\n' || line.text.back() == '\r'))
        line.text.pop_back();
    std::vector<LogListener> listeners;
    {
        std::lock_guard lock(s.mutex);
        line.time = std::chrono::duration<double>(std::chrono::steady_clock::now() - s.start).count();
        auto [it, inserted] = s.threads.emplace(std::this_thread::get_id(), s.threads.size());
        line.thread = it->second;
        const std::string formatted = formatLogLine(line);
        if (s.console) {
            std::FILE* out = level >= LogLevel::Warning ? stderr : stdout;
            // Plain text on the console: the editor's Play captures the game's output.
            if (line.category.empty())
                std::fprintf(out, "%s\n", line.text.c_str());
            else
                std::fprintf(out, "%s: %s\n", line.category.c_str(), line.text.c_str());
            std::fflush(out);
        }
        if (s.file) {
            std::fprintf(s.file, "%s\n", formatted.c_str());
            if (level >= LogLevel::Warning)
                std::fflush(s.file); // errors survive a crash right after
        }
        s.ring.push_back(line);
        if (s.ring.size() > kRingSize)
            s.ring.pop_front();
        listeners.reserve(s.listeners.size());
        for (const auto& [id, l] : s.listeners)
            listeners.push_back(l);
    }
    for (const LogListener& l : listeners)
        l(line);
}

void logf(LogLevel level, const char* category, const char* format, ...)
{
    if (static_cast<int>(level) < state().minimum.load())
        return;
    char buffer[2048];
    va_list args;
    va_start(args, format);
    const int n = std::vsnprintf(buffer, sizeof buffer, format, args);
    va_end(args);
    if (n < 0)
        return;
    if (static_cast<std::size_t>(n) < sizeof buffer) {
        logMessage(level, category, std::string(buffer, static_cast<std::size_t>(n)));
        return;
    }
    std::string big(static_cast<std::size_t>(n) + 1, '\0');
    va_start(args, format);
    std::vsnprintf(big.data(), big.size(), format, args);
    va_end(args);
    big.resize(static_cast<std::size_t>(n));
    logMessage(level, category, big);
}

std::vector<LogLine> recentLog(std::size_t count)
{
    LogState& s = state();
    std::lock_guard lock(s.mutex);
    const std::size_t n = std::min(count, s.ring.size());
    return std::vector<LogLine>(s.ring.end() - static_cast<std::ptrdiff_t>(n), s.ring.end());
}

int addLogListener(LogListener listener)
{
    LogState& s = state();
    std::lock_guard lock(s.mutex);
    const int id = s.nextListener++;
    s.listeners.emplace(id, std::move(listener));
    return id;
}

void removeLogListener(int id)
{
    LogState& s = state();
    std::lock_guard lock(s.mutex);
    s.listeners.erase(id);
}

} // namespace engine
