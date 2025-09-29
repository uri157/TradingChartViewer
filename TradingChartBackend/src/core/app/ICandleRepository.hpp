#pragma once

#include <vector>

#include "domain/Types.h"

namespace core::app {

class ICandleRepository {
public:
    virtual ~ICandleRepository() = default;

    virtual std::vector<domain::Candle> getSnapshot(const domain::Symbol& symbol,
                                                    domain::Interval interval,
                                                    std::size_t limit) = 0;

    virtual std::vector<domain::Candle> getRange(const domain::Symbol& symbol,
                                                 domain::Interval interval,
                                                 domain::TimestampMs from,
                                                 domain::TimestampMs to,
                                                 std::size_t limit) = 0;
};

}  // namespace core::app

