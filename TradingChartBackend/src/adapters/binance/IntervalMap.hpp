#pragma once

#include <stdexcept>
#include <string>
#include <string_view>

#include "domain/Types.h"

namespace adapters::binance {

std::string binance_interval(domain::Interval interval);
domain::Interval from_binance_interval(const std::string &value);

namespace detail {

constexpr std::string_view binance_interval_literal(domain::Interval interval) {
    switch (interval.ms) {
    case 60'000:
        return "1m";
    case 5 * 60'000:
        return "5m";
    case 60 * 60'000:
        return "1h";
    case 24 * 60 * 60'000:
        return "1d";
    }
    throw std::invalid_argument("Unsupported domain interval");
}

constexpr domain::Interval from_binance_interval_literal(std::string_view value) {
    if (value == "1m") {
        return domain::Interval{60'000};
    }
    if (value == "5m") {
        return domain::Interval{5 * 60'000};
    }
    if (value == "1h") {
        return domain::Interval{60 * 60'000};
    }
    if (value == "1d") {
        return domain::Interval{24 * 60 * 60'000};
    }
    throw std::invalid_argument("Unsupported Binance interval");
}

} // namespace detail

static_assert(detail::binance_interval_literal(domain::Interval{60'000}) == std::string_view{"1m"});
static_assert(detail::binance_interval_literal(domain::Interval{5 * 60'000}) == std::string_view{"5m"});
static_assert(detail::from_binance_interval_literal("1h").ms == 60 * 60'000);
static_assert(detail::from_binance_interval_literal("1d").ms == 24 * 60 * 60'000);

} // namespace adapters::binance

