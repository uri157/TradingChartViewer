#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "domain/Models.hpp"

namespace domain::contracts {

struct IntervalRangeInfo {
    std::string interval;
    std::optional<std::int64_t> fromTs;
    std::optional<std::int64_t> toTs;
};

class ICandleReadRepo {
public:
    virtual ~ICandleReadRepo() = default;

    virtual std::vector<Candle> getCandles(const Symbol& symbol,
                                           Interval interval,
                                           std::int64_t fromTs,
                                           std::int64_t toTs,
                                           std::size_t limit) const = 0;

    virtual std::vector<SymbolInfo> listSymbols() const {
        return {};
    }

    virtual std::optional<bool> symbolExists(const Symbol& symbol) const {
        (void)symbol;
        return std::nullopt;
    }

    virtual std::vector<IntervalRangeInfo> listSymbolIntervals(const Symbol& symbol) const {
        (void)symbol;
        return {};
    }

    virtual std::optional<std::pair<std::int64_t, std::int64_t>>
    get_min_max_ts(const Symbol& symbol, const std::string& interval) const {
        (void)symbol;
        (void)interval;
        return std::nullopt;
    }
};

class ILivePublisher {
public:
    virtual ~ILivePublisher() = default;

    virtual void publish(const Symbol& symbol, Interval interval, const Candle& candle) = 0;
};

}  // namespace domain::contracts

