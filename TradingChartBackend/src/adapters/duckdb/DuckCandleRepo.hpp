#pragma once

#include <optional>
#include <string>
#include <vector>

#include "domain/Ports.hpp"

namespace domain {
struct Candle;
}  // namespace domain

namespace adapters::duckdb {

class DuckCandleRepo : public domain::contracts::ICandleReadRepo {
public:
    explicit DuckCandleRepo(std::string dbPath = "data/market.duckdb");

    std::vector<domain::contracts::Candle> getCandles(const domain::contracts::Symbol& symbol,
                                                      domain::contracts::Interval interval,
                                                      std::int64_t fromTs,
                                                      std::int64_t toTs,
                                                      std::size_t limit) const override;

    std::vector<domain::contracts::SymbolInfo> listSymbols() const override;

    std::optional<bool> symbolExists(const domain::contracts::Symbol& symbol) const override;

    std::vector<domain::contracts::IntervalRangeInfo>
    listSymbolIntervals(const domain::contracts::Symbol& symbol) const override;

    std::optional<std::pair<std::int64_t, std::int64_t>>
    get_min_max_ts(const std::string& symbol, const std::string& interval) const override;

    bool upsert_batch(const std::string& symbol,
                      const std::string& interval,
                      const std::vector<domain::Candle>& rows);

    std::optional<std::int64_t> max_timestamp(const std::string& symbol,
                                              const std::string& interval) const;

private:
    std::string dbPath_;
};

}  // namespace adapters::duckdb

