#include "logging/Log.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <ctime>
#include <string>
#include <cctype>

namespace {
constexpr std::size_t kMessageBufferSize = 1024;
}


namespace logging {

std::atomic<config::LogLevel> Log::currentLevel{ config::LogLevel::Info };

std::mutex Log::outputMutex;

void Log::set_log_level(config::LogLevel level) {
    currentLevel.store(level, std::memory_order_relaxed);
}

config::LogLevel Log::get_log_level() {
    return currentLevel.load(std::memory_order_relaxed);
}

void Log::SetGlobalLogLevel(config::LogLevel level) {
    set_log_level(level);
}

bool Log::try_parse_log_level(std::string_view value, config::LogLevel& levelOut) {
    std::string normalized(value);
    std::transform(normalized.begin(), normalized.end(), normalized.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });

    if (normalized == "trace") {
        levelOut = config::LogLevel::Trace;
        return true;
    }
    if (normalized == "debug") {
        levelOut = config::LogLevel::Debug;
        return true;
    }
    if (normalized == "info") {
        levelOut = config::LogLevel::Info;
        return true;
    }
    if (normalized == "warn" || normalized == "warning") {
        levelOut = config::LogLevel::Warn;
        return true;
    }
    if (normalized == "error") {
        levelOut = config::LogLevel::Error;
        return true;
    }
    return false;
}

const char* Log::level_to_string(config::LogLevel level) {
    switch (level) {
    case config::LogLevel::Error:
        return "ERROR";
    case config::LogLevel::Warn:
        return "WARN";
    case config::LogLevel::Info:
        return "INFO";
    case config::LogLevel::Debug:
        return "DEBUG";
    case config::LogLevel::Trace:
        return "TRACE";
    }
    return "UNKNOWN";
}

const char* Log::category_to_string(LogCategory category) {
    switch (category) {
    case LogCategory::NET:
        return "NET";
    case LogCategory::DATA:
        return "DATA";
    case LogCategory::CACHE:
        return "CACHE";
    case LogCategory::SNAPSHOT:
        return "SNAPSHOT";
    case LogCategory::RENDER:
        return "RENDER";
    case LogCategory::UI:
        return "UI";
    case LogCategory::DB:
        return "DB";
    }
    return "UNKNOWN";
}

void Log::log(config::LogLevel level, LogCategory category, const char* fmt, ...) {
    std::va_list args;
    va_start(args, fmt);
    vlog(level, category, fmt, args);
    va_end(args);
}

void Log::vlog(config::LogLevel level, LogCategory category, const char* fmt, std::va_list args) {
    if (config::logLevelSeverity(level) < config::logLevelSeverity(currentLevel.load(std::memory_order_relaxed))) {
        return;
    }

    std::array<char, kMessageBufferSize> buffer{};
    std::va_list argsCopy;
    va_copy(argsCopy, args);
    int written = std::vsnprintf(buffer.data(), buffer.size(), fmt, argsCopy);
    va_end(argsCopy);

    if (written < 0) {
        std::snprintf(buffer.data(), buffer.size(), "<format-error>");
    }
    else if (static_cast<std::size_t>(written) >= buffer.size()) {
        if (buffer.size() >= 5) {
            buffer[buffer.size() - 4] = '.';
            buffer[buffer.size() - 3] = '.';
            buffer[buffer.size() - 2] = '.';
        }
        buffer[buffer.size() - 1] = '\0';
    }

    using namespace std::chrono;
    const auto now = system_clock::now();
    const auto msSinceEpoch = duration_cast<milliseconds>(now.time_since_epoch());
    const std::time_t seconds = static_cast<std::time_t>(msSinceEpoch.count() / 1000);
    const int millis = static_cast<int>(msSinceEpoch.count() % 1000);

    std::tm utcTime{};
#if defined(_WIN32)
    gmtime_s(&utcTime, &seconds);
#else
    gmtime_r(&seconds, &utcTime);
#endif

    char timestamp[16];
    std::snprintf(timestamp, sizeof(timestamp), "%02d:%02d:%02d.%03d", utcTime.tm_hour, utcTime.tm_min, utcTime.tm_sec, millis);

    const char* levelStr = level_to_string(level);
    const char* categoryStr = category_to_string(category);

    std::lock_guard<std::mutex> lock(outputMutex);
    std::fprintf(stderr, "%s %s %s %s\n", timestamp, levelStr, categoryStr, buffer.data());
    std::fflush(stderr);
}

}  // namespace logging
