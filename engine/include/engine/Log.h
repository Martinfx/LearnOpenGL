#pragma once
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace engine {

// Engine-wide log: levels and categories, written to the console, a file in the
// user's logs folder and a ring buffer (crash reports, consoles). Thread-safe;
// listeners are called on the thread that logged.
enum class LogLevel { Trace, Debug, Info, Warning, Error, Fatal };
const char* logLevelName(LogLevel level);

struct LogLine {
    double time = 0.0;        // seconds since the log started
    LogLevel level = LogLevel::Info;
    std::string category;     // "audio", "render", "script"...
    std::string text;
    std::uint64_t thread = 0; // small id of the logging thread (0 = first one seen, usually main)
};

// Starts the file sink: `<logs>/<app>.log`, the previous runs kept as <app>.1.log ... <app>.4.log.
// Without this call the log still goes to the console and the ring buffer.
bool openLogFile(const std::string& app, const std::filesystem::path& directory = {});
std::filesystem::path logFilePath();
void closeLogFile();

void setLogLevel(LogLevel minimum);   // lower levels are dropped (default Info; FPS_ENGINE_LOG=debug|trace)
LogLevel logLevel();
void setLogConsole(bool on);          // echo to stdout/stderr (default on)

void logMessage(LogLevel level, const char* category, const std::string& text);
std::size_t logCount(LogLevel level); // messages logged at this level since start
#if defined(__GNUC__) || defined(__clang__)
__attribute__((format(printf, 3, 4)))
#endif
void logf(LogLevel level, const char* category, const char* format, ...);

// The last lines (at most 2000 are kept), oldest first.
std::vector<LogLine> recentLog(std::size_t count = 500);
std::string formatLogLine(const LogLine& line);

using LogListener = std::function<void(const LogLine&)>;
int addLogListener(LogListener listener);
void removeLogListener(int id);

} // namespace engine

#define LOG_TRACE(category, ...) ::engine::logf(::engine::LogLevel::Trace, category, __VA_ARGS__)
#define LOG_DEBUG(category, ...) ::engine::logf(::engine::LogLevel::Debug, category, __VA_ARGS__)
#define LOG_INFO(category, ...) ::engine::logf(::engine::LogLevel::Info, category, __VA_ARGS__)
#define LOG_WARN(category, ...) ::engine::logf(::engine::LogLevel::Warning, category, __VA_ARGS__)
#define LOG_ERROR(category, ...) ::engine::logf(::engine::LogLevel::Error, category, __VA_ARGS__)
