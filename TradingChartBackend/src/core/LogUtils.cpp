#include "core/LogUtils.h"

namespace core {

LogRateLimiter::LogRateLimiter(std::chrono::milliseconds minInterval)
    : minInterval_(minInterval), last_(std::chrono::steady_clock::time_point::min()) {}

bool LogRateLimiter::allow() {
    const auto now = std::chrono::steady_clock::now();
    const auto prev = last_.load(std::memory_order_relaxed);
    if (prev == std::chrono::steady_clock::time_point::min() ||
        now - prev >= minInterval_) {
        last_.store(now, std::memory_order_relaxed);
        return true;
    }
    return false;
}

bool RateLogger::allow(const std::string& key, std::chrono::milliseconds interval) {
    const auto now = std::chrono::steady_clock::now();
    std::scoped_lock lk(mutex_);
    auto it = nextAllowed_.find(key);
    if (it == nextAllowed_.end() || now >= it->second) {
        nextAllowed_[key] = now + interval;
        return true;
    }
    return false;
}

void RateLogger::reset(const std::string& key) {
    std::scoped_lock lk(mutex_);
    nextAllowed_.erase(key);
}

}  // namespace core
