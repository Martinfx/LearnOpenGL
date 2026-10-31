#pragma once
#include <string>

// Checks that stay on in release builds:
//   ENGINE_CHECK(cond, "fmt", ...)  must hold: logs, writes a crash report and stops the program.
//   ENGINE_ENSURE(cond)             should hold: logs an error once per call site and carries on;
//                                   the value is `cond`, so `if (!ENGINE_ENSURE(p)) return;` works.
//   ENGINE_VERIFY(expr)             like ENSURE, for expressions with side effects (always evaluated).
namespace engine {
[[noreturn]] void checkFailed(const char* expression, const char* file, int line, const std::string& message);
bool ensureFailed(const char* expression, const char* file, int line, bool& reported);
std::string checkMessage(); // empty message for ENGINE_CHECK without one
#if defined(__GNUC__) || defined(__clang__)
__attribute__((format(printf, 1, 2)))
#endif
std::string checkMessage(const char* format, ...);
} // namespace engine

#define ENGINE_CHECK(cond, ...)                                                                                       \
    do {                                                                                                              \
        if (!(cond)) [[unlikely]]                                                                                     \
            ::engine::checkFailed(#cond, __FILE__, __LINE__, ::engine::checkMessage(__VA_ARGS__));                   \
    } while (false)

#define ENGINE_ENSURE(cond)                                                                                           \
    ([&]() -> bool {                                                                                                  \
        if (cond) [[likely]]                                                                                          \
            return true;                                                                                              \
        static bool reported_ = false;                                                                                \
        return ::engine::ensureFailed(#cond, __FILE__, __LINE__, reported_);                                          \
    }())

#define ENGINE_VERIFY(expr) ENGINE_ENSURE(expr)
