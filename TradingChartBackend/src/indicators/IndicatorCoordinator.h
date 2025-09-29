#pragma once

#include "indicators/IndicatorTypes.h"

#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>

namespace domain {
class CandleSeries;
class TimeSeriesRepository;
}

namespace indicators {

struct CacheKey {
    std::string seriesId;
    EmaParams params;

    bool operator==(const CacheKey& o) const;
};

struct CacheKeyHash {
    std::size_t operator()(const CacheKey& k) const;
};

struct CachedIndicator {
    SeriesVersion version;
    std::shared_ptr<const IndicatorSeries> series;
    std::optional<SeriesVersion> pendingVersion;
};

class IndicatorCoordinator {
public:
    explicit IndicatorCoordinator(std::shared_ptr<domain::TimeSeriesRepository> repo);

    std::shared_ptr<const IndicatorSeries> getEMA(const std::string& seriesId,
                                                  const domain::CandleSeries& candles,
                                                  const SeriesVersion& version,
                                                  const EmaParams& params,
                                                  bool allowAsyncRecompute);

    void invalidate(const std::string& seriesId);
    void invalidateAll();

private:
    void scheduleEmaCompute_(const CacheKey& key,
                             std::shared_ptr<domain::CandleSeries> candles,
                             std::size_t warmupPrefix,
                             std::size_t targetSize,
                             const SeriesVersion& version,
                             const EmaParams& params);

    std::shared_ptr<domain::TimeSeriesRepository> repo_;
    std::unordered_map<CacheKey, CachedIndicator, CacheKeyHash> cache_;
    std::mutex mtx_;
};

}  // namespace indicators

