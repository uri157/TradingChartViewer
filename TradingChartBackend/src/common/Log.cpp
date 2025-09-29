#include "common/Log.hpp"

#include <atomic>
#include <chrono>
#include <cctype>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <thread>

namespace ttp::log {
namespace {

std::atomic<Level> g_level{Level::Info};
std::mutex g_outputMutex;

const char* kLevelLabels[] = {"DEBUG", "INFO", "WARN", "ERROR"};

std::ostream& streamFor(Level level) {
    if (level == Level::Warn || level == Level::Error) {
        return std::cerr;
    }
    return std::cout;
}

std::tm safeLocaltime(std::time_t time) {
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &time);
#else
    localtime_r(&time, &tm);
#endif
    return tm;
}

}  // namespace

void setLevel(Level level) noexcept { g_level.store(level, std::memory_order_relaxed); }

Level getLevel() noexcept { return g_level.load(std::memory_order_relaxed); }

bool shouldLog(Level level) noexcept {
    return static_cast<int>(level) >= static_cast<int>(getLevel());
}

void log(Level level, const std::string& message) {
    const auto now = std::chrono::system_clock::now();
    const auto seconds = std::chrono::system_clock::to_time_t(now);
    const auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;

    const auto tm = safeLocaltime(seconds);

    std::ostringstream header;
    header << std::put_time(&tm, "%Y-%m-%d %H:%M:%S") << '.' << std::setw(3) << std::setfill('0')
           << milliseconds.count();

    std::ostringstream threadIdStream;
    threadIdStream << std::this_thread::get_id();

    std::lock_guard<std::mutex> lock(g_outputMutex);
    auto& out = streamFor(level);
    out << '[' << header.str() << "] [" << levelToString(level) << "] [thread "
        << threadIdStream.str() << "] " << message << std::endl;
}

const char* levelToString(Level level) noexcept {
    const auto index = static_cast<std::size_t>(level);
    if (index < (sizeof(kLevelLabels) / sizeof(kLevelLabels[0]))) {
        return kLevelLabels[index];
    }
    return "INFO";
}

Level levelFromString(std::string_view text) {
    std::string lower{text};
    for (auto& ch : lower) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }

    if (lower == "debug") {
        return Level::Debug;
    }
    if (lower == "info") {
        return Level::Info;
    }
    if (lower == "warn" || lower == "warning") {
        return Level::Warn;
    }
    if (lower == "err" || lower == "error") {
        return Level::Error;
    }

    throw std::invalid_argument("Nivel de log desconocido: " + std::string{text});
}

}  // namespace ttp::log
