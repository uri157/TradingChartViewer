#include "indicators/IndicatorCoordinator.h"

#include "logging/Log.h"
#include "domain/DomainContracts.h"
#include "domain/Types.h"
#include "indicators/IndicatorEngine.h"

#include <algorithm>
#include <functional>
#include <thread>
#include <utility>
#include <limits>

namespace indicators {

namespace {

struct PreparedSeries {
    std::shared_ptr<domain::CandleSeries> owned;
    const domain::CandleSeries* view{nullptr};
    std::size_t warmup{0};
};

PreparedSeries prepareSeriesWithWarmup(const domain::CandleSeries& candles,
                                       const EmaParams& params,
                                       domain::TimeSeriesRepository* repo) {
    PreparedSeries prepared;
    prepared.view = &candles;
    if (!repo || params.period <= 1 || candles.data.empty()) {
        return prepared;
    }

    const auto intervalMs = candles.interval.valid() ? candles.interval.ms : repo->intervalMs();
    if (intervalMs <= 0) {
        return prepared;
    }

    const std::size_t warmupCount = static_cast<std::size_t>(params.period - 1);
    if (warmupCount == 0 || candles.firstOpen <= 0) {
        return prepared;
    }

    auto meta = repo->metadata();
    if (meta.count == 0 || meta.minOpen <= 0) {
        return prepared;
    }

    const domain::TimestampMs requiredStart = candles.firstOpen -
        static_cast<domain::TimestampMs>(warmupCount) * intervalMs;
    if (requiredStart >= candles.firstOpen) {
        return prepared;
    }

    domain::TimestampMs clampedStart = std::max<domain::TimestampMs>(meta.minOpen, requiredStart);
    if (clampedStart >= candles.firstOpen) {
        return prepared;
    }
    const domain::TimestampMs warmupEnd = candles.firstOpen - intervalMs;
    if (warmupEnd < clampedStart) {
        return prepared;
    }

    auto warmupRange = repo->getRange(domain::TimeRange{clampedStart, warmupEnd});
    if (warmupRange.failed() || warmupRange.value.data.empty()) {
        return prepared;
    }

    auto combined = std::make_shared<domain::CandleSeries>();
    combined->interval = candles.interval.valid() ? candles.interval : domain::Interval{intervalMs};
    combined->data.reserve(warmupRange.value.data.size() + candles.data.size());
    combined->data.insert(combined->data.end(), warmupRange.value.data.begin(), warmupRange.value.data.end());
    combined->data.insert(combined->data.end(), candles.data.begin(), candles.data.end());
    combined->firstOpen = combined->data.front().openTime;
    combined->lastOpen = combined->data.back().openTime;

    prepared.owned = std::move(combined);
    prepared.view = prepared.owned.get();
    prepared.warmup = warmupRange.value.data.size();
    return prepared;
}

IndicatorSeries trimWarmup(const IndicatorSeries& series, std::size_t warmup, std::size_t targetSize) {
    IndicatorSeries trimmed;
    trimmed.id = series.id;
    trimmed.values.assign(targetSize, std::numeric_limits<float>::quiet_NaN());
    if (warmup >= series.values.size()) {
        return trimmed;
    }
    const std::size_t available = series.values.size() - warmup;
    const std::size_t copyCount = std::min(targetSize, available);
    for (std::size_t i = 0; i < copyCount; ++i) {
        trimmed.values[i] = series.values[warmup + i];
    }
    return trimmed;
}

} // namespace

bool CacheKey::operator==(const CacheKey& o) const {
    return seriesId == o.seriesId && params.period == o.params.period;
}

std::size_t CacheKeyHash::operator()(const CacheKey& k) const {
    std::size_t seed = std::hash<std::string>{}(k.seriesId);
    seed ^= static_cast<std::size_t>(k.params.period) + 0x9e3779b97f4a7c15ULL + (seed << 6) + (seed >> 2);
    return seed;
}

IndicatorCoordinator::IndicatorCoordinator(std::shared_ptr<domain::TimeSeriesRepository> repo)
    : repo_(std::move(repo)) {}

std::shared_ptr<const IndicatorSeries> IndicatorCoordinator::getEMA(const std::string& seriesId,
                                                                    const domain::CandleSeries& candles,
                                                                    const SeriesVersion& version,
                                                                    const EmaParams& params,
                                                                    bool allowAsyncRecompute) {
    const CacheKey key{seriesId, params};
    const std::size_t targetSize = candles.data.size();
    if (targetSize == 0) {
        LOG_TRACE(logging::LogCategory::CACHE,
                  "EMA skipped for %s: empty candle set",
                  seriesId.c_str());
        return nullptr;
    }

    LOG_GUARD_RET(repo_, logging::LogCategory::CACHE, nullptr, "EMA skipped for %s: repository unavailable", seriesId.c_str());

    auto prepared = prepareSeriesWithWarmup(candles, params, repo_.get());
    const domain::CandleSeries& computeSeries = prepared.view ? *prepared.view : candles;
    const std::size_t warmupPrefix = prepared.warmup;
    std::shared_ptr<const IndicatorSeries> existing;
    bool computeSync = false;
    bool scheduleAsync = false;

    {
        std::lock_guard<std::mutex> lock(mtx_);
        auto it = cache_.find(key);
        if (it != cache_.end()) {
            existing = it->second.series;
            if (it->second.version == version && it->second.series &&
                it->second.series->values.size() == targetSize) {
                return it->second.series;
            }
            existing.reset();

            if (allowAsyncRecompute) {
                if (!it->second.pendingVersion || !(it->second.pendingVersion.value() == version)) {
                    it->second.pendingVersion = version;
                    scheduleAsync = true;
                }
            }
            else {
                it->second.pendingVersion.reset();
                computeSync = true;
            }
        }
        else {
            CachedIndicator slot;
            slot.version = SeriesVersion{};
            slot.series = nullptr;
            if (allowAsyncRecompute) {
                slot.pendingVersion = version;
                cache_.emplace(key, std::move(slot));
                scheduleAsync = true;
            }
            else {
                cache_.emplace(key, std::move(slot));
                computeSync = true;
            }
        }
    }

    if (computeSync) {
        std::shared_ptr<IndicatorSeries> updated;
        if (existing && warmupPrefix == 0 && existing->values.size() == targetSize) {
            IndicatorSeries copy = *existing;
            IndicatorEngine::updateEMAIncremental(candles, params, copy);
            if (copy.values.size() == candles.data.size()) {
                updated = std::make_shared<IndicatorSeries>(std::move(copy));
            }
        }

        if (!updated) {
            auto computed = IndicatorEngine::computeEMA(computeSeries, params);
            IndicatorSeries trimmed = trimWarmup(computed, warmupPrefix, targetSize);
            updated = std::make_shared<IndicatorSeries>(std::move(trimmed));
        }

        {
            std::lock_guard<std::mutex> lock(mtx_);
            auto it = cache_.find(key);
            if (it == cache_.end()) {
                cache_.emplace(key, CachedIndicator{version, updated, std::nullopt});
            }
            else {
                it->second.series = updated;
                it->second.version = version;
                it->second.pendingVersion.reset();
            }
        }

        LOG_DEBUG(logging::LogCategory::CACHE,
                  "EMA sync compute for %s period=%d candles=%zu",
                  seriesId.c_str(),
                  params.period,
                  static_cast<std::size_t>(candles.data.size()));
        return updated;
    }

    if (scheduleAsync) {
        if (targetSize < static_cast<std::size_t>(params.period)) {
            LOG_TRACE(logging::LogCategory::CACHE,
                      "EMA async skipped for %s: insufficient candles=%zu < period=%d",
                      seriesId.c_str(),
                      targetSize,
                      params.period);
            return existing;
        }
        std::shared_ptr<domain::CandleSeries> computeCopy;
        if (prepared.owned) {
            computeCopy = prepared.owned;
        } else {
            computeCopy = std::make_shared<domain::CandleSeries>(candles);
        }
        scheduleEmaCompute_(key,
                             std::move(computeCopy),
                             warmupPrefix,
                             targetSize,
                             version,
                             params);
    }

    return existing;
}

void IndicatorCoordinator::invalidate(const std::string& seriesId) {
    std::lock_guard<std::mutex> lock(mtx_);
    for (auto it = cache_.begin(); it != cache_.end();) {
        if (it->first.seriesId == seriesId) {
            it = cache_.erase(it);
        }
        else {
            ++it;
        }
    }
}

void IndicatorCoordinator::invalidateAll() {
    std::lock_guard<std::mutex> lock(mtx_);
    cache_.clear();
}

void IndicatorCoordinator::scheduleEmaCompute_(const CacheKey& key,
                                               std::shared_ptr<domain::CandleSeries> candles,
                                               std::size_t warmupPrefix,
                                               std::size_t targetSize,
                                               const SeriesVersion& version,
                                               const EmaParams& params) {
    LOG_GUARD(candles, logging::LogCategory::CACHE, "Skipping EMA schedule for %s: null candles", key.seriesId.c_str());
    if (candles->data.empty()) {
        LOG_TRACE(logging::LogCategory::CACHE,
                  "Skipping EMA schedule for %s: empty candles",
                  key.seriesId.c_str());
        return;
    }

    if (candles->data.size() < static_cast<std::size_t>(params.period)) {
        LOG_TRACE(logging::LogCategory::CACHE,
                  "Skipping EMA schedule for %s: insufficient candles=%zu < period=%d",
                  key.seriesId.c_str(),
                  static_cast<std::size_t>(candles->data.size()),
                  params.period);
        return;
    }

    LOG_TRACE(logging::LogCategory::CACHE,
              "Scheduling EMA async compute for %s period=%d",
              key.seriesId.c_str(),
              params.period);

    std::thread([this, key, candles = std::move(candles), warmupPrefix, targetSize, version, params]() {
        IndicatorSeries computed = IndicatorEngine::computeEMA(*candles, params);
        IndicatorSeries trimmed = trimWarmup(computed, warmupPrefix, targetSize);
        auto finalSeries = std::make_shared<IndicatorSeries>(std::move(trimmed));
        {
            std::lock_guard<std::mutex> lock(mtx_);
            auto it = cache_.find(key);
            if (it == cache_.end()) {
                cache_.emplace(key, CachedIndicator{version, finalSeries, std::nullopt});
            }
            else {
                it->second.series = finalSeries;
                it->second.version = version;
                it->second.pendingVersion.reset();
            }
        }
        LOG_DEBUG(logging::LogCategory::CACHE,
                  "EMA async compute finished for %s period=%d candles=%zu",
                  key.seriesId.c_str(),
                  params.period,
                  static_cast<std::size_t>(candles->data.size()));
    }).detach();
}

}  // namespace indicators

