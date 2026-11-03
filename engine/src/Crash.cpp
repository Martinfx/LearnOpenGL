#include "engine/Crash.h"
#include "engine/Assert.h"
#include "engine/Log.h"
#include "engine/Paths.h"

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <exception>
#include <fstream>
#include <map>
#include <mutex>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <dbghelp.h>
#else
#include <execinfo.h>
#include <unistd.h>
#endif

#if __has_include("EngineVersion.h")
#include "EngineVersion.h" // generated at build time (cmake/Version.cmake)
#endif
#ifndef ENGINE_VERSION
#define ENGINE_VERSION "dev"
#endif

namespace engine {

namespace {

std::string g_app = "engine";
std::mutex g_contextMutex;
std::map<std::string, std::string> g_context;
std::atomic<bool> g_writing{false};
bool g_previousCrashed = false;
bool g_previousKnown = false;

std::filesystem::path markerPath(const std::string& app) { return userDirectory() / (app + ".running"); }

std::string timestamp(bool forFile)
{
    const std::time_t t = std::time(nullptr);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char buf[32];
    std::strftime(buf, sizeof buf, forFile ? "%Y%m%d-%H%M%S" : "%Y-%m-%d %H:%M:%S", &tm);
    return buf;
}

#ifndef _WIN32
const char* signalName(int sig)
{
    switch (sig) {
    case SIGSEGV: return "SIGSEGV (invalid memory access)";
    case SIGILL: return "SIGILL (illegal instruction; also bounds-check traps)";
    case SIGFPE: return "SIGFPE (arithmetic error)";
    case SIGBUS: return "SIGBUS (bus error)";
    case SIGABRT: return "SIGABRT (abort)";
    default: return "signal";
    }
}
#endif

// Report text; `stack` is written separately (it needs a file descriptor on POSIX).
void writeHeader(std::FILE* f, const std::string& reason)
{
    std::fprintf(f, "%s crash report\n", g_app.c_str());
    std::fprintf(f, "time: %s\nversion: %s\nreason: %s\n", timestamp(false).c_str(), ENGINE_VERSION, reason.c_str());
    if (g_contextMutex.try_lock()) { // never wait inside a crash
        for (const auto& [k, v] : g_context)
            std::fprintf(f, "%s: %s\n", k.c_str(), v.c_str());
        g_contextMutex.unlock();
    }
    std::fflush(f);
}

void writeLogTail(std::FILE* f)
{
    std::fprintf(f, "\nlast log lines:\n");
    for (const LogLine& l : recentLog(500))
        std::fprintf(f, "%s\n", formatLogLine(l).c_str());
    std::fflush(f);
}

std::filesystem::path reportPath() { return userDirectory("crashes") / (g_app + "-" + timestamp(true) + ".txt"); }

std::filesystem::path writeReport(const std::string& reason, void* platformContext)
{
    (void)platformContext;
    if (g_writing.exchange(true))
        return {}; // a second crash while writing the first report
    const std::filesystem::path path = reportPath();
#ifdef _WIN32
    std::FILE* f = _wfopen(path.wstring().c_str(), L"w");
#else
    std::FILE* f = std::fopen(path.c_str(), "w");
#endif
    if (!f) {
        g_writing = false;
        return {};
    }
    writeHeader(f, reason);
    std::fprintf(f, "\nstack:\n");
    std::fflush(f);
#ifdef _WIN32
    void* frames[64];
    const USHORT n = CaptureStackBackTrace(0, 64, frames, nullptr);
    HANDLE process = GetCurrentProcess();
    SymInitialize(process, nullptr, TRUE);
    alignas(SYMBOL_INFO) char symbolBuffer[sizeof(SYMBOL_INFO) + 256];
    auto* symbol = reinterpret_cast<SYMBOL_INFO*>(symbolBuffer);
    for (USHORT i = 0; i < n; ++i) {
        std::memset(symbolBuffer, 0, sizeof symbolBuffer);
        symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
        symbol->MaxNameLen = 255;
        DWORD64 displacement = 0;
        if (SymFromAddr(process, reinterpret_cast<DWORD64>(frames[i]), &displacement, symbol))
            std::fprintf(f, "  %2u %s + 0x%llx\n", i, symbol->Name, static_cast<unsigned long long>(displacement));
        else
            std::fprintf(f, "  %2u %p\n", i, frames[i]);
    }
    if (platformContext) { // minidump next to the report
        std::filesystem::path dumpPath = path;
        dumpPath.replace_extension(".dmp");
        HANDLE dump = CreateFileW(dumpPath.wstring().c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (dump != INVALID_HANDLE_VALUE) {
            MINIDUMP_EXCEPTION_INFORMATION info{};
            info.ThreadId = GetCurrentThreadId();
            info.ExceptionPointers = static_cast<EXCEPTION_POINTERS*>(platformContext);
            MiniDumpWriteDump(process, GetCurrentProcessId(), dump, MiniDumpWithIndirectlyReferencedMemory, &info, nullptr, nullptr);
            CloseHandle(dump);
            std::fprintf(f, "minidump: %s\n", dumpPath.string().c_str());
        }
    }
#else
    void* frames[64];
    const int n = backtrace(frames, 64);
    backtrace_symbols_fd(frames, n, fileno(f)); // no malloc: safe enough in a signal handler
    std::fprintf(f, "(frames without a name: addr2line -C -f -e <program> <offset in parentheses>)\n");
#endif
    writeLogTail(f);
    std::fclose(f);
    g_writing = false;
    return path;
}

#ifdef _WIN32
LONG WINAPI onException(EXCEPTION_POINTERS* info)
{
    char reason[96];
    std::snprintf(reason, sizeof reason, "exception 0x%08lx", info && info->ExceptionRecord ? info->ExceptionRecord->ExceptionCode : 0ul);
    const std::filesystem::path p = writeReport(reason, info);
    std::fprintf(stderr, "Crash: %s, report in %s\n", reason, p.string().c_str());
    return EXCEPTION_EXECUTE_HANDLER;
}
#else
void onSignal(int sig)
{
    const std::filesystem::path p = writeReport(signalName(sig), nullptr);
    const char* head = "Crash, report in ";
    [[maybe_unused]] ssize_t r = write(STDERR_FILENO, head, std::strlen(head));
    r = write(STDERR_FILENO, p.c_str(), std::strlen(p.c_str()));
    r = write(STDERR_FILENO, "\n", 1);
    std::signal(sig, SIG_DFL); // the default action ends the process (and makes a core file if enabled)
    std::raise(sig);
}
#endif

std::atomic<bool> g_quit{false};
#ifndef _WIN32
void onQuitSignal(int sig)
{
    g_quit.store(true);
    std::signal(sig, SIG_DFL); // a second one ends the program at once
}
#endif

void onTerminate()
{
    std::string reason = "uncaught exception";
    if (auto e = std::current_exception()) {
        try {
            std::rethrow_exception(e);
        } catch (const std::exception& ex) {
            reason += std::string(": ") + ex.what();
        } catch (...) {
        }
    }
    const std::filesystem::path p = writeReport(reason, nullptr);
    std::fprintf(stderr, "Crash: %s, report in %s\n", reason.c_str(), p.string().c_str());
    std::abort();
}

} // namespace

const char* engineVersion() { return ENGINE_VERSION; }

void setCrashContext(const std::string& key, const std::string& value)
{
    std::lock_guard lock(g_contextMutex);
    g_context[key] = value;
}

std::filesystem::path writeCrashReport(const std::string& reason) { return writeReport(reason, nullptr); }

void installCrashHandler(const std::string& app)
{
    g_app = app;
    setCrashContext("app", app);
    // A marker that only a clean exit removes: if it is still there, the last run died.
    std::error_code ec;
    const std::filesystem::path marker = markerPath(app);
    g_previousCrashed = std::filesystem::exists(marker, ec);
    g_previousKnown = true;
    std::ofstream(marker) << timestamp(false) << '\n';
    if (g_previousCrashed)
        LOG_WARN("crash", "The previous run of %s did not exit cleanly", app.c_str());
    std::set_terminate(onTerminate);
#ifdef _WIN32
    SetUnhandledExceptionFilter(onException);
#else
    // Own stack for the handler: a stack overflow must still be reported.
    static std::vector<char> altStack(64 * 1024);
    stack_t ss{};
    ss.ss_sp = altStack.data();
    ss.ss_size = altStack.size();
    sigaltstack(&ss, nullptr);
    struct sigaction sa {};
    sa.sa_handler = onSignal;
    sa.sa_flags = SA_ONSTACK;
    sigemptyset(&sa.sa_mask);
    for (int sig : {SIGSEGV, SIGILL, SIGFPE, SIGBUS, SIGABRT})
        sigaction(sig, &sa, nullptr);
    struct sigaction quit {};
    quit.sa_handler = onQuitSignal;
    sigemptyset(&quit.sa_mask);
    for (int sig : {SIGINT, SIGTERM})
        sigaction(sig, &quit, nullptr);
    // Warm up backtrace(): its first call loads libgcc, which must not happen in a handler.
    void* warm[2];
    backtrace(warm, 2);
#endif
}

bool previousRunCrashed(const std::string& app)
{
    if (g_previousKnown && app == g_app)
        return g_previousCrashed;
    std::error_code ec;
    return std::filesystem::exists(markerPath(app), ec);
}

bool quitRequested() { return g_quit.load(); }

void markCleanExit()
{
    std::error_code ec;
    std::filesystem::remove(markerPath(g_app), ec);
}

std::filesystem::path latestCrashReport()
{
    std::error_code ec;
    std::filesystem::path best;
    std::filesystem::file_time_type bestTime{};
    for (const auto& e : std::filesystem::directory_iterator(userDirectory("crashes"), ec)) {
        if (e.path().extension() != ".txt")
            continue;
        const auto t = e.last_write_time(ec);
        if (best.empty() || t > bestTime) {
            best = e.path();
            bestTime = t;
        }
    }
    return best;
}

// ---- asserts

std::string checkMessage() { return {}; }

std::string checkMessage(const char* format, ...)
{
    char buffer[1024];
    va_list args;
    va_start(args, format);
    std::vsnprintf(buffer, sizeof buffer, format, args);
    va_end(args);
    return buffer;
}

void checkFailed(const char* expression, const char* file, int line, const std::string& message)
{
    const std::string what = std::string("check failed: ") + expression + " at " + file + ":" + std::to_string(line) +
                             (message.empty() ? std::string() : " (" + message + ")");
    logMessage(LogLevel::Fatal, "check", what);
    const std::filesystem::path p = writeReport(what, nullptr);
    std::fprintf(stderr, "Crash report in %s\n", p.string().c_str());
    std::fflush(nullptr);
    std::_Exit(3);
}

bool ensureFailed(const char* expression, const char* file, int line, bool& reported)
{
    if (!reported) {
        reported = true;
        logf(LogLevel::Error, "check", "ensure failed: %s at %s:%d (reported once)", expression, file, line);
    }
    return false;
}

} // namespace engine
