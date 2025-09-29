#pragma once

#include <chrono>
#include <cstdint>
#include <limits>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

#include "domain/DomainContracts.h"
#include "domain/Types.h"
#include "infra/storage/PriceData.h"

namespace infra::storage {

struct Paths {
    std::string cacheDir;
};

class PriceDataTimeSeriesRepository : public domain::TimeSeriesRepository {
public:
    PriceDataTimeSeriesRepository();

    void bind(const std::string& symbol, domain::Interval interval, const Paths& paths);

    domain::Result<domain::CandleSeries> getLatest(std::size_t count) const override;
    domain::Result<domain::CandleSeries> getRange(domain::TimeRange range) const override;
    domain::AppendResult appendOrReplace(const domain::Candle& candle) override;
    domain::AppendResult appendBatch(const std::vector<domain::Candle>& batch) override;
    void flushIfNeeded(bool force = false);
    domain::RepoMetadata metadata() const override;
    domain::TimestampMs earliestOpenTime() const override;
    domain::TimestampMs latestOpenTime() const override;
    std::size_t candleCount() const override;
    bool hasGap() const override;
    domain::TimestampMs intervalMs() const override;
    domain::TimestampMs lastClosedOpenTime() const override;

    std::string currentSymbol() const;
    domain::Interval currentInterval() const;

private:
    mutable std::mutex mtx_;

    std::string symbol_;
    domain::Interval interval_{};
    std::string filePath_;
    bool bound_{false};

    std::unordered_set<std::int64_t> openIndex_;
    std::vector<domain::Candle> candles_;
    domain::RepoMetadata meta_{};
    bool hasGap_{false};
    domain::TimestampMs lastClosedOpen_{0};
    bool dirty_{false};
    std::chrono::steady_clock::time_point dirtySince_{};
    bool noDisk_{false};
    bool fastPathEnabled_{true};

    static std::string intervalToString(domain::Interval interval);
    static std::string makeFilePath(const std::string& cacheDir,
                                    const std::string& symbol,
                                    domain::Interval interval);

    void loadOrInitFileUnsafe_();
    void rebuildCacheFromDiskUnsafe_();
    void rewriteAllUnsafe_();
    void updateDerivedStateUnsafe_();
    bool flushIfNeededUnsafe_(bool force = false);
    void markDirtyUnsafe_();
    domain::AppendResult appendOrReplaceUnsafe_(domain::Candle candle);
    domain::Candle prepareCandleForAppend_(domain::Candle candle) const;
    domain::Candle recordToCandle(const PriceData& record) const;

    void appendRecordUnsafe_(const domain::Candle& candle);

    std::int64_t normalizeOpenTime(domain::TimestampMs openTime) const;
};

}  // namespace infra::storage

