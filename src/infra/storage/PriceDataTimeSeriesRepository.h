#pragma once

#include "infra/storage/PriceDataManager.h"
#include "domain/DomainContracts.h"

#include <mutex>
#include <string>
#include <vector>

namespace infra::storage {

// Adapter that maps PriceDataManager persistence to the domain::TimeSeriesRepository contract.
class PriceDataTimeSeriesRepository : public domain::TimeSeriesRepository {
public:
    PriceDataTimeSeriesRepository(const std::string& filename,
                                  std::string symbol,
                                  domain::Interval interval);

    domain::Result<domain::CandleSeries> getLatest(std::size_t count) const override;
    domain::Result<domain::CandleSeries> getRange(domain::TimeRange range) const override;
    domain::AppendResult appendOrReplace(const domain::Candle& candle) override;
    domain::AppendResult appendBatch(const std::vector<domain::Candle>& batch) override;
    domain::RepoMetadata metadata() const override;

    domain::AppendResult sealOpenTailIfAny();

    std::size_t candleCount() const override;
    bool hasGap() const noexcept override;
    domain::TimestampMs intervalMs() const noexcept override;
    domain::TimestampMs lastClosedOpenTime() const override;

private:
    void loadHistoricalLocked();
    domain::Candle toCandle(const infra::storage::PriceData& record) const;
    infra::storage::PriceData toPriceData(const domain::Candle& candle) const;
    domain::Candle normalizeCandle(const domain::Candle& candle) const;
    void refreshMetadataLocked();
    bool hasTailOpenLocked() const;
    bool persistClosed(const domain::Candle& candle);
    domain::AppendResult appendOrReplaceLocked(const domain::Candle& candle);

    mutable std::mutex mutex_;
    std::vector<domain::Candle> candles_;
    PriceDataManager manager_;
    std::string symbol_;
    domain::Interval interval_;
    domain::TimestampMs intervalMs_{0};
    std::string intervalLabel_;
    mutable domain::RepoMetadata metadata_{};
    bool hasGap_{false};
};

}  // namespace infra::storage
