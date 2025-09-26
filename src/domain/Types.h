#pragma once

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace domain {

using TimestampMs = long long;
using TradeCount = std::int32_t;
using Symbol = std::string;

struct Interval {
    TimestampMs ms{0};
    constexpr bool valid() const noexcept { return ms > 0; }
};

inline TimestampMs align_down_ms(TimestampMs t, TimestampMs step) {
    return (step > 0) ? (t / step) * step : t;
}

inline TimestampMs align_up_ms(TimestampMs t, TimestampMs step) {
    return (step > 0) ? ((t + step - 1) / step) * step : t;
}

struct TimeRange {
    TimestampMs start{0};
    TimestampMs end{0};
    bool empty() const noexcept { return end <= start; }
};

struct Candle {
    TimestampMs openTime{0};
    TimestampMs closeTime{0};
    double open{0};
    double high{0};
    double low{0};
    double close{0};
    double baseVolume{0};
    double quoteVolume{0};
    TradeCount trades{0};
    bool isClosed{false};
};

struct CandleSeries {
    Interval interval{};
    std::vector<Candle> data;
    TimestampMs firstOpen{0};
    TimestampMs lastOpen{0};

    bool empty() const noexcept { return data.empty(); }
    std::size_t size() const noexcept { return data.size(); }
};

struct LiveCandle {
    Candle candle;
    bool isFinal{false};
};

struct StreamError {
    int code{0};
    std::string message;
};

inline std::string interval_label(const Interval& interval) {
    if (!interval.valid()) {
        return "";
    }

    const auto ms = interval.ms;
    if (ms % 86'400'000 == 0) {
        const auto days = ms / 86'400'000;
        return std::to_string(days) + "d";
    }
    if (ms % 3'600'000 == 0) {
        const auto hours = ms / 3'600'000;
        return std::to_string(hours) + "h";
    }
    if (ms % 60'000 == 0) {
        const auto minutes = ms / 60'000;
        return std::to_string(minutes) + "m";
    }
    if (ms % 1'000 == 0) {
        const auto seconds = ms / 1'000;
        return std::to_string(seconds) + "s";
    }
    return std::to_string(ms) + "ms";
}

inline Interval interval_from_label(std::string_view label) {
    Interval interval{};
    if (label.empty()) {
        return interval;
    }

    std::size_t idx = 0;
    while (idx < label.size() && std::isspace(static_cast<unsigned char>(label[idx])) != 0) {
        ++idx;
    }

    std::size_t startDigits = idx;
    while (idx < label.size() && std::isdigit(static_cast<unsigned char>(label[idx])) != 0) {
        ++idx;
    }
    if (startDigits == idx) {
        return interval;
    }

    long long value = 0;
    try {
        value = std::stoll(std::string(label.substr(startDigits, idx - startDigits)));
    }
    catch (...) {
        return interval;
    }
    if (value <= 0) {
        return interval;
    }

    while (idx < label.size() && std::isspace(static_cast<unsigned char>(label[idx])) != 0) {
        ++idx;
    }

    long long multiplier = 1;
    if (idx < label.size()) {
        const char suffix = static_cast<char>(std::tolower(static_cast<unsigned char>(label[idx])));
        switch (suffix) {
        case 's':
            multiplier = 1'000;
            break;
        case 'm':
            multiplier = 60'000;
            break;
        case 'h':
            multiplier = 3'600'000;
            break;
        case 'd':
            multiplier = 86'400'000;
            break;
        default:
            break;
        }
    }

    interval.ms = value * multiplier;
    return interval;
}

}  // namespace domain

