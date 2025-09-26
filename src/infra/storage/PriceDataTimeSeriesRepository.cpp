#include "infra/storage/PriceDataTimeSeriesRepository.h"

#include "core/TimeUtils.h"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <string_view>
#include <utility>
#include <vector>

namespace infra::storage {

namespace {
domain::TimestampMs clampIntervalMs(const domain::Interval& interval) {
    if (interval.valid() && interval.ms > 0) {
        return interval.ms;
    }
    return core::TimeUtils::kMillisPerMinute;
}

domain::TimestampMs parseIntervalMs(std::string_view label) {
    if (label.empty()) {
        return 0;
    }

    std::size_t idx = 0;
    while (idx < label.size() && std::isspace(static_cast<unsigned char>(label[idx])) != 0) {
        ++idx;
    }
    std::size_t startDigits = idx;
    while (idx < label.size() && std::isdigit(static_cast<unsigned char>(label[idx])) != 0) {
        ++idx;
    }
    if (startDigits == idx) {
        return 0;
    }

    const std::string digits(label.substr(startDigits, idx - startDigits));
    long long value = std::strtoll(digits.c_str(), nullptr, 10);
    if (value <= 0) {
        return 0;
    }

    while (idx < label.size() && std::isspace(static_cast<unsigned char>(label[idx])) != 0) {
        ++idx;
    }

    if (idx >= label.size()) {
        return value;
    }

    const char suffix = static_cast<char>(std::tolower(static_cast<unsigned char>(label[idx])));
    switch (suffix) {
    case 's':
        return value * core::TimeUtils::kMillisPerSecond;
    case 'm':
        return value * core::TimeUtils::kMillisPerMinute;
    case 'h':
        return value * core::TimeUtils::kMillisPerMinute * 60;
    case 'd':
        return value * core::TimeUtils::kMillisPerMinute * 60 * 24;
    default:
        break;
    }

    return value;
}

}  // namespace

PriceDataTimeSeriesRepository::PriceDataTimeSeriesRepository(const std::string& filename,
                                                             std::string symbol,
                                                             domain::Interval interval)
    : manager_(filename),
      symbol_(std::move(symbol)),
      interval_(interval) {
    if (!interval_.valid()) {
        interval_.ms = core::TimeUtils::kMillisPerMinute;
    }
    intervalLabel_ = domain::interval_label(interval_);
    if (intervalLabel_.empty()) {
        intervalLabel_ = "1m";
    }
    intervalMs_ = clampIntervalMs(interval_);

    std::lock_guard<std::mutex> lock(mutex_);
    loadHistoricalLocked();
    refreshMetadataLocked();
}

domain::Result<domain::CandleSeries> PriceDataTimeSeriesRepository::getLatest(std::size_t count) const {
    std::lock_guard<std::mutex> lock(mutex_);

    domain::Result<domain::CandleSeries> result;
    result.value.interval = interval_;
    if (candles_.empty()) {
        return result;
    }

    const std::size_t start = (count >= candles_.size()) ? 0 : candles_.size() - count;
    result.value.data.assign(candles_.begin() + static_cast<std::ptrdiff_t>(start), candles_.end());
    if (!result.value.data.empty()) {
        result.value.firstOpen = result.value.data.front().openTime;
        result.value.lastOpen = result.value.data.back().openTime;
    }
    return result;
}

domain::Result<domain::CandleSeries> PriceDataTimeSeriesRepository::getRange(domain::TimeRange range) const {
    std::lock_guard<std::mutex> lock(mutex_);

    domain::Result<domain::CandleSeries> result;
    result.value.interval = interval_;
    if (range.empty() || candles_.empty()) {
        return result;
    }

    for (const auto& candle : candles_) {
        if (candle.openTime < range.start) {
            continue;
        }
        if (candle.openTime > range.end) {
            break;
        }
        result.value.data.push_back(candle);
    }

    if (!result.value.data.empty()) {
        result.value.firstOpen = result.value.data.front().openTime;
        result.value.lastOpen = result.value.data.back().openTime;
    }
    return result;
}

domain::AppendResult PriceDataTimeSeriesRepository::appendOrReplace(const domain::Candle& candle) {
    std::lock_guard<std::mutex> lock(mutex_);
    return appendOrReplaceLocked(candle);
}

domain::AppendResult PriceDataTimeSeriesRepository::appendBatch(const std::vector<domain::Candle>& batch) {
    domain::AppendResult summary;
    if (batch.empty()) {
        return summary;
    }

    std::vector<domain::Candle> ordered(batch.begin(), batch.end());
    std::sort(ordered.begin(), ordered.end(), [](const domain::Candle& lhs, const domain::Candle& rhs) {
        return lhs.openTime < rhs.openTime;
    });

    std::lock_guard<std::mutex> lock(mutex_);

    for (const auto& candle : ordered) {
        auto result = appendOrReplaceLocked(candle);
        summary.appended += result.appended;
        summary.touchedDisk = summary.touchedDisk || result.touchedDisk;
        summary.liveOnly = summary.liveOnly || result.liveOnly;

        if (result.state == domain::RangeState::Gap) {
            summary.state = domain::RangeState::Gap;
            summary.expected_from = result.expected_from;
            summary.expected_to = result.expected_to;
            break;
        }

        if (result.state == domain::RangeState::Overlap) {
            if (summary.state != domain::RangeState::Gap) {
                summary.state = domain::RangeState::Overlap;
            }
            continue;
        }

        if (result.state == domain::RangeState::Replaced && summary.state == domain::RangeState::Ok) {
            summary.state = domain::RangeState::Replaced;
        }
    }

    return summary;
}

domain::RepoMetadata PriceDataTimeSeriesRepository::metadata() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return metadata_;
}

domain::AppendResult PriceDataTimeSeriesRepository::sealOpenTailIfAny() {
    std::lock_guard<std::mutex> lock(mutex_);

    domain::AppendResult result;
    if (!hasTailOpenLocked()) {
        return result;
    }

    auto& tail = candles_.back();
    tail.isClosed = true;
    if (persistClosed(tail)) {
        refreshMetadataLocked();
        result.touchedDisk = true;
    }
    result.state = domain::RangeState::Replaced;
    return result;
}

bool PriceDataTimeSeriesRepository::hasGap() const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    return hasGap_;
}

std::size_t PriceDataTimeSeriesRepository::candleCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return candles_.size();
}

domain::TimestampMs PriceDataTimeSeriesRepository::intervalMs() const noexcept {
    return (intervalMs_ > 0) ? intervalMs_ : interval_.ms;
}

domain::TimestampMs PriceDataTimeSeriesRepository::lastClosedOpenTime() const {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto it = candles_.rbegin(); it != candles_.rend(); ++it) {
        if (it->isClosed) {
            return it->openTime;
        }
    }
    return metadata_.maxOpen;
}

domain::AppendResult PriceDataTimeSeriesRepository::appendOrReplaceLocked(const domain::Candle& candle) {
    domain::AppendResult result;

    auto normalized = normalizeCandle(candle);
    const auto record = toPriceData(normalized);
    if (!manager_.isValidRecord(record)) {
        return result;
    }

    bool touchedDisk = false;

    if (candles_.empty()) {
        candles_.push_back(normalized);
        result.appended = 1;
        if (normalized.isClosed) {
            touchedDisk = persistClosed(candles_.back());
            if (touchedDisk) {
                refreshMetadataLocked();
            }
        } else {
            result.liveOnly = true;
        }
        result.touchedDisk = touchedDisk;
        hasGap_ = false;
        return result;
    }

    auto& last = candles_.back();
    const auto lastOpen = last.openTime;
    const auto expected = lastOpen + intervalMs_;

    if (normalized.openTime == lastOpen) {
        if (last.isClosed && !normalized.isClosed) {
            result.state = domain::RangeState::Overlap;
            return result;
        }

        candles_.back() = normalized;
        result.state = domain::RangeState::Replaced;

        if (normalized.isClosed) {
            touchedDisk = persistClosed(candles_.back());
            if (touchedDisk) {
                refreshMetadataLocked();
            }
        } else {
            result.liveOnly = true;
        }

        result.touchedDisk = touchedDisk;
        hasGap_ = false;
        return result;
    }

    if (normalized.openTime < expected) {
        result.state = domain::RangeState::Overlap;
        return result;
    }

    if (normalized.openTime > expected) {
        hasGap_ = true;
        result.state = domain::RangeState::Gap;
        result.expected_from = expected;
        result.expected_to = normalized.openTime;
        return result;
    }

    if (!last.isClosed) {
        last.isClosed = true;
        touchedDisk = persistClosed(last) || touchedDisk;
    }

    candles_.push_back(normalized);
    result.appended = 1;

    if (normalized.isClosed) {
        touchedDisk = persistClosed(candles_.back()) || touchedDisk;
    } else {
        result.liveOnly = true;
    }

    if (touchedDisk) {
        refreshMetadataLocked();
    }

    result.touchedDisk = touchedDisk;
    hasGap_ = false;
    return result;
}

domain::Candle PriceDataTimeSeriesRepository::toCandle(const infra::storage::PriceData& record) const {
    domain::Candle candle{};
    const auto alignedOpen = domain::align_down_ms(record.openTime, intervalMs_);
    candle.openTime = alignedOpen;
    candle.closeTime = (alignedOpen > 0 && intervalMs_ > 0) ? alignedOpen + intervalMs_ - 1 : 0;
    candle.open = record.openPrice;
    candle.high = record.highPrice;
    candle.low = record.lowPrice;
    candle.close = record.closePrice;
    candle.baseVolume = record.volume;
    candle.quoteVolume = record.baseAssetVolume;
    candle.trades = static_cast<domain::TradeCount>(std::max(record.numberOfTrades, 0));
    candle.isClosed = true;
    return candle;
}

infra::storage::PriceData PriceDataTimeSeriesRepository::toPriceData(const domain::Candle& candle) const {
    infra::storage::PriceData record{};
    const auto alignedOpen = domain::align_down_ms(candle.openTime, intervalMs_);
    record.openTime = alignedOpen;
    record.closeTime = (alignedOpen > 0 && intervalMs_ > 0) ? alignedOpen + intervalMs_ - 1 : 0;
    record.openPrice = candle.open;
    record.highPrice = candle.high;
    record.lowPrice = candle.low;
    record.closePrice = candle.close;
    record.volume = candle.baseVolume;
    record.baseAssetVolume = candle.quoteVolume;
    record.numberOfTrades = static_cast<int>(candle.trades);
    record.takerBuyVolume = 0.0;
    record.takerBuyBaseAssetVolume = 0.0;
    std::strncpy(record.symbol, symbol_.c_str(), sizeof(record.symbol) - 1);
    record.symbol[sizeof(record.symbol) - 1] = '\0';
    std::strncpy(record.interval, intervalLabel_.c_str(), sizeof(record.interval) - 1);
    record.interval[sizeof(record.interval) - 1] = '\0';
    return record;
}

domain::Candle PriceDataTimeSeriesRepository::normalizeCandle(const domain::Candle& candle) const {
    domain::Candle normalized = candle;
    if (intervalMs_ > 0) {
        normalized.openTime = domain::align_down_ms(candle.openTime, intervalMs_);
        normalized.closeTime = (normalized.openTime > 0) ? normalized.openTime + intervalMs_ - 1 : 0;
    }
    return normalized;
}

void PriceDataTimeSeriesRepository::refreshMetadataLocked() {
    metadata_ = {};
    bool firstClosed = true;
    for (const auto& candle : candles_) {
        if (!candle.isClosed) {
            continue;
        }
        if (firstClosed) {
            metadata_.minOpen = candle.openTime;
            firstClosed = false;
        }
        metadata_.maxOpen = candle.openTime;
        ++metadata_.count;
    }
}

bool PriceDataTimeSeriesRepository::hasTailOpenLocked() const {
    return !candles_.empty() && !candles_.back().isClosed;
}

bool PriceDataTimeSeriesRepository::persistClosed(const domain::Candle& candle) {
    if (!candle.isClosed) {
        return false;
    }
    auto record = toPriceData(candle);
    if (!manager_.isValidRecord(record)) {
        return false;
    }
    std::vector<infra::storage::PriceData> records{record};
    manager_.saveRecords(records);
    return true;
}

void PriceDataTimeSeriesRepository::loadHistoricalLocked() {
    candles_.clear();
    auto records = manager_.readAllRecords();
    candles_.reserve(records.size());
    for (const auto& record : records) {
        if (!manager_.isValidRecord(record)) {
            continue;
        }
        if (record.interval[0] != '\0') {
            const auto parsed = parseIntervalMs(record.interval);
            if (parsed > 0) {
                intervalMs_ = parsed;
                interval_.ms = parsed;
                intervalLabel_ = record.interval;
            }
        }
        auto candle = toCandle(record);
        candle.isClosed = true;
        candles_.push_back(std::move(candle));
    }
    std::sort(candles_.begin(), candles_.end(), [](const domain::Candle& lhs, const domain::Candle& rhs) {
        return lhs.openTime < rhs.openTime;
    });
    if (intervalMs_ > 0) {
        interval_.ms = intervalMs_;
        if (intervalLabel_.empty()) {
            intervalLabel_ = domain::interval_label(interval_);
        }
    }
    hasGap_ = false;
}

}  // namespace infra::storage
