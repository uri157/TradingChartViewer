#include "app/SyncOrchestrator.h"

#include "core/EventBus.h"
#include "logging/Log.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <exception>
#include <utility>
#include <vector>

namespace {

long long alignDown(long long value, long long step) noexcept {
    return (step > 0) ? (value / step) * step : value;
}

long long intervalToMs(const std::string& label) noexcept {
    if (label.empty()) {
        return core::TimeUtils::kMillisPerMinute;
    }
    auto parsed = domain::interval_from_label(label);
    if (parsed.valid()) {
        return parsed.ms;
    }
    return core::TimeUtils::kMillisPerMinute;
}

std::size_t seedBarsFor(const std::string& interval) noexcept {
    std::string lower = interval;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });

    if (lower == "1m") {
        return 7u * 24u * 60u;
    }
    if (lower == "5m") {
        return 7u * 24u * 12u;
    }
    if (lower == "15m") {
        return 30u * 24u * 4u;
    }
    if (lower == "1h") {
        return 60u * 24u;
    }
    if (lower == "4h") {
        return 90u * 24u / 4u;
    }
    if (lower == "1d") {
        return 365u;
    }

    return 7u * 24u * 60u;
}

long long clampIntervalMs(const app::SyncConfig& cfg) noexcept {
    if (cfg.candleInterval.ms > 0) {
        return cfg.candleInterval.ms;
    }
    return intervalToMs(cfg.interval);
}

void traceCandles(const std::vector<domain::Candle>& candles) {
    if (candles.empty()) {
        return;
    }
    LOG_TRACE(logging::LogCategory::DATA,
              "Batch first=%lld last=%lld count=%zu",
              static_cast<long long>(candles.front().openTime),
              static_cast<long long>(candles.back().openTime),
              candles.size());
}

void logLifecycleEvent(const domain::AppendResult& result) {
    const bool looksLive = result.liveOnly || (result.state == domain::RangeState::Replaced && result.touchedDisk);
    if (!looksLive) {
        return;
    }

    if (result.state == domain::RangeState::Replaced) {
        if (result.liveOnly && !result.touchedDisk) {
            LOG_TRACE(logging::LogCategory::DATA, "Live candle updated in memory");
        } else if (result.touchedDisk && !result.liveOnly) {
            LOG_INFO(logging::LogCategory::DATA, "Live candle closed & persisted");
        }
        return;
    }

    if (result.state == domain::RangeState::Ok && result.appended == 1) {
        if (result.touchedDisk && result.liveOnly) {
            LOG_INFO(logging::LogCategory::DATA, "Live candle sealed & persisted on rollover");
        } else if (result.liveOnly) {
            LOG_TRACE(logging::LogCategory::DATA, "Live candle updated in memory");
        }
    }
}

constexpr std::chrono::milliseconds kLiveThrottleMs{75};

}  // namespace

namespace app {

SyncOrchestrator::SyncOrchestrator(std::shared_ptr<domain::TimeSeriesRepository> repo,
                                   std::shared_ptr<core::SeriesCache> cache,
                                   std::shared_ptr<domain::MarketSource> market,
                                   core::EventBus* eventBus,
                                   SyncConfig cfg)
    : repo_(std::move(repo)),
      cache_(std::move(cache)),
      market_(std::move(market)),
      eventBus_(eventBus),
      cfg_(std::move(cfg)) {
    if (cfg_.symbol.empty()) {
        cfg_.symbol = "BTCUSDT";
    }

    if (cfg_.interval.empty()) {
        cfg_.interval = "1m";
    }

    if (cfg_.candleInterval.ms <= 0) {
        auto parsed = domain::interval_from_label(cfg_.interval);
        if (parsed.valid()) {
            cfg_.candleInterval = parsed;
        }
    }

    if (cfg_.candleInterval.ms <= 0) {
        cfg_.candleInterval.ms = core::TimeUtils::kMillisPerMinute;
    }
}

SyncOrchestrator::~SyncOrchestrator() {
    stop();
}

void SyncOrchestrator::start() {
    bool expected = false;
    if (!running_.compare_exchange_strong(expected, true)) {
        return;
    }

    if (worker_.joinable()) {
        worker_.join();
    }

    worker_ = std::thread([this]() {
        try {
            runBackfill();
        }
        catch (const std::exception& ex) {
            LOG_WARN(logging::LogCategory::DATA, "SyncOrchestrator exception: %s", ex.what());
        }
        catch (...) {
            LOG_WARN(logging::LogCategory::DATA, "SyncOrchestrator terminated due to unknown exception");
        }
        running_.store(false, std::memory_order_release);
    });
}

void SyncOrchestrator::stop() {
    if (!running_.exchange(false, std::memory_order_acq_rel)) {
        if (worker_.joinable()) {
            worker_.join();
        }
        return;
    }

    if (worker_.joinable()) {
        worker_.join();
    }
}

bool SyncOrchestrator::isRunning() const noexcept {
    return running_.load(std::memory_order_acquire);
}

bool SyncOrchestrator::isSeeded() const noexcept {
    return seeded_.load(std::memory_order_acquire);
}

bool SyncOrchestrator::isBackfilling() const noexcept {
    return backfilling_.load(std::memory_order_acquire);
}

void SyncOrchestrator::requestBackfillOlder(domain::TimestampMs startOpen, std::size_t candles) {
    if (!repo_ || !market_ || candles == 0) {
        return;
    }

    if (!running_.load(std::memory_order_acquire) || backfilling_.load(std::memory_order_acquire)) {
        LOG_DEBUG(logging::LogCategory::DATA, "Backfill older ignored: orchestrator busy");
        return;
    }

    if (pagingBackfill_.exchange(true, std::memory_order_acq_rel)) {
        LOG_TRACE(logging::LogCategory::DATA, "Backfill older already in progress");
        return;
    }

    std::thread([this, startOpen, candles]() {
        struct ResetFlag {
            std::atomic<bool>& flag;
            ~ResetFlag() { flag.store(false, std::memory_order_release); }
        } reset{pagingBackfill_};

        const auto intervalMs = clampIntervalMs(cfg_);
        if (intervalMs <= 0) {
            LOG_WARN(logging::LogCategory::DATA, "Cannot backfill older history: invalid interval");
            return;
        }

        const domain::TimestampMs safeStart = std::max<domain::TimestampMs>(0, startOpen);
        domain::TimestampMs span = intervalMs * static_cast<domain::TimestampMs>(std::max<std::size_t>(candles, 1));
        if (span <= 0) {
            span = intervalMs;
        }
        const domain::TimestampMs end = safeStart + span - 1;
        domain::TimeRange range{safeStart, end};

        LOG_INFO(logging::LogCategory::DATA,
                 "Paging backfill request start=%lld end=%lld count=%zu",
                 static_cast<long long>(range.start),
                 static_cast<long long>(range.end),
                 candles);

        auto fetched = market_->fetchRange(cfg_.symbol, cfg_.candleInterval, range, candles);
        if (fetched.empty()) {
            LOG_INFO(logging::LogCategory::DATA,
                     "Paging backfill returned empty range start=%lld end=%lld",
                     static_cast<long long>(range.start),
                     static_cast<long long>(range.end));
            return;
        }

        std::sort(fetched.begin(), fetched.end(), [](const domain::Candle& lhs, const domain::Candle& rhs) {
            return lhs.openTime < rhs.openTime;
        });

        bool gapDetected = false;
        domain::AppendResult gapResult;
        for (const auto& candle : fetched) {
            auto result = repo_->appendOrReplace(candle);
            logLifecycleEvent(result);
            if (result.state == domain::RangeState::Gap) {
                gapDetected = true;
                gapResult = result;
                break;
            }
        }

        if (gapDetected) {
            repairGap(gapResult, fetched);
        }

        publishSnapshot(true);
    }).detach();
}

void SyncOrchestrator::runBackfill() {
    if (!repo_ || !cache_ || !market_) {
        LOG_WARN(logging::LogCategory::DATA, "SyncOrchestrator missing dependencies; aborting backfill");
        return;
    }

    backfilling_.store(true, std::memory_order_release);
    struct ResetBackfill {
        std::atomic<bool>& flag;
        ~ResetBackfill() { flag.store(false, std::memory_order_release); }
    } reset{backfilling_};

    widenedOnce_ = false;
    bool fetchedAny = false;

    const auto intervalMs = clampIntervalMs(cfg_);
    if (intervalMs <= 0) {
        LOG_WARN(logging::LogCategory::DATA, "Invalid interval for backfill; aborting");
        return;
    }

    auto meta = repo_->metadata();
    domain::TimestampMs nextFetch = 0;
    const auto nowMs = static_cast<domain::TimestampMs>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch())
            .count());

    const bool repoEmpty = meta.count == 0 || meta.maxOpen <= 0;
    const auto seedBars = std::max<std::size_t>(seedBarsFor(cfg_.interval), std::size_t{1});
    domain::TimestampMs seedSpanMs = 0;
    domain::TimestampMs seedEnd = 0;
    if (repoEmpty) {
        seedSpanMs = intervalMs * static_cast<domain::TimestampMs>(seedBars);
        seedEnd = alignDown(nowMs, intervalMs);
        const auto seedStart = std::max<domain::TimestampMs>(0, seedEnd - seedSpanMs);
        nextFetch = seedStart;

        LOG_INFO(logging::LogCategory::DATA,
                 "Backfill seeding empty repository symbol=%s interval=%s start=%lld end=%lld bars=%zu",
                 cfg_.symbol.c_str(),
                 cfg_.interval.c_str(),
                 static_cast<long long>(seedStart),
                 static_cast<long long>(seedEnd),
                 seedBars);
    } else {
        nextFetch = meta.maxOpen + intervalMs;
    }

    LOG_INFO(logging::LogCategory::DATA,
             "Backfill start symbol=%s interval=%s next=%lld",
             cfg_.symbol.c_str(),
             cfg_.interval.c_str(),
             static_cast<long long>(nextFetch));

    domain::TimestampMs widenAmount = seedSpanMs;
    if (widenAmount <= 0) {
        widenAmount = intervalMs * static_cast<domain::TimestampMs>(std::max<std::size_t>(cfg_.restBatch, std::size_t{1}));
    }
    if (widenAmount <= 0) {
        widenAmount = intervalMs;
    }

    while (running_.load(std::memory_order_acquire)) {
        if (repoEmpty && seedEnd > 0 && nextFetch > seedEnd) {
            LOG_DEBUG(logging::LogCategory::DATA,
                      "Backfill seed window exhausted at %lld",
                      static_cast<long long>(seedEnd));
            break;
        }

        const std::size_t batchLimit = std::max<std::size_t>(cfg_.restBatch, 1);
        domain::TimestampMs span = intervalMs * static_cast<domain::TimestampMs>(batchLimit);
        if (span <= 0) {
            span = intervalMs;
        }
        if (span <= 0) {
            LOG_WARN(logging::LogCategory::DATA,
                     "Backfill aborting due to invalid span interval=%lld batch=%zu",
                     static_cast<long long>(intervalMs),
                     batchLimit);
            break;
        }

        domain::TimestampMs requestEnd = nextFetch + span - 1;
        if (repoEmpty && seedEnd > 0) {
            const auto cappedEnd = seedEnd + intervalMs - 1;
            if (cappedEnd >= nextFetch) {
                requestEnd = std::min(requestEnd, cappedEnd);
            }
        }

        domain::TimeRange requestRange{nextFetch, requestEnd};
        if (requestRange.empty()) {
            LOG_DEBUG(logging::LogCategory::DATA,
                      "Backfill request range empty start=%lld end=%lld",
                      static_cast<long long>(requestRange.start),
                      static_cast<long long>(requestRange.end));
            break;
        }

        LOG_INFO(logging::LogCategory::DATA,
                 "Backfill request symbol=%s interval=%s start=%lld end=%lld limit=%zu",
                 cfg_.symbol.c_str(),
                 cfg_.interval.c_str(),
                 static_cast<long long>(requestRange.start),
                 static_cast<long long>(requestRange.end),
                 batchLimit);

        auto candles = market_->fetchRange(cfg_.symbol, cfg_.candleInterval, requestRange, batchLimit);
        if (!running_.load(std::memory_order_acquire)) {
            break;
        }

        if (candles.empty()) {
            if (!fetchedAny) {
                if (repoEmpty && !widenedOnce_ && widenAmount > 0) {
                    widenedOnce_ = true;
                    const auto previousStart = nextFetch;
                    nextFetch = std::max<domain::TimestampMs>(0, nextFetch - widenAmount);
                    LOG_WARN(logging::LogCategory::DATA,
                             "Initial backfill returned empty batch; widening window start=%lld->%lld span_ms=%lld",
                             static_cast<long long>(previousStart),
                             static_cast<long long>(nextFetch),
                             static_cast<long long>(widenAmount));
                    continue;
                }

                LOG_WARN(logging::LogCategory::DATA,
                         "Backfill aborted: no candles returned symbol=%s interval=%s",
                         cfg_.symbol.c_str(),
                         cfg_.interval.c_str());
            } else if (cfg_.trace) {
                LOG_TRACE(logging::LogCategory::NET, "Backfill fetch returned empty batch");
            }
            break;
        }

        fetchedAny = true;

        if (cfg_.trace) {
            traceCandles(candles);
        }

        LOG_INFO(logging::LogCategory::DATA,
                 "Backfill span=%lld-%lld count=%zu",
                 static_cast<long long>(candles.front().openTime),
                 static_cast<long long>(candles.back().openTime),
                 candles.size());

        domain::TimestampMs expected = meta.count > 0 ? meta.maxOpen + intervalMs : requestRange.start;
        for (const auto& candle : candles) {
            if (expected > 0 && candle.openTime > expected) {
                LOG_WARN(logging::LogCategory::DATA,
                         "Gap detected expected=%lld actual=%lld",
                         static_cast<long long>(expected),
                         static_cast<long long>(candle.openTime));
            }
            expected = candle.openTime + intervalMs;
        }

        domain::AppendResult gapResult;
        bool gapDetected = false;

        for (const auto& candle : candles) {
            auto result = repo_->appendOrReplace(candle);
            logLifecycleEvent(result);

            if (result.state == domain::RangeState::Gap) {
                gapResult = result;
                gapDetected = true;
                break;
            }
            if (result.state == domain::RangeState::Overlap) {
                LOG_WARN(logging::LogCategory::DATA, "Overlap/older candle ignored");
            }
        }

        if (gapDetected) {
            LOG_WARN(logging::LogCategory::DATA, "Repository reported gap while appending batch");
            repairGap(gapResult, candles);
        }

        publishSnapshot(true);

        meta = repo_->metadata();
        if (meta.count == 0) {
            continue;
        }
        nextFetch = meta.maxOpen + intervalMs;

        bool morePages = true;
        if (candles.size() < batchLimit && !gapDetected) {
            morePages = false;
        }

        if (!running_.load(std::memory_order_acquire)) {
            break;
        }

        if (!morePages) {
            break;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(std::max(cfg_.backoffMs, 0)));
    }

    publishSnapshot(true);
    LOG_INFO(logging::LogCategory::DATA, "Backfill finished");

    if (!running_.load(std::memory_order_acquire)) {
        return;
    }

    startLiveStream();

    while (running_.load(std::memory_order_acquire)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    stopLiveStream();
}

void SyncOrchestrator::repairGap(const domain::AppendResult& gap,
                                 const std::vector<domain::Candle>& batch) {
    if (!repo_ || !market_) {
        return;
    }

    if (repairing_) {
        LOG_WARN(logging::LogCategory::DATA, "Gap detected while already repairing; skipping");
        return;
    }

    const auto intervalMs = clampIntervalMs(cfg_);
    if (intervalMs <= 0) {
        LOG_WARN(logging::LogCategory::DATA, "Invalid interval while attempting gap repair");
        return;
    }

    if (gap.expected_to <= gap.expected_from) {
        LOG_WARN(logging::LogCategory::DATA,
                 "Gap range invalid expected_from=%lld expected_to=%lld",
                 static_cast<long long>(gap.expected_from),
                 static_cast<long long>(gap.expected_to));
        return;
    }

    repairing_ = true;
    struct ResetFlag {
        bool& flag;
        ~ResetFlag() { flag = false; }
    } reset{repairing_};

    LOG_WARN(logging::LogCategory::DATA,
             "Gap detected: re-fetching missing range [%lld, %lld)",
             static_cast<long long>(gap.expected_from),
             static_cast<long long>(gap.expected_to));

    const auto span = gap.expected_to - gap.expected_from;
    std::size_t limit = static_cast<std::size_t>(span / intervalMs);
    if (span % intervalMs != 0) {
        ++limit;
    }
    if (limit == 0) {
        limit = 1;
    }

    const domain::TimeRange missingRange{gap.expected_from, gap.expected_to - 1};
    auto missing = market_->fetchRange(cfg_.symbol, cfg_.candleInterval, missingRange, limit);
    if (missing.empty()) {
        LOG_WARN(logging::LogCategory::DATA, "Backfill fetch returned empty while repairing gap");
    }

    for (const auto& candle : missing) {
        const auto fill = repo_->appendOrReplace(candle);
        logLifecycleEvent(fill);
        if (fill.state == domain::RangeState::Gap) {
            LOG_WARN(logging::LogCategory::DATA, "Repository reported gap while appending backfill batch");
            break;
        }
        if (fill.state == domain::RangeState::Overlap) {
            LOG_WARN(logging::LogCategory::DATA, "Overlap/older candle ignored");
        }
    }

    std::vector<domain::Candle> retry;
    retry.reserve(batch.size());
    for (const auto& candle : batch) {
        const auto normalizedOpen = domain::align_down_ms(candle.openTime, intervalMs);
        if (normalizedOpen >= gap.expected_to) {
            retry.push_back(candle);
        }
    }

    bool repaired = false;
    if (!retry.empty()) {
        for (const auto& candle : retry) {
            auto retryResult = repo_->appendOrReplace(candle);
            logLifecycleEvent(retryResult);
            if (retryResult.state == domain::RangeState::Gap) {
                LOG_WARN(logging::LogCategory::DATA, "Backfill did not close the gap completely");
                return;
            }
            if (retryResult.state == domain::RangeState::Overlap) {
                LOG_WARN(logging::LogCategory::DATA, "Overlap/older candle ignored");
            }
        }
        repaired = true;
    } else if (!missing.empty()) {
        repaired = true;
    }

    if (repaired) {
        LOG_INFO(logging::LogCategory::DATA, "Gap repaired and series consistent");
    }
}

void SyncOrchestrator::startLiveStream() {
    if (!market_) {
        LOG_WARN(logging::LogCategory::DATA, "Live stream unavailable: market source missing");
        return;
    }
    if (liveSubscription_) {
        return;
    }

    try {
        lastLivePublish_ = std::chrono::steady_clock::time_point::min();
        liveSubscription_ = market_->streamLive(
            cfg_.symbol,
            cfg_.candleInterval,
            [this](const domain::LiveCandle& live) { handleLiveCandle(live); },
            [this](const domain::StreamError& error) { handleStreamError(error); });
        if (!liveSubscription_) {
            LOG_WARN(logging::LogCategory::DATA, "Live stream subscription failed");
        } else if (cfg_.trace) {
            LOG_TRACE(logging::LogCategory::DATA, "Live stream subscribed");
        }
    }
    catch (const std::exception& ex) {
        LOG_WARN(logging::LogCategory::DATA, "Live stream exception: %s", ex.what());
    }
}

void SyncOrchestrator::stopLiveStream() {
    if (liveSubscription_) {
        liveSubscription_->stop();
        liveSubscription_.reset();
        if (cfg_.trace) {
            LOG_TRACE(logging::LogCategory::DATA, "Live stream unsubscribed");
        }
    }
}

void SyncOrchestrator::handleLiveCandle(const domain::LiveCandle& live) {
    if (!running_.load(std::memory_order_acquire)) {
        return;
    }

    LOG_TRACE(logging::LogCategory::DATA,
              "LIVE update openTime=%lld isClosed=%d",
              static_cast<long long>(live.candle.openTime),
              live.isFinal && live.candle.isClosed ? 1 : 0);

    if (!repo_) {
        return;
    }

    auto result = repo_->appendOrReplace(live.candle);
    logLifecycleEvent(result);
    if (result.state == domain::RangeState::Gap) {
        LOG_WARN(logging::LogCategory::DATA,
                 "Live stream gap detected expected=%lld actual=%lld",
                 static_cast<long long>(result.expected_from),
                 static_cast<long long>(live.candle.openTime));
    }

    const bool closed = live.isFinal && live.candle.isClosed;
    const auto now = std::chrono::steady_clock::now();
    if (!closed && lastLivePublish_ != std::chrono::steady_clock::time_point::min() &&
        now - lastLivePublish_ < kLiveThrottleMs) {
        return;
    }

    lastLivePublish_ = now;
    publishSnapshot(closed);
}

void SyncOrchestrator::handleStreamError(const domain::StreamError& error) {
    LOG_WARN(logging::LogCategory::DATA,
             "Live stream error code=%d message=%s",
             error.code,
             error.message.c_str());
}

void SyncOrchestrator::publishSnapshot(bool lastClosed) {
    if (!repo_ || !cache_) {
        return;
    }

    std::lock_guard<std::mutex> guard(publishMutex_);

    auto latest = repo_->getLatest(cfg_.publishCandles);
    if (latest.failed()) {
        LOG_WARN(logging::LogCategory::DATA, "Unable to fetch latest candles for cache: %s", latest.error.c_str());
        return;
    }

    auto series = std::make_shared<domain::CandleSeries>(std::move(latest.value));
    cache_->update(series);
    LOG_DEBUG(logging::LogCategory::CACHE, "Snapshot swap candles=%zu", series->data.size());
    if (eventBus_ && series) {
        core::EventBus::SeriesUpdated evt{};
        evt.count = series->data.size();
        evt.firstOpen = series->data.empty() ? 0 : series->firstOpen;
        evt.lastOpen = series->data.empty() ? 0 : series->lastOpen;
        evt.lastClosed = lastClosed;
        eventBus_->publishSeriesUpdated(evt);
    }
    if (!seeded_.exchange(true, std::memory_order_acq_rel) && cfg_.trace) {
        LOG_TRACE(logging::LogCategory::CACHE, "Series cache seeded");
    }
}

}  // namespace app

