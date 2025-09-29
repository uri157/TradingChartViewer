#include "core/app/RangeService.hpp"

#include "common/config.hpp"
#include "core/app/ICandleRepository.hpp"

namespace core::app {

RangeService::RangeService(ICandleRepository& repository) noexcept : repository_{repository} {}

std::vector<domain::Candle> RangeService::getCandles(const domain::Symbol& symbol,
                                                     domain::Interval interval,
                                                     domain::TimestampMs from,
                                                     domain::TimestampMs to,
                                                     std::size_t limit) const {
    if (!kHybridBackend) {
        return {};
    }

    return repository_.getRange(symbol, interval, from, to, limit);
}

}  // namespace core::app

