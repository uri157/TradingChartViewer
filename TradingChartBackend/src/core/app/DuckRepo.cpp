#include "core/app/DuckRepo.hpp"

#include <algorithm>
#include <cstddef>
#include <stdexcept>
#include <utility>

#include "adapters/persistence/ICandleRepository.hpp"
#include "adapters/persistence/duckdb/DuckRepo.hpp"
#include "domain/Types.h"

namespace {
std::string intervalLabel(const domain::Interval& interval) {
    if (!interval.valid()) {
        return {};
    }
    return domain::interval_label(interval);
}

std::vector<domain::Candle> convertRows(const std::vector<adapters::persistence::CandleRow>& rows,
                                        domain::Interval interval) {
    std::vector<domain::Candle> candles;
    candles.reserve(rows.size());
    for (const auto& row : rows) {
        domain::Candle candle{};
        candle.openTime = row.openMs;
        candle.closeTime = interval.valid() ? row.openMs + interval.ms : row.openMs;
        candle.open = row.open;
        candle.high = row.high;
        candle.low = row.low;
        candle.close = row.close;
        candle.baseVolume = row.volume;
        candle.quoteVolume = 0.0;
        candle.trades = 0;
        candle.isClosed = true;
        candles.push_back(candle);
    }
    return candles;
}
}  // namespace

namespace core::app {

DuckRepo::DuckRepo(std::string dbPath)
    : DuckRepo(std::make_unique<adapters::persistence::duckdb_adapter::DuckRepo>(std::move(dbPath))) {}

DuckRepo::DuckRepo(std::unique_ptr<adapters::persistence::duckdb_adapter::DuckRepo> repo)
    : repo_{*repo}, ownedRepo_{std::move(repo)} {
    if (ownedRepo_ == nullptr) {
        throw std::invalid_argument("DuckRepo: repository cannot be null");
    }
    ownedRepo_->init();
}

DuckRepo::~DuckRepo() = default;

std::vector<domain::Candle> DuckRepo::getSnapshot(const domain::Symbol& symbol,
                                                  domain::Interval interval,
                                                  std::size_t limit) {
    if (!interval.valid() || limit == 0) {
        return {};
    }

    const auto label = intervalLabel(interval);
    if (label.empty()) {
        return {};
    }

    auto rows = repo_.getLastN(symbol, label, limit);
    auto candles = convertRows(rows, interval);
    if (limit < candles.size()) {
        const auto trimCount = candles.size() - limit;
        candles.erase(candles.begin(), candles.begin() + static_cast<std::ptrdiff_t>(trimCount));
    }
    return candles;
}

std::vector<domain::Candle> DuckRepo::getRange(const domain::Symbol& symbol,
                                               domain::Interval interval,
                                               domain::TimestampMs from,
                                               domain::TimestampMs to,
                                               std::size_t limit) {
    if (!interval.valid() || limit == 0 || from >= to) {
        return {};
    }

    const auto label = intervalLabel(interval);
    if (label.empty()) {
        return {};
    }

    auto rows = repo_.getRange(symbol, label, from, to);
    auto candles = convertRows(rows, interval);
    if (limit < candles.size()) {
        candles.resize(limit);
    }
    return candles;
}

}  // namespace core::app

