#pragma once

#include <cstdint>

namespace core {

namespace TimeUtils {
constexpr std::int64_t kMillisPerSecond = 1000;
constexpr std::int64_t kSecondsPerMinute = 60;
constexpr std::int64_t kMillisPerMinute = kMillisPerSecond * kSecondsPerMinute;
}  // namespace TimeUtils

}  // namespace core

namespace domain {
inline std::int64_t minutesToMillis(std::int64_t m) { return m * core::TimeUtils::kMillisPerMinute; }
inline std::int64_t millisToMinutes(std::int64_t ms) { return ms / core::TimeUtils::kMillisPerMinute; }
inline std::int64_t floorToMinuteMs(std::int64_t ms) {
    return (ms / core::TimeUtils::kMillisPerMinute) * core::TimeUtils::kMillisPerMinute;
}
inline std::int64_t ceilToMinuteMs(std::int64_t ms) {
    return floorToMinuteMs(ms) + core::TimeUtils::kMillisPerMinute;
}
inline std::int64_t roundToMinuteMs(std::int64_t ms) {
    const auto f = floorToMinuteMs(ms);
    return (ms - f) >= core::TimeUtils::kMillisPerMinute / 2 ? f + core::TimeUtils::kMillisPerMinute : f;
}
inline std::int64_t alignToMinute(std::int64_t ms) { return floorToMinuteMs(ms); }
}  // namespace domain
