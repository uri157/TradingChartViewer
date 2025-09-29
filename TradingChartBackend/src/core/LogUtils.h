#pragma once

#include <atomic>
#include <chrono>
#include <mutex>
#include <string>
#include <unordered_map>

namespace core {

class LogRateLimiter {
public:
    explicit LogRateLimiter(std::chrono::milliseconds minInterval);
    bool allow();

private:
    std::chrono::milliseconds minInterval_;
    std::atomic<std::chrono::steady_clock::time_point> last_;
};

class RateLogger {
public:
    bool allow(const std::string& key, std::chrono::milliseconds interval);
    void reset(const std::string& key);

private:
    std::mutex mutex_;
    std::unordered_map<std::string, std::chrono::steady_clock::time_point> nextAllowed_;
};

}  // namespace core
