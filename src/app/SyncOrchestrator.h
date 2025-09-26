#pragma once

#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include "core/SeriesCache.h"
#include "core/TimeUtils.h"
#include "domain/DomainContracts.h"
#include "domain/MarketSource.h"
#include "logging/Log.h"

namespace core {
class EventBus;
}

namespace app {

struct SyncConfig {
    std::size_t publishCandles = 120;
    std::size_t restBatch = 1000;
    int backoffMs = 500;
    bool trace = false;
    std::string symbol = "BTCUSDT";
    std::string interval = "1m";
    domain::Interval candleInterval{core::TimeUtils::kMillisPerMinute};
};

class SyncOrchestrator {
public:
    SyncOrchestrator(std::shared_ptr<domain::TimeSeriesRepository> repo,
                     std::shared_ptr<core::SeriesCache> cache,
                     std::shared_ptr<domain::MarketSource> market,
                     core::EventBus* eventBus,
                     SyncConfig cfg = {});
    ~SyncOrchestrator();

    void start();
    void stop();

    bool isRunning() const noexcept;
    bool isSeeded() const noexcept;
    bool isBackfilling() const noexcept;
    void requestBackfillOlder(domain::TimestampMs startOpen, std::size_t candles);

private:
    void runBackfill();
    void publishSnapshot(bool lastClosed);
    void repairGap(const domain::AppendResult& gap, const std::vector<domain::Candle>& batch);
    void startLiveStream();
    void stopLiveStream();
    void handleLiveCandle(const domain::LiveCandle& live);
    void handleStreamError(const domain::StreamError& error);

    std::shared_ptr<domain::TimeSeriesRepository> repo_;
    std::shared_ptr<core::SeriesCache> cache_;
    std::shared_ptr<domain::MarketSource> market_;
    core::EventBus* eventBus_{nullptr};
    SyncConfig cfg_;

    std::thread worker_;
    std::atomic<bool> running_{false};
    std::atomic<bool> seeded_{false};
    std::atomic<bool> backfilling_{false};
    std::atomic<bool> pagingBackfill_{false};
    bool repairing_{false};
    bool widenedOnce_{false};
    std::unique_ptr<domain::SubscriptionHandle> liveSubscription_;
    std::mutex publishMutex_;
    std::chrono::steady_clock::time_point lastLivePublish_{std::chrono::steady_clock::time_point::min()};
};

}  // namespace app
