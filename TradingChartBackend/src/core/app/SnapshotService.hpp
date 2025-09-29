#pragma once

#include <vector>

#include "domain/Types.h"

namespace core::app {

class ICandleRepository;

class SnapshotService {
public:
    explicit SnapshotService(ICandleRepository& repository) noexcept;

    [[nodiscard]] std::vector<domain::Candle> getSnapshot(const domain::Symbol& symbol,
                                                          domain::Interval interval,
                                                          std::size_t limit) const;

private:
    ICandleRepository& repository_;
};

}  // namespace core::app

