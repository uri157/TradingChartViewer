#include "infra/storage/PriceDataTimeSeriesRepository.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string_view>
#include <utility>

#include "logging/Log.h"
#include "metrics/RepoFastPathDiag.h"

namespace fs = std::filesystem;

namespace infra::storage {

namespace {

constexpr logging::LogCategory kLogCategory = logging::LogCategory::DB;

struct PreparedBatch {
    std::vector<domain::Candle> candles;
    bool contiguous{false};
    bool strictlyIncreasing{true};
};

PreparedBatch prepareBatch(const std::vector<domain::Candle>& batch, std::int64_t intervalMs) {
    PreparedBatch prepared;
    prepared.candles.reserve(batch.size());

    bool first = true;
    domain::TimestampMs previousOpen = 0;
    prepared.contiguous = intervalMs > 0;
    prepared.strictlyIncreasing = true;

    for (const auto& rawCandle : batch) {
        domain::Candle candle = rawCandle;
        if (intervalMs > 0) {
            const auto normalizedOpen = domain::align_down_ms(candle.openTime, intervalMs);
            candle.openTime = normalizedOpen;
            if (normalizedOpen > 0) {
                candle.closeTime = normalizedOpen + intervalMs - 1;
            }
            candle.isClosed = candle.isClosed ||
                              (candle.closeTime >= candle.openTime + intervalMs - 1);
        }

        const auto normalizedOpen = candle.openTime;
        if (first) {
            first = false;
        }
        else {
            if (intervalMs > 0) {
                prepared.contiguous =
                    prepared.contiguous && (normalizedOpen == previousOpen + intervalMs);
            }
            prepared.strictlyIncreasing =
                prepared.strictlyIncreasing && (normalizedOpen > previousOpen);
        }

        previousOpen = normalizedOpen;
        prepared.candles.push_back(std::move(candle));
    }

    if (prepared.candles.size() <= 1) {
        prepared.contiguous = intervalMs > 0;
        prepared.strictlyIncreasing = true;
    }

    return prepared;
}

PriceData makeRecord(const domain::Candle& candle,
                     const std::string& symbol,
                     const std::string& intervalLabel,
                     domain::TimestampMs intervalMs) {
    PriceData record{};
    record.openTime = candle.openTime;
    if (intervalMs > 0 && candle.openTime > 0) {
        record.openTime = domain::align_down_ms(candle.openTime, intervalMs);
        record.closeTime = record.openTime + intervalMs - 1;
    }
    else {
        record.closeTime = candle.closeTime;
    }

    record.openPrice = candle.open;
    record.highPrice = candle.high;
    record.lowPrice = candle.low;
    record.closePrice = candle.close;
    record.volume = candle.baseVolume;
    record.baseAssetVolume = candle.quoteVolume;
    record.numberOfTrades = static_cast<int>(candle.trades);
    record.takerBuyVolume = 0.0;
    record.takerBuyBaseAssetVolume = 0.0;

    std::strncpy(record.symbol, symbol.c_str(), sizeof(record.symbol) - 1);
    record.symbol[sizeof(record.symbol) - 1] = '\0';
    std::strncpy(record.interval, intervalLabel.c_str(), sizeof(record.interval) - 1);
    record.interval[sizeof(record.interval) - 1] = '\0';

    return record;
}

bool matchesDataset(const PriceData& record,
                    const std::string& symbol,
                    const std::string& intervalLabel) {
    const auto symbolEnd =
        std::find(record.symbol, record.symbol + sizeof(record.symbol), '\0');
    const auto intervalEnd =
        std::find(record.interval, record.interval + sizeof(record.interval), '\0');
    const std::string_view recordSymbol(record.symbol,
                                        static_cast<std::size_t>(symbolEnd - record.symbol));
    const std::string_view recordInterval(
        record.interval, static_cast<std::size_t>(intervalEnd - record.interval));
    const bool symbolMatches = symbol.empty() || recordSymbol == symbol;
    const bool intervalMatches = intervalLabel.empty() || recordInterval == intervalLabel;
    return symbolMatches && intervalMatches;
}

}  // namespace

PriceDataTimeSeriesRepository::PriceDataTimeSeriesRepository() {
    const char* env = std::getenv("TTP_NO_DISK");
    if (env != nullptr && std::string_view(env) == "1") {
        noDisk_ = true;
        LOG_WARN(kLogCategory, "DB: NO_DISK mode enabled (diagnostic)");
    }

    fastPathEnabled_ = metrics::repoFastPathEnabled();
    if (!fastPathEnabled_) {
        LOG_WARN(kLogCategory, "DB: repo fast path disabled via TTP_REPO_FASTPATH");
    }
    else if (metrics::repoFastPathDiagEnabled()) {
        LOG_INFO(kLogCategory, "DB: repo fast path diagnostics enabled");
    }
}

std::string PriceDataTimeSeriesRepository::intervalToString(domain::Interval interval) {
    const auto label = domain::interval_label(interval);
    if (!label.empty()) {
        return label;
    }
    if (interval.ms > 0) {
        return std::to_string(interval.ms) + "ms";
    }
    return "unk";
}

std::string PriceDataTimeSeriesRepository::makeFilePath(const std::string& cacheDir,
                                                        const std::string& symbol,
                                                        domain::Interval interval) {
    fs::path base(cacheDir.empty() ? fs::path{"."} : fs::path{cacheDir});
    base /= symbol + "_" + intervalToString(interval) + "_timeseries.bin";
    return base.string();
}

void PriceDataTimeSeriesRepository::bind(const std::string& symbol,
                                         domain::Interval interval,
                                         const Paths& paths) {
    std::scoped_lock lock(mtx_);

    const auto targetPath = makeFilePath(paths.cacheDir, symbol, interval);
    if (bound_ && symbol_ == symbol && interval_.ms == interval.ms && filePath_ == targetPath) {
        LOG_INFO(kLogCategory,
                 "DB: repo bind (noop) symbol=%s interval=%s path=%s",
                 symbol.c_str(),
                 intervalToString(interval).c_str(),
                 targetPath.c_str());
        return;
    }

    symbol_ = symbol;
    interval_ = interval;
    filePath_ = targetPath;
    bound_ = true;

    const auto parent = fs::path(filePath_).parent_path();
    if (!parent.empty()) {
        std::error_code ec;
        fs::create_directories(parent, ec);
        if (ec) {
            LOG_WARN(kLogCategory,
                     "DB: failed to ensure directory %s (%s)",
                     parent.string().c_str(),
                     ec.message().c_str());
        }
    }

    openIndex_.clear();
    candles_.clear();
    meta_ = {};
    hasGap_ = false;
    lastClosedOpen_ = 0;
    dirty_ = false;
    dirtySince_ = {};

    loadOrInitFileUnsafe_();

    LOG_INFO(kLogCategory,
             "DB: repo bind symbol=%s interval=%s path=%s",
             symbol_.c_str(),
             intervalToString(interval_).c_str(),
             filePath_.c_str());
}

domain::Result<domain::CandleSeries> PriceDataTimeSeriesRepository::getLatest(std::size_t count) const {
    metrics::RepoFastPathTimer diagTimer{"repo.getLatest"};
    metrics::RepoFastPathLatencyTimer observeGuard{"repo.getLatest.nanos"};
    std::scoped_lock lock(mtx_);
    domain::Result<domain::CandleSeries> out;
    out.value.interval = interval_;
    if (candles_.empty()) {
        return out;
    }

    const std::size_t startIndex = (count >= candles_.size()) ? 0 : candles_.size() - count;
    auto offset = static_cast<std::vector<domain::Candle>::difference_type>(startIndex);
    out.value.data.assign(candles_.begin() + offset, candles_.end());
    out.value.firstOpen = out.value.data.front().openTime;
    out.value.lastOpen = out.value.data.back().openTime;
    return out;
}

domain::Result<domain::CandleSeries> PriceDataTimeSeriesRepository::getRange(domain::TimeRange range) const {
    std::scoped_lock lock(mtx_);
    domain::Result<domain::CandleSeries> out;
    out.value.interval = interval_;
    if (candles_.empty() || range.empty()) {
        return out;
    }

    auto lower = std::lower_bound(candles_.begin(), candles_.end(), range.start,
                                  [](const domain::Candle& candle, domain::TimestampMs value) {
                                      return candle.openTime < value;
                                  });
    for (auto it = lower; it != candles_.end(); ++it) {
        if (range.end > 0 && it->openTime > range.end) {
            break;
        }
        out.value.data.push_back(*it);
    }

    if (!out.value.data.empty()) {
        out.value.firstOpen = out.value.data.front().openTime;
        out.value.lastOpen = out.value.data.back().openTime;
    }
    return out;
}

domain::AppendResult PriceDataTimeSeriesRepository::appendOrReplace(const domain::Candle& candle) {
    metrics::RepoFastPathTimer diagTimer{"repo.appendOrReplace"};
    std::scoped_lock lock(mtx_);
    if (!bound_) {
        LOG_ERROR(kLogCategory, "DB: appendOrReplace called before bind()");
        domain::AppendResult result;
        result.state = domain::RangeState::Gap;
        return result;
    }
    return appendOrReplaceUnsafe_(candle);
}

domain::AppendResult PriceDataTimeSeriesRepository::appendBatch(const std::vector<domain::Candle>& batch) {
    metrics::RepoFastPathTimer diagTimer{"repo.appendBatch"};
    domain::AppendResult summary;
    if (batch.empty()) {
        return summary;
    }

    auto intervalMsSnapshot = interval_.ms;
    auto prepared = prepareBatch(batch, intervalMsSnapshot);
    if (prepared.candles.empty()) {
        return summary;
    }

    for (;;) {
        std::unique_lock lock(mtx_);
        if (!bound_) {
            LOG_ERROR(kLogCategory, "DB: appendBatch called before bind()");
            summary.state = domain::RangeState::Gap;
            return summary;
        }

        if (interval_.ms != intervalMsSnapshot) {
            intervalMsSnapshot = interval_.ms;
            lock.unlock();
            prepared = prepareBatch(batch, intervalMsSnapshot);
            if (prepared.candles.empty()) {
                return summary;
            }
            continue;
        }

        metrics::RepoFastPathLatencyTimer lockTimer{"repo.lock.appendBatch"};

        const bool hadGapBefore = hasGap_;
        const auto intervalMs = intervalMsSnapshot;
        domain::TimestampMs lastKnownMaxOpen = candles_.empty() ? 0 : meta_.maxOpen;
        bool derivedDirty = false;
        bool needRewriteAll = false;
        bool needsMarkDirty = false;
        std::uint64_t slowPathInserts = 0;

        if (!candles_.empty() && intervalMs > 0) {
            const auto expectedNext = meta_.maxOpen + intervalMs;
            if (prepared.candles.front().openTime > expectedNext) {
                summary.state = domain::RangeState::Gap;
                summary.expected_from = expectedNext;
                summary.expected_to = prepared.candles.front().openTime;
                return summary;
            }
        }

        const bool repoEmpty = candles_.empty();
        const bool canFastPath = prepared.contiguous && prepared.strictlyIncreasing && intervalMs > 0 &&
                                 (repoEmpty || prepared.candles.front().openTime == meta_.maxOpen + intervalMs);

        if (fastPathEnabled_ && canFastPath) {
            metrics::RepoFastPathTimer fastTimer{"repo.appendBatch.fast"};
            metrics::repoFastPathIncr("repo.fast_path.appends");

            const auto previousCount = candles_.size();
            candles_.reserve(previousCount + prepared.candles.size());
            candles_.insert(candles_.end(), prepared.candles.begin(), prepared.candles.end());

            if (previousCount == 0) {
                meta_.minOpen = prepared.candles.front().openTime;
            }
            meta_.count = candles_.size();
            meta_.maxOpen = prepared.candles.back().openTime;

            if (repoEmpty) {
                hasGap_ = false;
            }

            bool touchedDisk = false;
            bool anyLive = false;
            for (const auto& candle : prepared.candles) {
                openIndex_.insert(candle.openTime);
                if (candle.isClosed) {
                    lastClosedOpen_ = candle.openTime;
                }
                else {
                    anyLive = true;
                }

                appendRecordUnsafe_(candle);
                if (!noDisk_ && !filePath_.empty()) {
                    touchedDisk = true;
                }
            }

            summary.state = domain::RangeState::Ok;
            summary.appended = prepared.candles.size();
            summary.touchedDisk = touchedDisk;
            summary.liveOnly = anyLive;

            const bool gapClosed = hadGapBefore && !hasGap_;
            const bool flushed = flushIfNeededUnsafe_(gapClosed);
            summary.touchedDisk = summary.touchedDisk || flushed;
            return summary;
        }

        for (const auto& candle : prepared.candles) {
            metrics::RepoFastPathTimer perCandleTimer{"repo.appendOne"};
            auto preparedCandle = prepareCandleForAppend_(candle);
            const auto normalizedOpen = preparedCandle.openTime;

            if (!candles_.empty() && intervalMs > 0 && normalizedOpen > lastKnownMaxOpen + intervalMs) {
                summary = {};
                summary.state = domain::RangeState::Gap;
                summary.expected_from = lastKnownMaxOpen + intervalMs;
                summary.expected_to = normalizedOpen;
                break;
            }

            auto it = std::lower_bound(candles_.begin(), candles_.end(), normalizedOpen,
                                       [](const domain::Candle& lhs, domain::TimestampMs value) {
                                           return lhs.openTime < value;
                                       });

            if (it != candles_.end() && it->openTime == normalizedOpen) {
                metrics::repoFastPathIncr("repo.rewriteAll.calls");
                *it = preparedCandle;
                derivedDirty = true;
                needsMarkDirty = true;
                lastKnownMaxOpen = std::max(lastKnownMaxOpen, normalizedOpen);
                summary.appended += 1;
                summary.liveOnly = summary.liveOnly || !preparedCandle.isClosed;
                summary.state = domain::RangeState::Replaced;
                continue;
            }

            const bool insertAtEnd = (it == candles_.end());
            candles_.insert(it, preparedCandle);
            derivedDirty = true;
            lastKnownMaxOpen = std::max(lastKnownMaxOpen, normalizedOpen);
            summary.appended += 1;
            summary.liveOnly = summary.liveOnly || !preparedCandle.isClosed;
            summary.state = domain::RangeState::Ok;

            if (insertAtEnd) {
                appendRecordUnsafe_(preparedCandle);
                summary.touchedDisk = summary.touchedDisk || (!noDisk_);
            }
            else {
                needRewriteAll = true;
                metrics::repoFastPathIncr("repo.rewriteAll.calls");
                ++slowPathInserts;
            }
        }

        if (derivedDirty) {
            metrics::RepoFastPathTimer postBatchTimer{"repo.updateDerived.postBatch"};
            updateDerivedStateUnsafe_();
        }

        if (needRewriteAll) {
            metrics::repoFastPathIncr("repo.slow_path.inserts", slowPathInserts);
            rewriteAllUnsafe_();
            summary.touchedDisk = summary.touchedDisk || (!noDisk_);
        }
        else if (needsMarkDirty) {
            markDirtyUnsafe_();
        }

        const bool gapClosed = hadGapBefore && !hasGap_;
        const bool flushed = flushIfNeededUnsafe_(gapClosed);
        summary.touchedDisk = summary.touchedDisk || flushed;
        return summary;
    }
}

domain::RepoMetadata PriceDataTimeSeriesRepository::metadata() const {
    std::scoped_lock lock(mtx_);
    return meta_;
}

domain::TimestampMs PriceDataTimeSeriesRepository::earliestOpenTime() const {
    std::scoped_lock lock(mtx_);
    return meta_.minOpen;
}

domain::TimestampMs PriceDataTimeSeriesRepository::latestOpenTime() const {
    std::scoped_lock lock(mtx_);
    return meta_.maxOpen;
}

std::size_t PriceDataTimeSeriesRepository::candleCount() const {
    std::scoped_lock lock(mtx_);
    return candles_.size();
}

bool PriceDataTimeSeriesRepository::hasGap() const {
    std::scoped_lock lock(mtx_);
    return hasGap_;
}

domain::TimestampMs PriceDataTimeSeriesRepository::intervalMs() const {
    return interval_.ms;
}

domain::TimestampMs PriceDataTimeSeriesRepository::lastClosedOpenTime() const {
    std::scoped_lock lock(mtx_);
    return lastClosedOpen_;
}

std::string PriceDataTimeSeriesRepository::currentSymbol() const {
    std::scoped_lock lock(mtx_);
    return symbol_;
}

domain::Interval PriceDataTimeSeriesRepository::currentInterval() const {
    std::scoped_lock lock(mtx_);
    return interval_;
}

void PriceDataTimeSeriesRepository::loadOrInitFileUnsafe_() {
    if (filePath_.empty()) {
        return;
    }

    if (!fs::exists(filePath_)) {
        std::ofstream ofs(filePath_, std::ios::binary | std::ios::trunc);
        if (!ofs.good()) {
            LOG_ERROR(kLogCategory, "DB: failed to create %s", filePath_.c_str());
            return;
        }
    }

    rebuildCacheFromDiskUnsafe_();
}

void PriceDataTimeSeriesRepository::rebuildCacheFromDiskUnsafe_() {
    candles_.clear();
    openIndex_.clear();
    meta_ = {};
    hasGap_ = false;
    lastClosedOpen_ = 0;
    dirty_ = false;
    dirtySince_ = {};

    if (filePath_.empty()) {
        return;
    }

    std::ifstream ifs(filePath_, std::ios::binary);
    if (!ifs.good()) {
        LOG_WARN(kLogCategory,
                 "DB: failed to open %s for reading; assuming empty",
                 filePath_.c_str());
        return;
    }

    const auto intervalLabel = intervalToString(interval_);
    PriceData record{};
    while (ifs.read(reinterpret_cast<char*>(&record), sizeof(record))) {
        if (record.openTime <= 0) {
            continue;
        }
        if (!matchesDataset(record, symbol_, intervalLabel)) {
            continue;
        }
        candles_.push_back(recordToCandle(record));
    }

    updateDerivedStateUnsafe_();
}

void PriceDataTimeSeriesRepository::rewriteAllUnsafe_() {
    metrics::RepoFastPathTimer diagTimer{"repo.rewriteAll"};
    metrics::RepoFastPathLatencyTimer duration{"repo.rewriteAll.nanos"};
    if (filePath_.empty()) {
        return;
    }

    if (noDisk_) {
        metrics::repoFastPathIncr("repo.disk.writes.skipped");
        dirty_ = false;
        dirtySince_ = {};
        return;
    }

    std::ofstream ofs(filePath_, std::ios::binary | std::ios::trunc);
    if (!ofs.good()) {
        LOG_ERROR(kLogCategory, "DB: failed to open %s for rewrite", filePath_.c_str());
        return;
    }

    const std::uint64_t estimatedBytes = static_cast<std::uint64_t>(candles_.size()) * sizeof(PriceData);
    if (estimatedBytes > 0) {
        metrics::repoFastPathIncr("repo.rewriteAll.bytes", estimatedBytes);
    }
    const auto intervalLabel = intervalToString(interval_);
    const auto intervalMs = interval_.ms;
    for (const auto& candle : candles_) {
        const auto record = makeRecord(candle, symbol_, intervalLabel, intervalMs);
        ofs.write(reinterpret_cast<const char*>(&record), sizeof(record));
    }
    ofs.flush();
    dirty_ = false;
    dirtySince_ = {};
}

void PriceDataTimeSeriesRepository::markDirtyUnsafe_() {
    if (!dirty_) {
        dirty_ = true;
        dirtySince_ = std::chrono::steady_clock::now();
    }
}

bool PriceDataTimeSeriesRepository::flushIfNeededUnsafe_(bool force) {
    if (!dirty_) {
        return false;
    }
    if (!force) {
        const auto now = std::chrono::steady_clock::now();
        if (dirtySince_ == std::chrono::steady_clock::time_point{}) {
            dirtySince_ = now;
            return false;
        }
        if (now - dirtySince_ < std::chrono::milliseconds(500)) {
            return false;
        }
    }
    rewriteAllUnsafe_();
    return !noDisk_;
}

void PriceDataTimeSeriesRepository::flushIfNeeded(bool force) {
    std::scoped_lock lock(mtx_);
    flushIfNeededUnsafe_(force);
}

void PriceDataTimeSeriesRepository::updateDerivedStateUnsafe_() {
    metrics::RepoFastPathTimer diagTimer{"repo.updateDerived"};
    metrics::repoFastPathIncr("repo.updateDerived.count");
    std::sort(candles_.begin(), candles_.end(), [](const domain::Candle& lhs, const domain::Candle& rhs) {
        return lhs.openTime < rhs.openTime;
    });

    openIndex_.clear();
    meta_ = {};
    hasGap_ = false;
    lastClosedOpen_ = 0;

    if (candles_.empty()) {
        return;
    }

    meta_.count = candles_.size();
    meta_.minOpen = candles_.front().openTime;
    meta_.maxOpen = candles_.back().openTime;

    const auto intervalMs = interval_.ms;
    domain::TimestampMs previousOpen = 0;
    bool first = true;
    for (const auto& candle : candles_) {
        openIndex_.insert(candle.openTime);
        if (candle.isClosed) {
            lastClosedOpen_ = candle.openTime;
        }
        if (!first && intervalMs > 0) {
            const auto expected = previousOpen + intervalMs;
            if (candle.openTime > expected) {
                hasGap_ = true;
            }
        }
        previousOpen = candle.openTime;
        first = false;
    }
}

domain::Candle PriceDataTimeSeriesRepository::prepareCandleForAppend_(domain::Candle candle) const {
    const auto normalizedOpen = normalizeOpenTime(candle.openTime);
    candle.openTime = normalizedOpen;
    if (interval_.ms > 0 && normalizedOpen > 0) {
        candle.closeTime = normalizedOpen + interval_.ms - 1;
    }
    candle.isClosed = candle.isClosed ||
                      (interval_.ms > 0 && candle.closeTime >= candle.openTime + interval_.ms - 1);
    return candle;
}

domain::AppendResult PriceDataTimeSeriesRepository::appendOrReplaceUnsafe_(domain::Candle candle) {
    metrics::RepoFastPathTimer diagTimer{"repo.appendOrReplaceUnsafe"};
    domain::AppendResult result;

    candle = prepareCandleForAppend_(candle);
    const auto normalizedOpen = candle.openTime;

    if (!candles_.empty() && interval_.ms > 0) {
        const auto expectedNext = meta_.maxOpen + interval_.ms;
        if (normalizedOpen > expectedNext) {
            result.state = domain::RangeState::Gap;
            result.expected_from = expectedNext;
            result.expected_to = normalizedOpen;
            return result;
        }
    }

    auto it = std::lower_bound(candles_.begin(), candles_.end(), candle.openTime,
                               [](const domain::Candle& lhs, domain::TimestampMs value) {
                                   return lhs.openTime < value;
                               });

    if (it != candles_.end() && it->openTime == candle.openTime) {
        const bool replaceTail = (!candles_.empty() && it + 1 == candles_.end() &&
                                  candle.openTime == meta_.maxOpen);
        if (replaceTail) {
            metrics::repoFastPathIncr("repo.tail_replace");
            *it = candle;
            meta_.maxOpen = candle.openTime;

            if (candle.isClosed) {
                lastClosedOpen_ = candle.openTime;
            }
            else if (lastClosedOpen_ >= candle.openTime) {
                domain::TimestampMs newLastClosed = 0;
                for (auto rit = candles_.rbegin(); rit != candles_.rend(); ++rit) {
                    if (rit->isClosed) {
                        newLastClosed = rit->openTime;
                        break;
                    }
                }
                lastClosedOpen_ = newLastClosed;
            }

            bool touchedDisk = false;
            if (!noDisk_ && !filePath_.empty()) {
                std::fstream fs(filePath_, std::ios::in | std::ios::out | std::ios::binary);
                if (!fs.good()) {
                    LOG_ERROR(kLogCategory,
                              "DB: failed to open %s for tail replace",
                              filePath_.c_str());
                }
                else {
                    const auto record =
                        makeRecord(candle, symbol_, intervalToString(interval_), interval_.ms);
                    const auto recordSize = static_cast<std::streamoff>(sizeof(PriceData));
                    fs.seekp(0, std::ios::end);
                    const auto endPos = fs.tellp();
                    if (endPos < recordSize) {
                        LOG_ERROR(kLogCategory,
                                  "DB: file %s too small for tail replace",
                                  filePath_.c_str());
                    }
                    else {
                        fs.seekp(-recordSize, std::ios::end);
                        if (fs.fail()) {
                            LOG_ERROR(kLogCategory,
                                      "DB: failed to seek %s for tail replace",
                                      filePath_.c_str());
                        }
                        else {
                            fs.write(reinterpret_cast<const char*>(&record), sizeof(record));
                            if (fs.fail()) {
                                LOG_ERROR(kLogCategory,
                                          "DB: failed to write tail replace record to %s",
                                          filePath_.c_str());
                            }
                            else {
                                fs.flush();
                                touchedDisk = true;
                            }
                        }
                    }
                }
                if (!touchedDisk) {
                    markDirtyUnsafe_();
                }
            }

            result.state = domain::RangeState::Replaced;
            result.appended = 1;
            result.touchedDisk = touchedDisk;
            result.liveOnly = !candle.isClosed;
            return result;
        }

        metrics::repoFastPathIncr("repo.rewriteAll.calls");
        *it = candle;
        updateDerivedStateUnsafe_();
        markDirtyUnsafe_();
        result.state = domain::RangeState::Replaced;
        result.appended = 1;
        result.touchedDisk = false;
        result.liveOnly = !candle.isClosed;
        return result;
    }

    const bool insertAtEnd = (it == candles_.end());
    it = candles_.insert(it, candle);
    updateDerivedStateUnsafe_();

    if (insertAtEnd) {
        appendRecordUnsafe_(candle);
    }
    else {
        metrics::repoFastPathIncr("repo.rewriteAll.calls");
        markDirtyUnsafe_();
    }

    result.state = domain::RangeState::Ok;
    result.appended = 1;
    result.touchedDisk = insertAtEnd && !noDisk_;
    result.liveOnly = !candle.isClosed;
    return result;
}

domain::Candle PriceDataTimeSeriesRepository::recordToCandle(const PriceData& record) const {
    domain::Candle candle;
    candle.openTime = record.openTime;
    candle.closeTime = record.closeTime;
    candle.open = record.openPrice;
    candle.high = record.highPrice;
    candle.low = record.lowPrice;
    candle.close = record.closePrice;
    candle.baseVolume = record.volume;
    candle.quoteVolume = record.baseAssetVolume;
    candle.trades = static_cast<domain::TradeCount>(record.numberOfTrades);
    candle.isClosed = interval_.ms <= 0 || (record.closeTime >= record.openTime + interval_.ms - 1);
    return candle;
}

void PriceDataTimeSeriesRepository::appendRecordUnsafe_(const domain::Candle& candle) {
    metrics::RepoFastPathTimer diagTimer{"repo.appendRecord"};
    if (noDisk_) {
        metrics::repoFastPathIncr("repo.disk.writes.skipped");
        return;
    }
    if (filePath_.empty()) {
        return;
    }

    std::ofstream ofs(filePath_, std::ios::binary | std::ios::app);
    if (!ofs.good()) {
        LOG_ERROR(kLogCategory, "DB: failed to open %s for append", filePath_.c_str());
        return;
    }

    const auto record = makeRecord(candle, symbol_, intervalToString(interval_), interval_.ms);
    ofs.write(reinterpret_cast<const char*>(&record), sizeof(record));
    ofs.flush();
    metrics::repoFastPathIncr("repo.appendRecord.count");
}

std::int64_t PriceDataTimeSeriesRepository::normalizeOpenTime(domain::TimestampMs openTime) const {
    if (interval_.ms > 0) {
        return domain::align_down_ms(openTime, interval_.ms);
    }
    return openTime;
}

}  // namespace infra::storage

