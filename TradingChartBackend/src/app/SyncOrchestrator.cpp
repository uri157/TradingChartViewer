#include "app/SyncOrchestrator.h"

#include "core/EventBus.h"
#include "core/LogUtils.h"
#include "core/RenderSnapshot.h"
#include "core/SeriesCache.h"
#ifdef TTP_ENABLE_DIAG
#include "core/Diag.h"
#endif
#include "domain/DomainContracts.h"
#include "domain/Types.h"
#include "infra/exchange/ExchangeGateway.h"
#include "infra/storage/PriceDataTimeSeriesRepository.h"
#include "indicators/IndicatorCoordinator.h"
#include "logging/Log.h"
#include "metrics/RepoFastPathDiag.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <exception>
#include <thread>
#include <utility>
#include <vector>

namespace app {
namespace {
constexpr std::chrono::milliseconds kLivePublishThrottle{75};
constexpr std::chrono::milliseconds kLiveBatchMinInterval{50};
constexpr std::chrono::milliseconds kLiveBatchMaxInterval{100};
constexpr std::size_t kLiveBatchImmediateThreshold{32};
constexpr std::size_t kMinHistoryCandlesReady{300};
constexpr std::size_t kTargetedGapPaddingCandles{300};
constexpr std::chrono::milliseconds kTargetedBackfillMinSleep{10};
constexpr std::chrono::milliseconds kBackfillFlushInterval{100};

std::string intervalLabel(const domain::Interval& interval) {
  auto label = domain::interval_label(interval);
  if (!label.empty()) {
    return label;
  }
  if (interval.valid()) {
    return std::to_string(interval.ms) + "ms";
  }
  return "?";
}

const char* uiDataStateLabel(core::UiDataState state) {
  switch (state) {
    case core::UiDataState::Loading:
      return "Loading";
    case core::UiDataState::LiveOnly:
      return "LiveOnly";
    case core::UiDataState::Ready:
      return "Ready";
  }
  return "Unknown";
}

std::uint64_t hashCandleTail(const std::vector<domain::Candle>& tail) {
  constexpr std::uint64_t kOffsetBasis = 1469598103934665603ull;
  constexpr std::uint64_t kPrime = 1099511628211ull;
  std::uint64_t hash = kOffsetBasis;

  auto mixBytes = [&](const void* data, std::size_t len) {
    const auto* bytes = static_cast<const unsigned char*>(data);
    for (std::size_t i = 0; i < len; ++i) {
      hash ^= static_cast<std::uint64_t>(bytes[i]);
      hash *= kPrime;
    }
  };

  for (const auto& candle : tail) {
    mixBytes(&candle.openTime, sizeof(candle.openTime));
    mixBytes(&candle.closeTime, sizeof(candle.closeTime));
    mixBytes(&candle.open, sizeof(candle.open));
    mixBytes(&candle.high, sizeof(candle.high));
    mixBytes(&candle.low, sizeof(candle.low));
    mixBytes(&candle.close, sizeof(candle.close));
    const std::uint8_t closed = candle.isClosed ? 1u : 0u;
    mixBytes(&closed, sizeof(closed));
  }

  return hash;
}
}  // namespace

SyncOrchestrator::SyncOrchestrator(infra::exchange::ExchangeGateway& gw,
                                   infra::storage::PriceDataTimeSeriesRepository& repo,
                                   const infra::storage::Paths& paths,
                                   indicators::IndicatorCoordinator* ind,
                                   SyncConfig cfg)
    : gw_(gw),
      repo_(repo),
      paths_(paths),
      indicators_(ind),
      cfg_(cfg),
      seriesCache_(cfg.seriesCache),
      eventBus_(cfg.eventBus),
      publishCount_(std::max<std::size_t>(cfg.publishCandles, kMinHistoryCandlesReady)) {
  LOG_INFO(logging::LogCategory::DATA,
           "SYNC:publishCandles=%zu",
           publishCount_);
  if (cfg_.backfillChunk <= 0) {
    cfg_.backfillChunk = 1;
  }
  if (cfg_.backfillMinSleepMs < 0) {
    cfg_.backfillMinSleepMs = 0;
  }
  if (cfg_.lookbackMaxMs < 0) {
    cfg_.lookbackMaxMs = 0;
  }
  pendingSnapshot_.clear();
  lastPublishTimeOpt_.reset();
  gapInFlight_.store(false, std::memory_order_release);
  lastStableCount_ = 0;
  lastPublishedSeries_.reset();
}

SyncOrchestrator::~SyncOrchestrator() {
  try {
    stop();
  } catch (...) {
    LOG_WARN(logging::LogCategory::DATA, "SyncOrchestrator stop threw during destruction");
  }
}

void SyncOrchestrator::start(const SessionState& s) {
  if (s.symbol.empty() || !s.interval.valid()) {
    LOG_WARN(logging::LogCategory::DATA,
             "SESSION:start ignored invalid symbol='%s' interval_ms=%lld",
             s.symbol.c_str(),
             static_cast<long long>(s.interval.ms));
    return;
  }

  std::uint64_t sid = 0;
  {
    std::scoped_lock lk(mtx_);
    sid = ++sessionId_;
    running_.store(true, std::memory_order_release);
    activeSession_ = s;
    seeded_.store(false, std::memory_order_release);
  }

  pendingSnapshot_.clear();
  snapshotVersion_.store(0, std::memory_order_relaxed);
  lastPublishedVersion_ = 0;
  lastPublishedCount_ = 0;
  lastPublishedState_ = core::UiDataState::Loading;
  lastPublishedSymbol_.clear();
  lastPublishedInterval_.clear();
  lastPublishedTail_.clear();
  lastPublishedLiveGap_ = false;
  lastStableCount_ = 0;
  liveGapPending_.store(false, std::memory_order_release);
  lastPublishTimeOpt_.reset();
  lastLivePublish_ = std::chrono::steady_clock::time_point::min();

  try {
    repo_.bind(s.symbol, s.interval, paths_);
  } catch (...) {
    LOG_ERROR(logging::LogCategory::DATA,
              "DATA: bind failed symbol=%s interval=%s",
              s.symbol.c_str(),
              intervalLabel(s.interval).c_str());
    throw;
  }

  if (indicators_) {
    try {
      indicators_->invalidateAll();
    } catch (...) {
      LOG_WARN(logging::LogCategory::DATA, "Indicators invalidateAll threw");
    }
  }

  publishSnapshotLoading_(s);

  startLiveBatcher_();
  startCoalescer_();

  try {
    auto onData = [this, sid](const domain::LiveCandle& live) { handleLiveCandle_(sid, live); };
    auto onError = [this, sid](const domain::StreamError& err) { handleStreamError_(sid, err); };
    liveSubscription_ = gw_.streamLive(s.symbol, s.interval, std::move(onData), std::move(onError));
  } catch (...) {
    LOG_ERROR(logging::LogCategory::NET,
              "NET:ws start failed symbol=%s interval=%s",
              s.symbol.c_str(),
              intervalLabel(s.interval).c_str());
    liveSubscription_.reset();
  }

  spawnBackfillReverse_(sid, s);
}

void SyncOrchestrator::stop() {
  std::unique_ptr<domain::SubscriptionHandle> liveHandle;
  {
    std::scoped_lock lk(mtx_);
    if (!running_.load(std::memory_order_acquire)) {
      return;
    }
    running_.store(false, std::memory_order_release);
    liveHandle = std::move(liveSubscription_);
    activeSession_.reset();
  }

  if (liveHandle) {
    try {
      liveHandle->stop();
    } catch (...) {
      LOG_WARN(logging::LogCategory::NET, "NET:ws subscription stop threw");
    }
  }

  if (backfillThread_.joinable()) {
    try {
      backfillThread_.join();
    } catch (...) {
      LOG_WARN(logging::LogCategory::DATA, "Backfill thread join threw");
    }
  }

  if (targetedBackfillThread_.joinable()) {
    try {
      targetedBackfillThread_.join();
    } catch (...) {
      LOG_WARN(logging::LogCategory::DATA, "Targeted backfill thread join threw");
    }
  }

  stopLiveBatcher_();
  stopCoalescer_();

  repo_.flushIfNeeded(true);

  try {
    gw_.stopLive();
  } catch (...) {
    LOG_WARN(logging::LogCategory::NET, "NET:ws stop threw");
  }
}

bool SyncOrchestrator::isBackfilling() const noexcept {
  return backfilling_.load(std::memory_order_acquire);
}

bool SyncOrchestrator::hasLiveGap() const noexcept {
  return liveGapPending_.load(std::memory_order_acquire);
}

std::uint64_t SyncOrchestrator::snapshotVersion() const noexcept {
  return snapshotVersion_.load(std::memory_order_acquire);
}

void SyncOrchestrator::spawnBackfillReverse_(std::uint64_t sid, const SessionState& s) {
  if (backfillThread_.joinable()) {
    try {
      backfillThread_.join();
    } catch (...) {
      LOG_WARN(logging::LogCategory::DATA, "Previous backfill thread join threw");
    }
  }

  backfillThread_ = std::thread([this, sid, s] {
    backfilling_.store(true, std::memory_order_release);
    struct BackfillGuard {
      SyncOrchestrator* self;
      ~BackfillGuard() {
        if (self) {
          self->backfilling_.store(false, std::memory_order_release);
        }
      }
    } guard{this};

    try {
      LOG_INFO(logging::LogCategory::DATA,
               "DATA:reverse_backfill start symbol=%s interval=%s lookbackMaxMs=%lld chunk=%d",
               s.symbol.c_str(),
               intervalLabel(s.interval).c_str(),
               static_cast<long long>(cfg_.lookbackMaxMs),
               cfg_.backfillChunk);

      const std::int64_t intervalMs = s.interval.valid() ? s.interval.ms : repo_.intervalMs();
      if (intervalMs <= 0) {
        LOG_WARN(logging::LogCategory::DATA, "Reverse backfill aborted: invalid interval");
        return;
      }

      std::int64_t end = alignDownToIntervalMs_(nowMs_(), s);
      std::int64_t oldestAllowed = (cfg_.lookbackMaxMs > 0)
                                       ? std::max<std::int64_t>(0, end - cfg_.lookbackMaxMs)
                                       : 0;

      auto meta = repo_.metadata();
      if (meta.count > 0) {
        if (meta.maxOpen > 0) {
          end = std::min<std::int64_t>(end, meta.maxOpen);
        }
        if (meta.minOpen > 0) {
          oldestAllowed = std::min<std::int64_t>(oldestAllowed, meta.minOpen);
        }
      }

      const std::size_t limit = static_cast<std::size_t>(cfg_.backfillChunk);
      auto lastFlushCheck = std::chrono::steady_clock::now();

#ifdef DIAG_SYNC
      {
        static core::LogRateLimiter diagBackfillLimiter{std::chrono::milliseconds(250)};
        if (diagBackfillLimiter.allow()) {
          LOG_INFO(logging::LogCategory::DATA,
                   "DIAG_SYNC reverse_backfill sid=%llu intervalMs=%lld end=%lld oldestAllowed=%lld repoTotal=%zu chunk=%zu",
                   static_cast<unsigned long long>(sid),
                   static_cast<long long>(intervalMs),
                   static_cast<long long>(end),
                   static_cast<long long>(oldestAllowed),
                   meta.count,
                   limit);
        }
      }
#endif

      while (running_.load(std::memory_order_acquire) && end >= oldestAllowed) {
        if (!isSessionCurrent_(sid)) {
          break;
        }

        const auto now = std::chrono::steady_clock::now();
        if (now - lastFlushCheck >= kBackfillFlushInterval) {
          repo_.flushIfNeeded();
          lastFlushCheck = now;
        }

      const std::int64_t chunkSpan = intervalMs * static_cast<std::int64_t>((limit > 0) ? limit : 1);
      std::int64_t start = end - chunkSpan + intervalMs;
      if (start < oldestAllowed) {
        start = oldestAllowed;
      }
      if (start < 0) {
        start = 0;
      }
      if (start >= end) {
        LOG_INFO(logging::LogCategory::DATA,
                 "DATA:reverse_backfill done symbol=%s interval=%s",
                 s.symbol.c_str(),
                 intervalLabel(s.interval).c_str());
        break;
      }

      domain::TimeRange range{start, end + intervalMs - 1};
      LOG_INFO(logging::LogCategory::DATA,
               "DATA:reverse_backfill window=[%lld, %lld] limit=%zu",
               static_cast<long long>(range.start),
               static_cast<long long>(range.end),
               limit);

      std::vector<domain::Candle> batch;
      try {
        metrics::RepoFastPathTimer fetchTimer{"sync.backfill.fetch"};
        batch = gw_.fetchRange(s.symbol, s.interval, range, limit);
      } catch (const std::exception& ex) {
        LOG_WARN(logging::LogCategory::NET,
                 "NET:fetchRange failed window=[%lld, %lld] error=%s",
                 static_cast<long long>(range.start),
                 static_cast<long long>(range.end),
                 ex.what());
        if (cfg_.backfillMinSleepMs > 0) {
          std::this_thread::sleep_for(std::chrono::milliseconds(cfg_.backfillMinSleepMs));
        }
        continue;
      } catch (...) {
        LOG_WARN(logging::LogCategory::NET,
                 "NET:fetchRange failed window=[%lld, %lld] error=unknown",
                 static_cast<long long>(range.start),
                 static_cast<long long>(range.end));
        if (cfg_.backfillMinSleepMs > 0) {
          std::this_thread::sleep_for(std::chrono::milliseconds(cfg_.backfillMinSleepMs));
        }
        continue;
      }

      if (!running_.load(std::memory_order_acquire) || !isSessionCurrent_(sid)) {
        break;
      }

      if (!batch.empty()) {
        std::sort(batch.begin(), batch.end(), [](const domain::Candle& lhs, const domain::Candle& rhs) {
          return lhs.openTime < rhs.openTime;
        });
      }

      metrics::repoFastPathIncr("sync.backfill.batch.size", batch.size());
      domain::AppendResult summary;
      {
        metrics::RepoFastPathTimer appendTimer{"sync.backfill.appendBatch"};
        summary = repo_.appendBatch(batch);
      }
      const std::size_t appended = summary.appended;
      const std::size_t dupes = batch.size() > appended ? batch.size() - appended : 0;
      LOG_INFO(logging::LogCategory::DATA,
               "DATA:reverse_backfill window=[%lld, %lld] count=%zu dupes=%zu",
               batch.empty() ? static_cast<long long>(range.start)
                             : static_cast<long long>(batch.front().openTime),
               batch.empty() ? static_cast<long long>(range.end)
                             : static_cast<long long>(batch.back().openTime),
               batch.size(),
               dupes);

#ifdef DIAG_SYNC
      if (!batch.empty()) {
        static core::LogRateLimiter diagBackfillWindowLimiter{std::chrono::milliseconds(200)};
        if (diagBackfillWindowLimiter.allow()) {
          auto metaAfter = repo_.metadata();
          LOG_INFO(logging::LogCategory::DATA,
                   "DIAG_SYNC reverse_backfill sid=%llu appended=%zu dupes=%zu repoTotal=%zu window_start=%lld window_end=%lld",
                   static_cast<unsigned long long>(sid),
                   appended,
                   dupes,
                   metaAfter.count,
                   batch.empty() ? static_cast<long long>(range.start)
                                 : static_cast<long long>(batch.front().openTime),
                   batch.empty() ? static_cast<long long>(range.end)
                                 : static_cast<long long>(batch.back().openTime));
        }
      }
#endif

      if (!batch.empty() && appended > 0) {
        scheduleSnapshotPublish_();
      }

      if (!batch.empty()) {
        end = batch.front().openTime - intervalMs;
      } else {
        end = start - intervalMs;
      }

      if (cfg_.backfillMinSleepMs > 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(cfg_.backfillMinSleepMs));
      } else if (batch.size() >= limit) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
      }
      }

      LOG_INFO(logging::LogCategory::DATA,
               "DATA:reverse_backfill done symbol=%s interval=%s",
               s.symbol.c_str(),
               intervalLabel(s.interval).c_str());
      scheduleSnapshotPublish_();
      if (!repo_.hasGap()) {
        gapInFlight_.store(false, std::memory_order_release);
      }
      repo_.flushIfNeeded(true);
    } catch (const std::exception& ex) {
      LOG_WARN(logging::LogCategory::DATA,
               "DATA:reverse_backfill aborted by exception: %s",
               ex.what());
    } catch (...) {
      LOG_WARN(logging::LogCategory::DATA,
               "DATA:reverse_backfill aborted by unknown exception");
    }
  });
}

void SyncOrchestrator::handleLiveCandle_(std::uint64_t sid, const domain::LiveCandle& live) {
  if (!running_.load(std::memory_order_acquire) || !isSessionCurrent_(sid)) {
    return;
  }

  if (stopLiveBatch_.load(std::memory_order_acquire)) {
    return;
  }

  {
    std::lock_guard<std::mutex> lk(liveQueueMtx_);
    if (liveQueue_.empty()) {
      liveQueueFirstEnqueue_ = std::chrono::steady_clock::now();
    }
    liveQueue_.push_back(live);
  }
  liveQueueCv_.notify_one();
}

void SyncOrchestrator::processLiveBatch_(std::vector<domain::LiveCandle>& batch) {
  if (batch.empty()) {
    return;
  }

#ifdef TTP_ENABLE_DIAG
  auto diagTimer = core::diag::timer("sync.live");
#endif

  std::vector<domain::Candle> candles;
  candles.reserve(batch.size());
  bool anyClosedFinal = false;
  for (const auto& live : batch) {
    domain::Candle candle = live.candle;
    if (live.isFinal) {
      candle.isClosed = true;
    }
    anyClosedFinal = anyClosedFinal || (live.isFinal && candle.isClosed);
    candles.push_back(std::move(candle));
  }

  auto summary = repo_.appendBatch(candles);
#ifdef TTP_ENABLE_DIAG
  if (summary.state == domain::RangeState::Gap) {
    core::diag::incr("sync.gap");
  }
#endif
  if (summary.state == domain::RangeState::Gap) {
    liveGapPending_.store(true, std::memory_order_release);
    gapInFlight_.store(true, std::memory_order_release);
    static std::atomic<std::uint64_t> gapSequence{0};
    const auto seq = gapSequence.fetch_add(1, std::memory_order_relaxed) + 1;
    static core::RateLogger gapLogger;
#ifdef DIAG_SYNC
    static core::RateLogger diagGapLogger;
#endif

    std::optional<SessionState> session;
    std::uint64_t sidSnapshot = 0;
    {
      std::scoped_lock lk(mtx_);
      session = activeSession_;
      sidSnapshot = sessionId_;
    }

    const std::string symbol = session ? session->symbol : std::string{"?"};
    const std::string interval = session ? intervalLabel(session->interval) : std::string{"?"};
    const auto expectedFrom = summary.expected_from;
    const auto expectedTo = summary.expected_to;
    const auto intervalMs = repo_.intervalMs();

    domain::TimestampMs referenceOpen = !candles.empty() ? candles.back().openTime : expectedTo;
    if (referenceOpen <= 0 && !candles.empty()) {
      referenceOpen = candles.front().openTime;
    }
    domain::TimestampMs paddedTo = expectedTo > 0 ? expectedTo : referenceOpen;
    if (paddedTo <= 0 && !candles.empty()) {
      paddedTo = candles.back().openTime;
    }

    domain::TimestampMs paddedFrom = expectedFrom > 0 ? expectedFrom : paddedTo;
    if (intervalMs > 0) {
      const auto paddingMs = intervalMs * static_cast<std::int64_t>(kTargetedGapPaddingCandles);
      if (paddingMs > 0) {
        paddedFrom = std::max<domain::TimestampMs>(0, paddedFrom - paddingMs);
      }
    }

    const auto liveOpen = !candles.empty() ? candles.back().openTime : paddedTo;

    if (gapLogger.allow("live_gap_info", std::chrono::milliseconds(500))) {
      LOG_INFO(logging::LogCategory::DATA,
               "Live candle gap detected count=%llu expected=[%lld,%lld] live_open=%lld symbol=%s interval=%s",
               static_cast<unsigned long long>(seq),
               static_cast<long long>(expectedFrom),
               static_cast<long long>(expectedTo),
               static_cast<long long>(liveOpen),
               symbol.c_str(),
               interval.c_str());
    } else {
      LOG_DEBUG(logging::LogCategory::DATA,
                "Live candle gap detected count=%llu expected=[%lld,%lld] live_open=%lld (suppressed)",
                static_cast<unsigned long long>(seq),
                static_cast<long long>(expectedFrom),
                static_cast<long long>(expectedTo),
                static_cast<long long>(liveOpen));
    }

#ifdef DIAG_SYNC
    if (diagGapLogger.allow("diag_live_gap", std::chrono::milliseconds(250))) {
      const auto repoMeta = repo_.metadata();
      LOG_INFO(logging::LogCategory::DATA,
               "DIAG_SYNC live_gap sid=%llu seq=%llu expected_from=%lld expected_to=%lld padded_from=%lld padded_to=%lld repoTotal=%zu intervalMs=%lld liveOpen=%lld",
               static_cast<unsigned long long>(sidSnapshot),
               static_cast<unsigned long long>(seq),
               static_cast<long long>(expectedFrom),
               static_cast<long long>(expectedTo),
               static_cast<long long>(paddedFrom),
               static_cast<long long>(paddedTo),
               repoMeta.count,
               static_cast<long long>(intervalMs),
               static_cast<long long>(liveOpen));
    }
#endif

    if (paddedTo >= paddedFrom) {
      scheduleTargetedBackfill_(paddedFrom, paddedTo);
    }
    batch.clear();
    return;
  }

  if (liveGapPending_.load(std::memory_order_acquire)) {
    liveGapPending_.store(false, std::memory_order_release);
  }

  const bool appended = summary.appended > 0;
  const bool replaced = summary.state == domain::RangeState::Replaced;
  const bool closed = anyClosedFinal;

  const auto now = std::chrono::steady_clock::now();
  bool shouldPublish = appended || (replaced && closed);
  if (!shouldPublish && now - lastLivePublish_ >= kLivePublishThrottle) {
    shouldPublish = true;
  }

  if (shouldPublish) {
    lastLivePublish_ = now;
    scheduleSnapshotPublish_();
  }

  batch.clear();
}

void SyncOrchestrator::handleStreamError_(std::uint64_t /*sid*/, const domain::StreamError& err) {
  LOG_WARN(logging::LogCategory::NET,
           "NET:ws stream error code=%d message=%s",
           err.code,
           err.message.c_str());
}

void SyncOrchestrator::startLiveBatcher_() {
  stopLiveBatcher_();
  stopLiveBatch_.store(false, std::memory_order_release);
  {
    std::lock_guard<std::mutex> lk(liveQueueMtx_);
    liveQueue_.clear();
    liveQueueFirstEnqueue_.reset();
  }
  try {
    liveBatchThread_ = std::thread(&SyncOrchestrator::liveBatchLoop_, this);
  } catch (...) {
    stopLiveBatch_.store(true, std::memory_order_release);
    LOG_WARN(logging::LogCategory::DATA, "Failed to start live batch thread");
  }
}

void SyncOrchestrator::stopLiveBatcher_() {
  stopLiveBatch_.store(true, std::memory_order_release);
  liveQueueCv_.notify_all();
  if (liveBatchThread_.joinable()) {
    try {
      liveBatchThread_.join();
    } catch (...) {
      LOG_WARN(logging::LogCategory::DATA, "Live batch thread join threw");
    }
  }
  {
    std::lock_guard<std::mutex> lk(liveQueueMtx_);
    liveQueue_.clear();
    liveQueueFirstEnqueue_.reset();
  }
}

void SyncOrchestrator::liveBatchLoop_() {
  std::vector<domain::LiveCandle> batch;
  batch.reserve(64);

  while (true) {
    std::unique_lock lk(liveQueueMtx_);
    liveQueueCv_.wait_for(lk, kLiveBatchMaxInterval, [this] {
      return stopLiveBatch_.load(std::memory_order_acquire) || !liveQueue_.empty();
    });

    if (stopLiveBatch_.load(std::memory_order_acquire) && liveQueue_.empty()) {
      break;
    }

    if (liveQueue_.empty()) {
      continue;
    }

    if (!liveQueueFirstEnqueue_) {
      liveQueueFirstEnqueue_ = std::chrono::steady_clock::now();
    }

    const auto firstEnqueue = *liveQueueFirstEnqueue_;
    const auto minDeadline = firstEnqueue + kLiveBatchMinInterval;
    const auto maxDeadline = firstEnqueue + kLiveBatchMaxInterval;

    while (!stopLiveBatch_.load(std::memory_order_acquire)) {
      const auto now = std::chrono::steady_clock::now();
      if (liveQueue_.size() >= kLiveBatchImmediateThreshold || now >= minDeadline || now >= maxDeadline) {
        break;
      }

      liveQueueCv_.wait_until(lk, minDeadline, [this] {
        return stopLiveBatch_.load(std::memory_order_acquire) ||
               liveQueue_.size() >= kLiveBatchImmediateThreshold;
      });
      if (stopLiveBatch_.load(std::memory_order_acquire) && liveQueue_.empty()) {
        break;
      }
    }

    if (stopLiveBatch_.load(std::memory_order_acquire) && liveQueue_.empty()) {
      break;
    }

    const auto now = std::chrono::steady_clock::now();
    if (!stopLiveBatch_.load(std::memory_order_acquire) && liveQueue_.size() < kLiveBatchImmediateThreshold &&
        now < minDeadline && now < maxDeadline) {
      continue;
    }

    batch.swap(liveQueue_);
    liveQueueFirstEnqueue_.reset();
    lk.unlock();

    processLiveBatch_(batch);
    batch.clear();
  }

  std::vector<domain::LiveCandle> remaining;
  {
    std::lock_guard<std::mutex> lk2(liveQueueMtx_);
    remaining.swap(liveQueue_);
    liveQueueFirstEnqueue_.reset();
  }
  processLiveBatch_(remaining);
}

void SyncOrchestrator::scheduleTargetedBackfill_(domain::TimestampMs start, domain::TimestampMs end) {
  if (!running_.load(std::memory_order_acquire)) {
    return;
  }

  if (end < start) {
    std::swap(start, end);
  }

  if (end <= 0) {
    return;
  }

  if (start < 0) {
    start = 0;
  }

  std::optional<SessionState> session;
  std::uint64_t sid = 0;
  {
    std::scoped_lock lk(mtx_);
    if (!running_.load(std::memory_order_acquire)) {
      return;
    }
    session = activeSession_;
    sid = sessionId_;
  }

  if (!session) {
    return;
  }

  if (targetedBackfillThread_.joinable()) {
    try {
      targetedBackfillThread_.join();
    } catch (...) {
      LOG_WARN(logging::LogCategory::DATA, "Targeted backfill thread join threw (pre-schedule)");
    }
  }

  const auto symbol = session->symbol;
  const auto interval = session->interval;

  try {
    targetedBackfillThread_ = std::thread([this, sid, symbol, interval, start, end] {
      if (!running_.load(std::memory_order_acquire) || !isSessionCurrent_(sid)) {
        return;
      }

      auto rangeStart = std::min(start, end);
      auto rangeEnd = std::max(start, end);

      const auto intervalMs = interval.valid() ? interval.ms : repo_.intervalMs();
      if (intervalMs > 0) {
        rangeStart = domain::align_down_ms(rangeStart, intervalMs);
        rangeEnd = domain::align_up_ms(rangeEnd, intervalMs);
      }

      auto inclusiveEnd = rangeEnd;
      if (intervalMs > 0) {
        inclusiveEnd = rangeEnd + intervalMs - 1;
      }
      domain::TimeRange range{rangeStart, inclusiveEnd};
      if (range.end <= range.start) {
        range.end = range.start + (intervalMs > 0 ? intervalMs : 1);
      }

      const auto spanMs = (rangeEnd > rangeStart) ? (rangeEnd - rangeStart) : (intervalMs > 0 ? intervalMs : 1);
      const auto denom = (intervalMs > 0) ? intervalMs : 1;
      std::size_t limit = static_cast<std::size_t>(spanMs / denom + 2);
      limit = std::max<std::size_t>(limit, kTargetedGapPaddingCandles);
      if (cfg_.backfillChunk > 0) {
        limit = std::max<std::size_t>(limit, static_cast<std::size_t>(cfg_.backfillChunk));
      }

      LOG_INFO(logging::LogCategory::DATA,
               "DATA:targeted_backfill scheduling symbol=%s interval=%s window=[%lld,%lld] limit=%zu",
               symbol.c_str(),
               intervalLabel(interval).c_str(),
               static_cast<long long>(rangeStart),
               static_cast<long long>(rangeEnd),
               limit);

      std::vector<domain::Candle> batch;
      try {
        batch = gw_.fetchRange(symbol, interval, range, limit);
      } catch (const std::exception& ex) {
        LOG_WARN(logging::LogCategory::NET,
                 "NET:targeted_backfill fetchRange failed window=[%lld,%lld] error=%s",
                 static_cast<long long>(range.start),
                 static_cast<long long>(range.end),
                 ex.what());
        return;
      } catch (...) {
        LOG_WARN(logging::LogCategory::NET,
                 "NET:targeted_backfill fetchRange failed window=[%lld,%lld] error=unknown",
                 static_cast<long long>(range.start),
                 static_cast<long long>(range.end));
        return;
      }

      if (!running_.load(std::memory_order_acquire) || !isSessionCurrent_(sid)) {
        return;
      }

      if (!batch.empty()) {
        std::sort(batch.begin(), batch.end(), [](const domain::Candle& lhs, const domain::Candle& rhs) {
          return lhs.openTime < rhs.openTime;
        });
      }

      domain::TimestampMs loggedStart = range.start;
      domain::TimestampMs loggedEnd = range.end;
      if (!batch.empty()) {
        loggedStart = batch.front().openTime;
        loggedEnd = batch.back().openTime;
      }

      domain::AppendResult appendSummary{};
      try {
        appendSummary = repo_.appendBatch(batch);
      } catch (const std::exception& ex) {
        LOG_WARN(logging::LogCategory::DATA,
                 "DATA:targeted_backfill appendBatch failed window=[%lld,%lld] error=%s",
                 static_cast<long long>(loggedStart),
                 static_cast<long long>(loggedEnd),
                 ex.what());
        return;
      } catch (...) {
        LOG_WARN(logging::LogCategory::DATA,
                 "DATA:targeted_backfill appendBatch failed window=[%lld,%lld] error=unknown",
                 static_cast<long long>(loggedStart),
                 static_cast<long long>(loggedEnd));
        return;
      }

      LOG_INFO(logging::LogCategory::DATA,
               "DATA:targeted_backfill window=[%lld,%lld] fetched=%zu appended=%zu",
               static_cast<long long>(loggedStart),
               static_cast<long long>(loggedEnd),
               batch.size(),
               appendSummary.appended);

      if (appendSummary.appended > 0) {
        if (!repo_.hasGap()) {
          gapInFlight_.store(false, std::memory_order_release);
        }
        scheduleSnapshotPublish_();
      }

      std::this_thread::sleep_for(kTargetedBackfillMinSleep);
    });
  } catch (...) {
    LOG_WARN(logging::LogCategory::DATA, "Failed to start targeted backfill thread");
  }
}

bool SyncOrchestrator::isSessionCurrent_(std::uint64_t sid) const {
  std::scoped_lock lk(mtx_);
  return sid == sessionId_ && running_.load(std::memory_order_acquire);
}

void SyncOrchestrator::publishSnapshotLoading_(const SessionState& s) {
  LOG_INFO(logging::LogCategory::SNAPSHOT,
           "SNAPSHOT:publish state=%s symbol=%s interval=%s",
           uiDataStateLabel(core::UiDataState::Loading),
           s.symbol.c_str(),
           intervalLabel(s.interval).c_str());

  // No vaciar el cache en Loading para preservar viewport/serie previa

  if (eventBus_) {
    core::EventBus::SeriesUpdated evt{};
    evt.count = 0;
    evt.firstOpen = 0;
    evt.lastOpen = 0;
    evt.lastClosed = false;
    evt.state = core::UiDataState::Loading;
    eventBus_->publishSeriesUpdated(evt);
  }
}

void SyncOrchestrator::flushSnapshot_() {
#ifdef TTP_ENABLE_DIAG
  struct FlushDiagGuard {
    std::chrono::steady_clock::time_point start{std::chrono::steady_clock::now()};
    std::size_t repoCount{0};
    std::size_t latestCount{0};
    bool log{false};
    ~FlushDiagGuard() {
      const auto end = std::chrono::steady_clock::now();
      const auto nanos = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
      core::diag::incr("sync.flush.count");
      core::diag::observe("sync.flush.nanos", static_cast<std::uint64_t>(nanos));
      if (log) {
        LOG_INFO(logging::LogCategory::DATA,
                 "SYNC:flush repoCount=%zu latestCount=%zu",
                 repoCount,
                 latestCount);
      }
    }
  } flushDiagGuard;
  auto diagTimer = core::diag::timer("sync.flush");
#endif
  std::optional<SessionState> session;
  [[maybe_unused]] std::uint64_t sidSnapshot = 0;
  {
    std::scoped_lock lk(mtx_);
    session = activeSession_;
    sidSnapshot = sessionId_;
  }

  std::shared_ptr<domain::CandleSeries> owned;
  std::shared_ptr<const domain::CandleSeries> series;
#ifdef DIAG_SYNC
  std::size_t repoTotal = 0;
#else
  [[maybe_unused]] std::size_t repoTotal = 0;
#endif
  std::size_t desired = publishCount_;
  const bool gapInFlight = gapInFlight_.load(std::memory_order_acquire);
  bool repoHasGap = false;
  bool reusedLastSeries = false;
  decltype(repo_.metadata()) repoView{};
  {
    std::scoped_lock lk(publishMtx_);
    repoView = repo_.metadata();
    repoTotal = repoView.count;
#ifdef TTP_ENABLE_DIAG
    flushDiagGuard.repoCount = repoView.count;
    flushDiagGuard.log = true;
#endif
    repoHasGap = repo_.hasGap();
    if (repoHasGap) {
      publishCount_ =
          std::max(publishCount_, std::size_t{kMinHistoryCandlesReady});
      desired = publishCount_;
    }

    if (repoView.count >= kMinHistoryCandlesReady) {
      desired = std::max(desired, std::size_t{kMinHistoryCandlesReady});
    }
    if (gapInFlight || repoHasGap) {
      desired =
          std::max(desired,
                   std::max(lastStableCount_, std::size_t{kMinHistoryCandlesReady}));
    }

    if (gapInFlight && repoHasGap && lastPublishedSeries_) {
      series = lastPublishedSeries_;
      desired = series->data.size();
      reusedLastSeries = true;
#ifdef TTP_ENABLE_DIAG
      flushDiagGuard.latestCount = series ? series->data.size() : 0;
#endif
    } else {
      auto latest = repo_.getLatest(desired);
      if (latest.failed()) {
        LOG_WARN(logging::LogCategory::DATA,
                 "Unable to fetch latest candles for snapshot: %s",
                 latest.error.c_str());
#ifdef TTP_ENABLE_DIAG
      flushDiagGuard.latestCount = 0;
#endif
        return;
      }
      owned = std::make_shared<domain::CandleSeries>(std::move(latest.value));
      series = std::const_pointer_cast<const domain::CandleSeries>(owned);
#ifdef TTP_ENABLE_DIAG
      flushDiagGuard.latestCount = series ? series->data.size() : 0;
#endif
    }
  }

  if (!repoHasGap && gapInFlight) {
    gapInFlight_.store(false, std::memory_order_release);
  }

  std::size_t count = 0;
  if (reusedLastSeries) {
    count = lastPublishedCount_;
  } else {
    count = series ? series->data.size() : 0;
  }
  core::UiDataState uiState = core::UiDataState::Loading;
  if (reusedLastSeries && series) {
    uiState = lastPublishedState_;
  } else if (count == 0) {
    uiState = core::UiDataState::Loading;
  } else if (count < kMinHistoryCandlesReady && desired < kMinHistoryCandlesReady) {
    uiState = core::UiDataState::LiveOnly;
  } else {
    uiState = core::UiDataState::Ready;
  }

  if (uiState == core::UiDataState::Ready && !repoHasGap && series) {
    lastStableCount_ = series->data.size();
  }

  const bool liveGapActive = liveGapPending_.load(std::memory_order_acquire);

  const std::string symbol = session ? session->symbol : std::string{"?"};
  const std::string interval = session ? intervalLabel(session->interval) : std::string{"?"};

  std::vector<domain::Candle> tail;
  if (series && !series->data.empty()) {
    const std::size_t tailCount = std::min<std::size_t>(std::size_t{8}, series->data.size());
    tail.insert(tail.end(), series->data.end() - tailCount, series->data.end());
  }

  std::optional<std::uint64_t> tailHash;
  if (!tail.empty()) {
    tailHash = hashCandleTail(tail);
  }

  bool shouldPublishSnapshot = false;
#ifdef DIAG_SYNC
  std::string shouldPublishReason;
  shouldPublishSnapshot =
      shouldPublish_(uiState, symbol, interval, count, tail, &shouldPublishReason);
#else
  shouldPublishSnapshot = shouldPublish_(uiState, symbol, interval, count, tail);
#endif

  if (!shouldPublishSnapshot && liveGapActive != lastPublishedLiveGap_) {
#ifdef DIAG_SYNC
    if (shouldPublishReason.empty()) {
      shouldPublishReason = "live_gap_changed";
    }
#endif
    shouldPublishSnapshot = true;
  }

#ifdef DIAG_SYNC
  static core::LogRateLimiter diagSnapshotLimiter{std::chrono::milliseconds(150)};
  if (diagSnapshotLimiter.allow()) {
    const unsigned long long tailHashValue =
        tailHash ? static_cast<unsigned long long>(*tailHash) : 0ull;
    LOG_INFO(logging::LogCategory::DATA,
             "DIAG_SYNC snapshot sid=%llu state=%s count=%zu repoTotal=%zu publishCount=%zu desired=%zu tailHashPresent=%d tailHash=0x%llx shouldPublish=%d reason=%s",
             static_cast<unsigned long long>(sidSnapshot),
             uiDataStateLabel(uiState),
             count,
             repoTotal,
             publishCount_,
             desired,
             tailHash.has_value() ? 1 : 0,
             tailHashValue,
             shouldPublishSnapshot ? 1 : 0,
             shouldPublishReason.empty() ? "n/a" : shouldPublishReason.c_str());
  }
#endif

  if (!shouldPublishSnapshot) {
    return;
  }

  const auto prevState = lastPublishedState_;
  const auto prevSymbol = lastPublishedSymbol_;
  const auto prevInterval = lastPublishedInterval_;

  if (seriesCache_) {
    seriesCache_->update(series);
  }

  const bool stateTransition = uiState != prevState;
  const bool identityChanged = symbol != prevSymbol || interval != prevInterval;
  static core::LogRateLimiter snapshotLogLimiter{std::chrono::milliseconds(250)};
  if (stateTransition || identityChanged) {
    LOG_INFO(logging::LogCategory::SNAPSHOT,
             "SNAPSHOT:publish state=%s symbol=%s interval=%s candles=%zu",
             uiDataStateLabel(uiState),
             symbol.c_str(),
             interval.c_str(),
             count);
  } else if (snapshotLogLimiter.allow()) {
    LOG_DEBUG(logging::LogCategory::SNAPSHOT,
              "SNAPSHOT:publish state=%s symbol=%s interval=%s candles=%zu",
              uiDataStateLabel(uiState),
              symbol.c_str(),
              interval.c_str(),
              count);
  }

  const auto newVersion = snapshotVersion_.fetch_add(1, std::memory_order_acq_rel) + 1;
  lastPublishedVersion_ = newVersion;
  lastPublishedState_ = uiState;
  lastPublishedSymbol_ = symbol;
  lastPublishedInterval_ = interval;
  lastPublishedCount_ = count;
  lastPublishedTail_ = std::move(tail);
  lastPublishedSeries_ = series;
  lastPublishedLiveGap_ = liveGapActive;

  if (eventBus_ && series) {
    core::EventBus::SeriesUpdated evt{};
    evt.count = series->data.size();
    evt.firstOpen = series->data.empty() ? 0 : series->firstOpen;
    evt.lastOpen = series->data.empty() ? 0 : series->lastOpen;
    evt.lastClosed = !series->data.empty() && series->data.back().isClosed;
    evt.tailHash = tailHash;
    evt.state = uiState;
    eventBus_->publishSeriesUpdated(evt);
  }

  seeded_.store(true, std::memory_order_release);
}

bool SyncOrchestrator::shouldPublish_(core::UiDataState state,
                                      const std::string& symbol,
                                      const std::string& interval,
                                      std::size_t count,
                                      const std::vector<domain::Candle>& tail
#ifdef DIAG_SYNC
                                      , std::string* reasonOut
#endif
                                      ) {
#ifdef DIAG_SYNC
  auto setReason = [&](const std::string& reason) {
    if (reasonOut) {
      *reasonOut = reason;
    }
  };
#endif
  if (lastPublishedVersion_ == 0) {
#ifdef DIAG_SYNC
    setReason("first_publish");
#endif
    return true;
  }
  if (state != lastPublishedState_) {
#ifdef DIAG_SYNC
    setReason("state_changed");
#endif
    return true;
  }
  if (symbol != lastPublishedSymbol_ || interval != lastPublishedInterval_) {
#ifdef DIAG_SYNC
    setReason("identity_changed");
#endif
    return true;
  }
  if (count != lastPublishedCount_) {
#ifdef DIAG_SYNC
    setReason("count_changed");
#endif
    return true;
  }
  if (tail.size() != lastPublishedTail_.size()) {
#ifdef DIAG_SYNC
    setReason("tail_size_changed");
#endif
    return true;
  }
  for (std::size_t i = 0; i < tail.size(); ++i) {
    const auto& curr = tail[i];
    const auto& prev = lastPublishedTail_[i];
    if (curr.openTime != prev.openTime || curr.closeTime != prev.closeTime ||
        curr.open != prev.open || curr.high != prev.high || curr.low != prev.low ||
        curr.close != prev.close || curr.isClosed != prev.isClosed) {
#ifdef DIAG_SYNC
      setReason("tail_differs_" + std::to_string(i));
#endif
      return true;
    }
  }
#ifdef DIAG_SYNC
  setReason("unchanged");
#endif
  return false;
}

void SyncOrchestrator::scheduleSnapshotPublish_() {
  pendingSnapshot_.test_and_set();
}

void SyncOrchestrator::startCoalescer_() {
  stopCoalescer_();
  stopCoalesce_.store(false, std::memory_order_release);
  pendingSnapshot_.clear();
  try {
    coalesceThread_ = std::thread(&SyncOrchestrator::coalesceLoop_, this);
  } catch (...) {
    LOG_WARN(logging::LogCategory::DATA, "Failed to start snapshot coalescer thread");
  }
}

void SyncOrchestrator::stopCoalescer_() {
  stopCoalesce_.store(true, std::memory_order_release);
  if (coalesceThread_.joinable()) {
    try {
      coalesceThread_.join();
    } catch (...) {
      LOG_WARN(logging::LogCategory::DATA, "Snapshot coalescer thread join threw");
    }
  }
  stopCoalesce_.store(false, std::memory_order_release);
  pendingSnapshot_.clear();
}

void SyncOrchestrator::coalesceLoop_() {
  using namespace std::chrono_literals;
  const auto minInterval = std::chrono::milliseconds(33);
  while (!stopCoalesce_.load(std::memory_order_acquire)) {
    if (pendingSnapshot_.test_and_set()) {
#ifdef TTP_ENABLE_DIAG
      core::diag::incr("sync.coalesce.wake");
#endif
      const auto now = std::chrono::steady_clock::now();
      if (!lastPublishTimeOpt_.has_value()) {
#ifdef TTP_ENABLE_DIAG
        core::diag::incr("sync.coalesce.flush");
#endif
        flushSnapshot_();
        lastPublishTimeOpt_ = now;
      } else {
        const auto elapsed = now - *lastPublishTimeOpt_;
        if (elapsed < minInterval) {
          std::this_thread::sleep_for(minInterval - elapsed);
        }
#ifdef TTP_ENABLE_DIAG
        core::diag::incr("sync.coalesce.flush");
#endif
        flushSnapshot_();
        lastPublishTimeOpt_ = std::chrono::steady_clock::now();
      }
      pendingSnapshot_.clear();
    } else {
      pendingSnapshot_.clear();
      std::this_thread::sleep_for(2ms);
    }
  }

  if (pendingSnapshot_.test_and_set()) {
#ifdef TTP_ENABLE_DIAG
    core::diag::incr("sync.coalesce.wake");
#endif
    const auto now = std::chrono::steady_clock::now();
    if (!lastPublishTimeOpt_.has_value()) {
#ifdef TTP_ENABLE_DIAG
      core::diag::incr("sync.coalesce.flush");
#endif
      flushSnapshot_();
      lastPublishTimeOpt_ = now;
    } else {
#ifdef TTP_ENABLE_DIAG
      core::diag::incr("sync.coalesce.flush");
#endif
      flushSnapshot_();
      lastPublishTimeOpt_ = std::chrono::steady_clock::now();
    }
    pendingSnapshot_.clear();
  }
}

std::int64_t SyncOrchestrator::nowMs_() {
  using namespace std::chrono;
  return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

std::int64_t SyncOrchestrator::alignDownToIntervalMs_(std::int64_t t, const SessionState& s) {
  const std::int64_t intervalMs = s.interval.valid() ? s.interval.ms : 0;
  if (intervalMs <= 0) {
    return t;
  }
  return (t / intervalMs) * intervalMs;
}

}  // namespace app
