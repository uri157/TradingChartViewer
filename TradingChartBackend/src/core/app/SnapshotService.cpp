#include "core/app/SnapshotService.hpp"

#include "common/config.hpp"
#include "core/app/ICandleRepository.hpp"

namespace core::app {

SnapshotService::SnapshotService(ICandleRepository& repository) noexcept : repository_{repository} {}

std::vector<domain::Candle> SnapshotService::getSnapshot(const domain::Symbol& symbol,
                                                         domain::Interval interval,
                                                         std::size_t limit) const {
    if (!kHybridBackend) {
        return {};
    }

    return repository_.getSnapshot(symbol, interval, limit);
}

}  // namespace core::app

