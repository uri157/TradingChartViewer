#pragma once

#include <cctype>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace domain::contracts {

struct Candle {
    std::int64_t ts{0};
    double o{0.0};
    double h{0.0};
    double l{0.0};
    double c{0.0};
    double v{0.0};
};

enum class Interval {
    Unknown,
    OneMinute,
    ThreeMinutes,
    FiveMinutes,
    FifteenMinutes,
    ThirtyMinutes,
    OneHour,
    TwoHours,
    FourHours,
    SixHours,
    TwelveHours,
    OneDay,
    OneWeek,
};

inline std::string intervalToString(Interval interval) {
    switch (interval) {
    case Interval::OneMinute:
        return "1m";
    case Interval::ThreeMinutes:
        return "3m";
    case Interval::FiveMinutes:
        return "5m";
    case Interval::FifteenMinutes:
        return "15m";
    case Interval::ThirtyMinutes:
        return "30m";
    case Interval::OneHour:
        return "1h";
    case Interval::TwoHours:
        return "2h";
    case Interval::FourHours:
        return "4h";
    case Interval::SixHours:
        return "6h";
    case Interval::TwelveHours:
        return "12h";
    case Interval::OneDay:
        return "1d";
    case Interval::OneWeek:
        return "1w";
    case Interval::Unknown:
    default:
        break;
    }
    return "";
}

inline Interval intervalFromString(std::string_view value) {
    std::string normalized;
    normalized.reserve(value.size());
    for (char ch : value) {
        normalized.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
    }

    if (normalized == "1m" || normalized == "1min" || normalized == "1minute") {
        return Interval::OneMinute;
    }
    if (normalized == "3m" || normalized == "3min" || normalized == "3minute") {
        return Interval::ThreeMinutes;
    }
    if (normalized == "5m" || normalized == "5min" || normalized == "5minute") {
        return Interval::FiveMinutes;
    }
    if (normalized == "15m" || normalized == "15min" || normalized == "15minute") {
        return Interval::FifteenMinutes;
    }
    if (normalized == "30m" || normalized == "30min" || normalized == "30minute") {
        return Interval::ThirtyMinutes;
    }
    if (normalized == "1h" || normalized == "60m") {
        return Interval::OneHour;
    }
    if (normalized == "2h" || normalized == "120m") {
        return Interval::TwoHours;
    }
    if (normalized == "4h" || normalized == "240m") {
        return Interval::FourHours;
    }
    if (normalized == "6h" || normalized == "360m") {
        return Interval::SixHours;
    }
    if (normalized == "12h" || normalized == "720m") {
        return Interval::TwelveHours;
    }
    if (normalized == "1d" || normalized == "1day" || normalized == "24h") {
        return Interval::OneDay;
    }
    if (normalized == "1w" || normalized == "1week") {
        return Interval::OneWeek;
    }
    return Interval::Unknown;
}

using Symbol = std::string;

struct SymbolInfo {
    Symbol symbol;
    std::optional<std::string> base;
    std::optional<std::string> quote;
};

}  // namespace domain::contracts

