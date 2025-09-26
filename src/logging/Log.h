#pragma once

#include "config/Config.h"

#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <mutex>
#include <string_view>

namespace logging {

enum class LogCategory { NET, DATA, CACHE, SNAPSHOT, RENDER, UI, DB };

class Log {
public:
    static void set_log_level(config::LogLevel level);
    static config::LogLevel get_log_level();
    static void SetGlobalLogLevel(config::LogLevel level);

    static bool try_parse_log_level(std::string_view value, config::LogLevel& levelOut);
    static const char* level_to_string(config::LogLevel level);
    static const char* category_to_string(LogCategory category);

    static void log(config::LogLevel level, LogCategory category, const char* fmt, ...)
#if defined(__GNUC__) || defined(__clang__)
        __attribute__((format(printf, 3, 4)))
#endif
        ;

private:
    static void vlog(config::LogLevel level, LogCategory category, const char* fmt, std::va_list args);

    static std::atomic<config::LogLevel> currentLevel;
    static std::mutex outputMutex;
};

}  // namespace logging

#define LOG_ERROR(cat, ...) ::logging::Log::log(::config::LogLevel::Error, (cat), __VA_ARGS__)
#define LOG_WARN(cat, ...)  ::logging::Log::log(::config::LogLevel::Warn,  (cat), __VA_ARGS__)
#define LOG_INFO(cat, ...)  ::logging::Log::log(::config::LogLevel::Info,  (cat), __VA_ARGS__)
#define LOG_DEBUG(cat, ...) ::logging::Log::log(::config::LogLevel::Debug, (cat), __VA_ARGS__)
#define LOG_TRACE(cat, ...) ::logging::Log::log(::config::LogLevel::Trace, (cat), __VA_ARGS__)

#define LOG_GUARD(expr, cat, ...)                                                                                      \
    do {                                                                                                                \
        if (!(expr)) {                                                                                                  \
            LOG_WARN((cat), __VA_ARGS__);                                                                               \
            return;                                                                                                     \
        }                                                                                                               \
    } while (false)

#define LOG_GUARD_RET(expr, cat, ret, ...)                                                                              \
    do {                                                                                                                \
        if (!(expr)) {                                                                                                  \
            LOG_WARN((cat), __VA_ARGS__);                                                                               \
            return (ret);                                                                                               \
        }                                                                                                               \
    } while (false)
