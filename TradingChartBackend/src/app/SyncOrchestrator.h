#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "app/SessionState.h"
#include "infra/storage/PriceDataTimeSeriesRepository.h"
#include "core/RenderSnapshot.h"

// Forward declarations for dependencies
namespace core {
class EventBus;
class SeriesCache;
}

namespace domain {
class SubscriptionHandle;
struct Candle;
struct LiveCandle;
struct StreamError;
struct CandleSeries;
}

namespace infra {
namespace exchange {
class ExchangeGateway;
}  // namespace exchange
namespace storage {
class PriceDataTimeSeriesRepository;
}  // namespace storage
}  // namespace infra

namespace indicators {
class IndicatorCoordinator;
}

namespace app {

struct SyncConfig {
  enum class BackfillMode { Auto, Reverse, Forward };

  std::int64_t lookbackMaxMs = 7LL * 24 * 60 * 60 * 1000;  // 7 days
  int          backfillChunk = 1000;
  int          backfillMinSleepMs = 250;
  bool         wsWarmup = true;
  BackfillMode backfillMode = BackfillMode::Auto;

  // Optional integrations with existing subsystems
  std::size_t publishCandles = 600;
  core::SeriesCache* seriesCache = nullptr;
  core::EventBus* eventBus = nullptr;
};

class SyncOrchestrator {
public:
  SyncOrchestrator(infra::exchange::ExchangeGateway& gw,
                   infra::storage::PriceDataTimeSeriesRepository& repo,
                   const infra::storage::Paths& paths,
                   indicators::IndicatorCoordinator* ind,
                   SyncConfig cfg);
  ~SyncOrchestrator();

  void start(const SessionState& s);
  void stop();
  void switchTo(const SessionState& s) { stop(); start(s); }

  bool isBackfilling() const noexcept;
  bool hasLiveGap() const noexcept;
  std::uint64_t snapshotVersion() const noexcept;

private:
  // Dependencias
  infra::exchange::ExchangeGateway& gw_;
  infra::storage::PriceDataTimeSeriesRepository& repo_;
  infra::storage::Paths paths_;
  indicators::IndicatorCoordinator* indicators_{};

  // Configuración
  SyncConfig cfg_{};
  core::SeriesCache* seriesCache_{};
  core::EventBus* eventBus_{};
  std::size_t publishCount_{600};

  // Estado
  mutable std::mutex mtx_;
  std::mutex publishMtx_;
  std::thread backfillThread_;
  std::thread targetedBackfillThread_;
  std::unique_ptr<domain::SubscriptionHandle> liveSubscription_;
  std::atomic<bool> running_{false};
  std::atomic<bool> backfilling_{false};
  std::atomic<bool> seeded_{false};
  std::atomic<bool> liveGapPending_{false};
  std::atomic<bool> gapInFlight_{false};
  std::size_t lastStableCount_{0};
  std::uint64_t sessionId_{0};
  std::optional<SessionState> activeSession_;
  std::chrono::steady_clock::time_point lastLivePublish_{std::chrono::steady_clock::time_point::min()};
  std::mutex liveQueueMtx_;
  std::condition_variable liveQueueCv_;
  std::vector<domain::LiveCandle> liveQueue_;
  std::optional<std::chrono::steady_clock::time_point> liveQueueFirstEnqueue_;
  std::thread liveBatchThread_;
  std::atomic<bool> stopLiveBatch_{false};

  // Helpers
  void spawnBackfillReverse_(std::uint64_t sid, const SessionState& s);
  void handleLiveCandle_(std::uint64_t sid, const domain::LiveCandle& live);
  void handleStreamError_(std::uint64_t sid, const domain::StreamError& err);
  bool isSessionCurrent_(std::uint64_t sid) const;

  void scheduleTargetedBackfill_(domain::TimestampMs start, domain::TimestampMs end);

  void publishSnapshotLoading_(const SessionState& s);
  void scheduleSnapshotPublish_();
  void flushSnapshot_();
  void startCoalescer_();
  void stopCoalescer_();
  void coalesceLoop_();
  void startLiveBatcher_();
  void stopLiveBatcher_();
  void liveBatchLoop_();
  void processLiveBatch_(std::vector<domain::LiveCandle>& batch);
  bool shouldPublish_(core::UiDataState state,
                      const std::string& symbol,
                      const std::string& interval,
                      std::size_t count,
                      const std::vector<domain::Candle>& tail
#ifdef DIAG_SYNC
                      , std::string* reasonOut = nullptr
#endif
  );

  static std::int64_t nowMs_();
  static std::int64_t alignDownToIntervalMs_(std::int64_t t, const SessionState& s);

  std::atomic_flag pendingSnapshot_ = ATOMIC_FLAG_INIT;
  std::thread coalesceThread_;
  std::atomic<bool> stopCoalesce_{false};
  std::atomic<std::uint64_t> snapshotVersion_{0};
  std::uint64_t lastPublishedVersion_{0};
  std::size_t lastPublishedCount_{0};
  core::UiDataState lastPublishedState_{core::UiDataState::Loading};
  std::string lastPublishedSymbol_;
  std::string lastPublishedInterval_;
  std::vector<domain::Candle> lastPublishedTail_;
  std::shared_ptr<const domain::CandleSeries> lastPublishedSeries_;
  bool lastPublishedLiveGap_{false};
  std::optional<std::chrono::steady_clock::time_point> lastPublishTimeOpt_;
};

}  // namespace app
