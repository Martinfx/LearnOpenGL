#pragma once
#include <filesystem>
#include <string>

namespace engine {

// Crash reports: on a crash (segfault, illegal instruction, abort, uncaught
// exception, failed ENGINE_CHECK) a text report goes to <user dir>/crashes/:
// what happened, the stack, build version, context values (level, GPU...) and
// the last 500 log lines. Windows also writes a minidump next to it.
// Call once at startup, after openLogFile().
void installCrashHandler(const std::string& app);
// Context shown in the report, e.g. setCrashContext("level", "skrunda1.level").
void setCrashContext(const std::string& key, const std::string& value);
// Writes a report now (used by ENGINE_CHECK); returns its path.
std::filesystem::path writeCrashReport(const std::string& reason);
// Build version (git describe at configure time).
const char* engineVersion();

// Did the last run of `app` end without calling markCleanExit()? (crash, kill, power loss)
// The marker is created by installCrashHandler and removed by markCleanExit.
bool previousRunCrashed(const std::string& app);
void markCleanExit();
// Ctrl+C / SIGTERM asked the program to quit (handled like closing the window, so the run ends
// cleanly; a second Ctrl+C ends it at once).
bool quitRequested();
// Newest report in the crashes folder, empty if none.
std::filesystem::path latestCrashReport();

} // namespace engine
