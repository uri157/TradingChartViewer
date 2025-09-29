#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "domain/Types.h"

namespace domain {

class SubscriptionHandle {
public:
    virtual ~SubscriptionHandle() = default;
    virtual void stop() = 0;
};

class MarketSource {
public:
    virtual ~MarketSource() = default;

    virtual std::vector<Candle> fetchRange(const Symbol& symbol,
                                           const Interval& interval,
                                           const TimeRange& range,
                                           std::size_t limit) = 0;

    virtual std::unique_ptr<SubscriptionHandle> streamLive(
        const Symbol& symbol,
        const Interval& interval,
        std::function<void(const LiveCandle&)> onData,
        std::function<void(const StreamError&)> onError) = 0;
};

}  // namespace domain

