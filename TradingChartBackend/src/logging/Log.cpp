#include "logging/Log.h"

#include "core/LogUtils.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <cctype>
#include <deque>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <utility>

namespace {
using logging::Log;

constexpr std::size_t kMessageBufferSize = 1024;
constexpr std::size_t kQueueCapacity = 4096;
constexpr std::chrono::milliseconds kDefaultInfoRateLimit{100};  // 10/s
constexpr std::chrono::milliseconds kReverseBackfillLimit{500};
constexpr std::size_t kDebugLogMaxBytes = 8 * 1024 * 1024;
const std::filesystem::path kDebugLogPath{"./logs/ttp-debug.log"};

struct LogMessage {
    config::LogLevel level{};
    logging::LogCategory category{};
    std::chrono::system_clock::time_point timestamp{};
    std::string text;
};

struct AsyncLogState {
    std::mutex mutex;
    std::condition_variable cv;
    std::deque<LogMessage> queue;
    bool stopping = false;
    std::thread worker;
};

AsyncLogState& asyncState() {
    static AsyncLogState state;
    return state;
}

std::once_flag& workerOnce() {
    static std::once_flag flag;
    return flag;
}

struct DebugFileState {
    std::mutex mutex;
    std::ofstream stream;
    std::size_t size = 0;
    bool enabled = false;
};

DebugFileState& debugFileState() {
    static DebugFileState state;
    return state;
}

void processMessage(const LogMessage& msg);

std::atomic<bool>& debugSinkEnabled() {
    static std::atomic<bool> enabled{false};
    return enabled;
}

std::atomic<int>& infoRateLimitMs() {
    static std::atomic<int> limit{0};
    return limit;
}

core::RateLogger& infoRateLimiter() {
    static core::RateLogger limiter;
    return limiter;
}

core::LogRateLimiter& reverseBackfillLimiter() {
    static core::LogRateLimiter limiter{kReverseBackfillLimit};
    return limiter;
}

class SnapshotDebugFilter {
public:
    bool allow(const std::string& message) {
        auto state = extractToken_(message, "state=");
        auto version = extractToken_(message, "version=");
        if (!state && !version) {
            return true;
        }

        std::scoped_lock lock(mutex_);
        bool changed = false;
        if (state) {
            if (!hasState_ || *state != lastState_) {
                lastState_ = *state;
                hasState_ = true;
                changed = true;
            }
        }
        if (version) {
            if (!hasVersion_ || *version != lastVersion_) {
                lastVersion_ = *version;
                hasVersion_ = true;
                changed = true;
            }
        }
        return changed;
    }

private:
    static std::optional<std::string> extractToken_(const std::string& message, std::string_view key) {
        auto pos = message.find(key);
        if (pos == std::string::npos) {
            return std::nullopt;
        }
        pos += key.size();
        std::size_t end = pos;
        while (end < message.size()) {
            unsigned char ch = static_cast<unsigned char>(message[end]);
            if (std::isspace(ch) || ch == ',' || ch == ')') {
                break;
            }
            ++end;
        }
        if (end <= pos) {
            return std::nullopt;
        }
        return message.substr(pos, end - pos);
    }

    std::mutex mutex_;
    std::string lastState_;
    std::string lastVersion_;
    bool hasState_ = false;
    bool hasVersion_ = false;
};

SnapshotDebugFilter& snapshotFilter() {
    static SnapshotDebugFilter filter;
    return filter;
}

void openDebugLogUnlocked(DebugFileState& state) {
    namespace fs = std::filesystem;
    std::error_code ec;
    if (!kDebugLogPath.parent_path().empty()) {
        fs::create_directories(kDebugLogPath.parent_path(), ec);
    }

    state.stream.open(kDebugLogPath, std::ios::out | std::ios::app);
    if (!state.stream) {
        state.size = 0;
        return;
    }

    state.size = fs::exists(kDebugLogPath, ec) ? static_cast<std::size_t>(fs::file_size(kDebugLogPath, ec)) : 0U;
}

void rotateDebugLogUnlocked(DebugFileState& state) {
    namespace fs = std::filesystem;
    std::error_code ec;
    if (state.stream.is_open()) {
        state.stream.flush();
        state.stream.close();
    }

    fs::path rotated = kDebugLogPath;
    rotated += ".1";

    fs::remove(rotated, ec);
    fs::rename(kDebugLogPath, rotated, ec);
    state.size = 0;
}

void shutdownWorker();

void ensureWorkerStarted() {
    std::call_once(workerOnce(), [] {
        auto& state = asyncState();
        state.worker = std::thread([] {
            auto& localState = asyncState();
            std::unique_lock<std::mutex> lock(localState.mutex);
            while (true) {
                localState.cv.wait(lock, [&] {
                    return localState.stopping || !localState.queue.empty();
                });

                if (localState.queue.empty()) {
                    if (localState.stopping) {
                        break;
                    }
                    continue;
                }

                LogMessage msg = std::move(localState.queue.front());
                localState.queue.pop_front();
                lock.unlock();

                processMessage(msg);

                lock.lock();
            }
        });
        std::atexit(shutdownWorker);
    });
}

void shutdownWorker() {
    auto& state = asyncState();
    {
        std::lock_guard<std::mutex> lock(state.mutex);
        state.stopping = true;
    }
    state.cv.notify_all();
    if (state.worker.joinable()) {
        state.worker.join();
    }

    auto& debugState = debugFileState();
    std::lock_guard<std::mutex> lock(debugState.mutex);
    if (debugState.stream.is_open()) {
        debugState.stream.flush();
        debugState.stream.close();
    }
}

bool enqueueMessage(LogMessage&& msg) {
    ensureWorkerStarted();
    auto& state = asyncState();
    {
        std::unique_lock<std::mutex> lock(state.mutex);
        if (state.queue.size() >= kQueueCapacity) {
            return false;
        }
        state.queue.emplace_back(std::move(msg));
    }
    state.cv.notify_one();
    return true;
}

std::string formatLine(const LogMessage& msg) {
    using namespace std::chrono;
    const auto msSinceEpoch = duration_cast<milliseconds>(msg.timestamp.time_since_epoch());
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

    const char* levelStr = Log::level_to_string(msg.level);
    const char* categoryStr = Log::category_to_string(msg.category);

    std::string line;
    line.reserve(std::strlen(timestamp) + std::strlen(levelStr) + std::strlen(categoryStr) + msg.text.size() + 4);
    line.append(timestamp);
    line.push_back(' ');
    line.append(levelStr);
    line.push_back(' ');
    line.append(categoryStr);
    line.push_back(' ');
    line.append(msg.text);
    return line;
}

void writeToStream(FILE* stream, const std::string& line) {
    std::fwrite(line.data(), 1, line.size(), stream);
    std::fputc('\n', stream);
    std::fflush(stream);
}

bool writeToDebugFile(const std::string& line) {
    if (!debugSinkEnabled().load(std::memory_order_acquire)) {
        return false;
    }

    auto& state = debugFileState();
    std::lock_guard<std::mutex> lock(state.mutex);
    if (!state.enabled) {
        return false;
    }

    if (!state.stream.is_open()) {
        openDebugLogUnlocked(state);
        if (!state.stream.is_open()) {
            return false;
        }
    }

    const std::size_t lineBytes = line.size() + 1;
    if (state.size + lineBytes > kDebugLogMaxBytes) {
        rotateDebugLogUnlocked(state);
        openDebugLogUnlocked(state);
        if (!state.stream.is_open()) {
            return false;
        }
    }

    state.stream << line << '\n';
    state.stream.flush();
    state.size += lineBytes;
    return true;
}

bool passesFilters(const LogMessage& msg) {
    if (msg.level == config::LogLevel::Debug && msg.category == logging::LogCategory::SNAPSHOT) {
        if (!snapshotFilter().allow(msg.text)) {
            return false;
        }
    }

    if (msg.text.find("reverse_backfill window=") != std::string::npos) {
        if (!reverseBackfillLimiter().allow()) {
            return false;
        }
    }

    return true;
}

bool allowInfoCategory(const LogMessage& msg) {
    const int interval = infoRateLimitMs().load(std::memory_order_relaxed);
    if (interval <= 0) {
        return true;
    }
    auto key = std::string(Log::category_to_string(msg.category));
    return infoRateLimiter().allow(key, std::chrono::milliseconds(interval));
}

void configureForLevel(config::LogLevel level) {
    infoRateLimitMs().store(level == config::LogLevel::Trace ? static_cast<int>(kDefaultInfoRateLimit.count()) : 0,
                            std::memory_order_relaxed);

    const bool enableDebug = config::logLevelSeverity(level) <= config::logLevelSeverity(config::LogLevel::Debug);
    auto& state = debugFileState();
    {
        std::lock_guard<std::mutex> lock(state.mutex);
        state.enabled = enableDebug;
        if (!enableDebug) {
            if (state.stream.is_open()) {
                state.stream.flush();
                state.stream.close();
            }
            state.size = 0;
        }
    }
    debugSinkEnabled().store(enableDebug, std::memory_order_release);
}

void processMessage(const LogMessage& msg) {
    if (!passesFilters(msg)) {
        return;
    }

    if (msg.level == config::LogLevel::Info && !allowInfoCategory(msg)) {
        return;
    }

    const std::string line = formatLine(msg);

    switch (msg.level) {
    case config::LogLevel::Error:
    case config::LogLevel::Warn:
        writeToStream(stderr, line);
        break;
    case config::LogLevel::Info:
        writeToStream(stdout, line);
        break;
    case config::LogLevel::Debug:
    case config::LogLevel::Trace: {
        if (!writeToDebugFile(line)) {
            writeToStream(stdout, line);
        }
        break;
    }
    }
}

}  // namespace

namespace logging {

std::atomic<config::LogLevel> Log::currentLevel{ config::LogLevel::Info };

void Log::set_log_level(config::LogLevel level) {
    currentLevel.store(level, std::memory_order_relaxed);
    configureForLevel(level);
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

    LogMessage message;
    message.level = level;
    message.category = category;
    message.timestamp = std::chrono::system_clock::now();
    message.text.assign(buffer.data());

    if (!enqueueMessage(LogMessage(message))) {
        // Drop low-severity messages when the queue is saturated, but ensure warnings/errors surface.
        if (level == config::LogLevel::Error || level == config::LogLevel::Warn) {
            processMessage(message);
        }
    }
}

}  // namespace logging
