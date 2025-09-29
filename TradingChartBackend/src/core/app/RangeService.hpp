#pragma once

#include <vector>

#include "domain/Types.h"

namespace core::app {

class ICandleRepository;

class RangeService {
public:
    explicit RangeService(ICandleRepository& repository) noexcept;

    [[nodiscard]] std::vector<domain::Candle> getCandles(const domain::Symbol& symbol,
                                                         domain::Interval interval,
                                                         domain::TimestampMs from,
                                                         domain::TimestampMs to,
                                                         std::size_t limit) const;

private:
    ICandleRepository& repository_;
};

}  // namespace core::app

