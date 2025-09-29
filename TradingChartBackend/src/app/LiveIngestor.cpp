#include "app/LiveIngestor.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstdint>
#include <exception>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <cerrno>

#include "adapters/duckdb/DuckCandleRepo.hpp"
#include "api/WebSocketServer.hpp"
#include "domain/Types.h"
#include "logging/Log.h"
#include "common/Metrics.hpp"

namespace app {
namespace {

namespace exchange = domain;

constexpr logging::LogCategory kLogCategory = logging::LogCategory::DATA;
constexpr std::int64_t kBootstrapCandles = 200;
constexpr std::int64_t kMillisecondsThreshold = 1'000'000'000'000LL;
constexpr std::size_t kResyncPageLimit = 1000;
constexpr const char* kEnvWsEmitPartials = "WS_EMIT_PARTIALS";
constexpr const char* kEnvWsPartialThrottleMs = "WS_PARTIAL_THROTTLE_MS";

std::int64_t interval_to_ms(exchange::Interval interval) {
    if (!interval.valid()) {
        throw std::invalid_argument("Unsupported exchange interval");
    }
    return interval.ms;
}

std::int64_t normalize_to_milliseconds(std::int64_t ts) {
    if (ts > 0 && ts < kMillisecondsThreshold) {
        return ts * 1000LL;
    }
    return ts;
}

std::optional<std::int64_t> current_open_candle_floor(std::int64_t intervalMs) {
    if (intervalMs <= 0) {
        return std::nullopt;
    }

    const auto now = std::chrono::system_clock::now();
    const auto nowMs =
        std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
    return domain::align_down_ms(nowMs, intervalMs);
}

std::vector<std::string> sanitize_symbols(const std::vector<std::string>& symbols) {
    std::vector<std::string> filtered;
    filtered.reserve(symbols.size());

    for (const auto& symbol : symbols) {
        if (symbol.empty()) {
            continue;
        }
        if (std::find(filtered.begin(), filtered.end(), symbol) == filtered.end()) {
            filtered.push_back(symbol);
        }
    }

    return filtered;
}

std::vector<domain::Candle> to_domain_candles(const std::vector<exchange::Candle>& rows,
                                              std::int64_t intervalMs) {
    std::vector<domain::Candle> converted;
    converted.reserve(rows.size());

    for (auto candle : rows) {
        candle.openTime = normalize_to_milliseconds(candle.openTime);
        candle.closeTime = normalize_to_milliseconds(candle.closeTime);
        if (intervalMs > 0 && candle.openTime > 0) {
            candle.closeTime = candle.openTime + intervalMs - 1;
        } else if (candle.closeTime <= 0) {
            candle.closeTime = candle.openTime;
        }
        candle.isClosed = true;
        converted.push_back(candle);
    }

    return converted;
}

bool parse_env_bool(const char* name, bool defaultValue) {
    const char* value = std::getenv(name);
    if (value == nullptr) {
        return defaultValue;
    }

    std::string normalized(value);
    std::transform(normalized.begin(), normalized.end(), normalized.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });

    if (normalized == "1" || normalized == "true" || normalized == "yes" || normalized == "on") {
        return true;
    }
    if (normalized == "0" || normalized == "false" || normalized == "no" || normalized == "off") {
        return false;
    }

    LOG_WARN(kLogCategory,
             "LiveIngestor: invalid boolean for %s=%s, using default=%s",
             name,
             value,
             defaultValue ? "true" : "false");
    return defaultValue;
}

std::int64_t parse_env_int(const char* name, std::int64_t defaultValue) {
    const char* value = std::getenv(name);
    if (value == nullptr) {
        return defaultValue;
    }

    errno = 0;
    char* endPtr = nullptr;
    const long long parsed = std::strtoll(value, &endPtr, 10);
    if (endPtr == value || (endPtr != nullptr && *endPtr != '\0') || errno == ERANGE) {
        LOG_WARN(kLogCategory,
                 "LiveIngestor: invalid integer for %s=%s, using default=%lld",
                 name,
                 value,
                 static_cast<long long>(defaultValue));
        return defaultValue;
    }
    if (parsed < 0) {
        LOG_WARN(kLogCategory,
                 "LiveIngestor: negative value ignored for %s=%lld, using default=%lld",
                 name,
                 static_cast<long long>(parsed),
                 static_cast<long long>(defaultValue));
        return defaultValue;
    }

    return static_cast<std::int64_t>(parsed);
}

void broadcast_candle(const std::string& symbol,
                      const std::string& interval,
                      const domain::Candle& candle,
                      bool isFinal) {
    const auto tsMs = normalize_to_milliseconds(candle.openTime);

    std::ostringstream oss;
    oss << "{\"type\":\"candle\",\"symbol\":\"" << symbol << "\",\"interval\":\"" << interval
        << "\",\"final\":" << (isFinal ? "true" : "false") << ",\"data\":["
        << static_cast<long long>(tsMs) << ',' << candle.open << ',' << candle.high << ',' << candle.low << ','
        << candle.close << ',' << candle.baseVolume << "]}";

    ttp::api::broadcast(oss.str());
    LOG_DEBUG(kLogCategory,
              "LiveIngestor: broadcast candle symbol=%s interval=%s open_ms=%lld final=%s",
              symbol.c_str(),
              interval.c_str(),
              static_cast<long long>(tsMs),
              isFinal ? "true" : "false");
}

}  // namespace

LiveIngestor::LiveIngestor(adapters::duckdb::DuckCandleRepo& repo,
                           domain::IExchangeKlines& rest,
                           domain::IExchangeLiveKlines& ws)
    : repo_(repo), rest_(rest), ws_(ws) {}

LiveIngestor::~LiveIngestor() {
    stop();
}

void LiveIngestor::run(const std::vector<std::string>& symbols, domain::Interval interval) {
    if (worker_.joinable()) {
        stop();
    }

    stopRequested_.store(false, std::memory_order_relaxed);

    std::vector<std::string> symbolsCopy = symbols;
    worker_ = std::thread([this, symbolsCopy = std::move(symbolsCopy), interval]() mutable {
        try {
            LOG_INFO(kLogCategory, "LiveIngestor thread starting");
            this->run_worker_(std::move(symbolsCopy), interval);
            LOG_INFO(kLogCategory, "LiveIngestor thread finished cleanly");
        }
        catch (const std::exception& ex) {
            LOG_ERROR(kLogCategory, "LiveIngestor thread crashed: %s", ex.what());
        }
        catch (...) {
            LOG_ERROR(kLogCategory, "LiveIngestor thread crashed: unknown exception");
        }
    });
}

void LiveIngestor::run_worker_(std::vector<std::string> symbols, domain::Interval interval) {
    const auto intervalLabel = exchange::to_string(interval);
    if (intervalLabel.empty()) {
        LOG_WARN(kLogCategory, "LiveIngestor: unsupported interval provided");
        return;
    }

    std::int64_t intervalMs = 0;
    try {
        intervalMs = interval_to_ms(interval);
    }
    catch (const std::exception& ex) {
        LOG_WARN(kLogCategory, "LiveIngestor: unable to map interval=%s error=%s", intervalLabel.c_str(), ex.what());
        return;
    }

    ttp::common::metrics::Registry::instance().setGauge("interval_ms", static_cast<double>(intervalMs));

    const auto normalizedInterval = domain::interval_from_label(intervalLabel);
    if (!normalizedInterval.valid()) {
        LOG_WARN(kLogCategory, "LiveIngestor: invalid persistence interval label=%s", intervalLabel.c_str());
        return;
    }

    const auto sanitized = sanitize_symbols(symbols);
    if (sanitized.empty()) {
        LOG_WARN(kLogCategory, "LiveIngestor: no symbols provided for run");
        return;
    }

    {
        std::lock_guard<std::mutex> lock(lastClosedMutex_);
        lastClosedOpenMs_.clear();
    }

    {
        std::lock_guard<std::mutex> lock(liveMutex_);
        liveCandles_.clear();
        lastBroadcastMs_.clear();
    }

    emitPartials_ = parse_env_bool(kEnvWsEmitPartials, true);
    partialThrottleMs_ = parse_env_int(kEnvWsPartialThrottleMs, 0);

    LOG_INFO(kLogCategory,
             "LiveIngestor: WS config emit_partials=%s throttle_ms=%lld",
             emitPartials_ ? "true" : "false",
             static_cast<long long>(partialThrottleMs_));

    const auto now = std::chrono::system_clock::now();
    const auto nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
    const auto staleThreshold = nowMs - 2 * intervalMs;
    const auto bootstrapFromMs = std::max<std::int64_t>(0, nowMs - kBootstrapCandles * intervalMs);

    for (const auto& symbol : sanitized) {
        if (stopRequested_.load(std::memory_order_relaxed)) {
            LOG_INFO(kLogCategory, "LiveIngestor: stop requested before resync completes");
            return;
        }

        std::optional<std::int64_t> maxTsOpt;
        try {
            maxTsOpt = repo_.max_timestamp(symbol, intervalLabel);
        }
        catch (const std::exception& ex) {
            LOG_WARN(kLogCategory,
                     "LiveIngestor: failed to query last candle symbol=%s interval=%s error=%s",
                     symbol.c_str(),
                     intervalLabel.c_str(),
                     ex.what());
            continue;
        }

        const auto lastStored = maxTsOpt.value_or(0);
        const auto lastStoredMs = maxTsOpt.has_value() ? normalize_to_milliseconds(lastStored) : 0;
        if (maxTsOpt.has_value()) {
            const auto lastStoredOpen = domain::align_down_ms(lastStoredMs, intervalMs);
            record_last_closed_open_(symbol, lastStoredOpen);
        }
        const bool needsResync = !maxTsOpt.has_value() || lastStored < staleThreshold;

        if (needsResync) {
            std::int64_t currentStartOpen = 0;
            if (maxTsOpt.has_value()) {
                const auto lastStoredOpen = domain::align_down_ms(lastStoredMs, intervalMs);
                currentStartOpen = lastStoredOpen + intervalMs;
            } else {
                currentStartOpen = domain::align_down_ms(bootstrapFromMs, intervalMs);
            }
            currentStartOpen = std::max<std::int64_t>(0, currentStartOpen);

            const auto nowOpenOptForLog = current_open_candle_floor(intervalMs);
            const auto nowOpenForLog = nowOpenOptForLog.has_value()
                                            ? *nowOpenOptForLog
                                            : domain::align_down_ms(nowMs, intervalMs);

            LOG_INFO(kLogCategory,
                     "LiveIngestor: resyncing symbol=%s interval=%s from_open_ms=%lld to_open_ms=%lld",
                     symbol.c_str(),
                     intervalLabel.c_str(),
                     static_cast<long long>(currentStartOpen),
                     static_cast<long long>(nowOpenForLog));

            std::int64_t lastEmittedStartOpen = -1;
            std::int64_t previousStartOpen = -1;
            int stalledPages = 0;
            constexpr int kMaxStalledPages = 3;

            while (!stopRequested_.load(std::memory_order_relaxed)) {
                if (previousStartOpen == currentStartOpen) {
                    ++stalledPages;
                    if (stalledPages >= kMaxStalledPages) {
                        LOG_WARN(kLogCategory,
                                 "LiveIngestor: pagination stalled (repeated start_open) symbol=%s interval=%s start_open=%lld",
                                 symbol.c_str(),
                                 intervalLabel.c_str(),
                                 static_cast<long long>(currentStartOpen));
                        break;
                    }
                } else {
                    stalledPages = 0;
                }
                previousStartOpen = currentStartOpen;

                const auto nowOpenOpt = current_open_candle_floor(intervalMs);
                const auto nowOpenMs = nowOpenOpt.has_value()
                                           ? *nowOpenOpt
                                           : domain::align_down_ms(nowMs, intervalMs);

                if (currentStartOpen >= nowOpenMs) {
                    LOG_INFO(kLogCategory,
                             "LiveIngestor: resync reached current open candle; switching to WS symbol=%s interval=%s "
                             "start_open_ms=%lld now_open_ms=%lld",
                             symbol.c_str(),
                             intervalLabel.c_str(),
                             static_cast<long long>(currentStartOpen),
                             static_cast<long long>(nowOpenMs));
                    break;
                }

                const auto maxWindow = static_cast<std::int64_t>(kResyncPageLimit) * intervalMs;
                auto endOpenMs = currentStartOpen + maxWindow;
                if (endOpenMs > nowOpenMs) {
                    endOpenMs = nowOpenMs;
                }

                if (endOpenMs <= currentStartOpen) {
                    LOG_INFO(kLogCategory,
                             "LiveIngestor: resync reached end of historical window symbol=%s interval=%s start_open_ms=%lld",
                             symbol.c_str(),
                             intervalLabel.c_str(),
                             static_cast<long long>(currentStartOpen));
                    break;
                }

                const auto startSeconds = currentStartOpen / 1000;
                const auto endSeconds = endOpenMs / 1000;

                LOG_DEBUG(kLogCategory,
                          "LiveIngestor: resync page symbol=%s interval=%s start_open_ms=%lld end_open_ms=%lld",
                          symbol.c_str(),
                          intervalLabel.c_str(),
                          static_cast<long long>(currentStartOpen),
                          static_cast<long long>(endOpenMs));

                exchange::KlinesPage page;
                try {
                    page = rest_.fetch_klines(symbol, interval, startSeconds, endSeconds, kResyncPageLimit);
                }
                catch (const std::exception& ex) {
                    LOG_WARN(kLogCategory,
                             "LiveIngestor: REST resync failed symbol=%s interval=%s start_open=%lld end_open=%lld error=%s",
                             symbol.c_str(),
                             intervalLabel.c_str(),
                             static_cast<long long>(currentStartOpen),
                             static_cast<long long>(endOpenMs),
                             ex.what());
                    break;
                }
                catch (...) {
                    LOG_WARN(kLogCategory,
                             "LiveIngestor: REST resync encountered unknown error symbol=%s interval=%s start_open=%lld end_open=%lld",
                             symbol.c_str(),
                             intervalLabel.c_str(),
                             static_cast<long long>(currentStartOpen),
                             static_cast<long long>(endOpenMs));
                    break;
                }

                if (page.rows.empty()) {
                    LOG_INFO(kLogCategory,
                             "LiveIngestor: REST resync returned no rows symbol=%s interval=%s start_open=%lld end_open=%lld",
                             symbol.c_str(),
                             intervalLabel.c_str(),
                             static_cast<long long>(currentStartOpen),
                             static_cast<long long>(endOpenMs));
                    break;
                }

                auto repoRows = to_domain_candles(page.rows, normalizedInterval.ms);
                if (repoRows.empty()) {
                    LOG_WARN(kLogCategory,
                             "LiveIngestor: REST resync produced empty batch after conversion symbol=%s interval=%s",
                             symbol.c_str(),
                             intervalLabel.c_str());
                    break;
                }

                while (!repoRows.empty()) {
                    const auto lastOpenCandidate =
                        domain::align_down_ms(repoRows.back().closeTime, intervalMs);
                    if (lastOpenCandidate < nowOpenMs) {
                        break;
                    }
                    repoRows.pop_back();
                }

                if (repoRows.empty()) {
                    LOG_INFO(kLogCategory,
                             "LiveIngestor: resync batch discarded due to open >= now_open symbol=%s interval=%s start_open=%lld",
                             symbol.c_str(),
                             intervalLabel.c_str(),
                             static_cast<long long>(currentStartOpen));
                    break;
                }

                bool persisted = repo_.upsert_batch(symbol, intervalLabel, repoRows);
                if (!persisted) {
                    LOG_WARN(kLogCategory,
                             "LiveIngestor: failed to persist resync batch symbol=%s interval=%s size=%zu",
                             symbol.c_str(),
                             intervalLabel.c_str(),
                             repoRows.size());
                } else {
                    ttp::common::metrics::Registry::instance().incrementCounter(
                        "rest_catchup_candles_total",
                        static_cast<std::uint64_t>(repoRows.size()));
                    broadcast_candle(symbol, intervalLabel, repoRows.back(), true);
                }

                const auto lastCloseMs = repoRows.back().closeTime;
                const auto lastOpenMs = domain::align_down_ms(lastCloseMs, intervalMs);
                if (persisted) {
                    record_last_closed_open_(symbol, lastOpenMs);
                }
                const auto nextStartOpen = lastOpenMs + intervalMs;

                LOG_DEBUG(kLogCategory,
                          "LiveIngestor: resync batch symbol=%s interval=%s start_open_ms=%lld end_open_ms=%lld size=%zu "
                          "last_open_ms=%lld next_start_open_ms=%lld",
                          symbol.c_str(),
                          intervalLabel.c_str(),
                          static_cast<long long>(currentStartOpen),
                          static_cast<long long>(endOpenMs),
                          repoRows.size(),
                          static_cast<long long>(lastOpenMs),
                          static_cast<long long>(nextStartOpen));

                if (nextStartOpen <= currentStartOpen) {
                    LOG_WARN(kLogCategory,
                             "LiveIngestor: REST resync stalled symbol=%s interval=%s start_open=%lld next_start=%lld",
                             symbol.c_str(),
                             intervalLabel.c_str(),
                             static_cast<long long>(currentStartOpen),
                             static_cast<long long>(nextStartOpen));
                    break;
                }

                if (lastEmittedStartOpen >= 0 && nextStartOpen <= lastEmittedStartOpen) {
                    LOG_WARN(kLogCategory,
                             "LiveIngestor: pagination stalled: start_open unchanged symbol=%s interval=%s next_start=%lld",
                             symbol.c_str(),
                             intervalLabel.c_str(),
                             static_cast<long long>(nextStartOpen));
                    break;
                }

                lastEmittedStartOpen = nextStartOpen;
                currentStartOpen = nextStartOpen;
            }

            LOG_INFO(kLogCategory,
                     "LiveIngestor: resync completed symbol=%s interval=%s",
                     symbol.c_str(),
                     intervalLabel.c_str());
        }
        else {
            LOG_DEBUG(kLogCategory,
                      "LiveIngestor: skipping resync symbol=%s interval=%s last_ts=%lld",
                      symbol.c_str(),
                      intervalLabel.c_str(),
                      static_cast<long long>(lastStored));
        }
    }

    if (stopRequested_.load(std::memory_order_relaxed)) {
        LOG_INFO(kLogCategory, "LiveIngestor: stop requested before subscribing to live feed");
        return;
    }

    LOG_INFO(kLogCategory,
             "LiveIngestor: starting streaming interval=%s symbols=%zu",
             intervalLabel.c_str(),
             sanitized.size());

    ws_.set_on_reconnected([this,
                             intervalLabel,
                             interval,
                             intervalMs,
                             symbols = sanitized]() {
        if (stopRequested_.load(std::memory_order_relaxed)) {
            return;
        }
        try {
            this->catch_up_(symbols, intervalLabel, interval, intervalMs);
            LOG_INFO(kLogCategory,
                     "LiveIngestor: WS ready interval=%s symbols=%zu",
                     intervalLabel.c_str(),
                     symbols.size());
        }
        catch (const std::exception& ex) {
            LOG_WARN(kLogCategory,
                     "LiveIngestor: catch-up on reconnect failed interval=%s error=%s",
                     intervalLabel.c_str(),
                     ex.what());
        }
        catch (...) {
            LOG_WARN(kLogCategory,
                     "LiveIngestor: catch-up on reconnect failed with unknown error interval=%s",
                     intervalLabel.c_str());
        }
    });

    try {
        ws_.subscribe(sanitized,
                      interval,
                      [this, intervalLabel, intervalMs, normalizedInterval](const std::string& symbol,
                                                                           const exchange::Candle& candle) {
                          if (stopRequested_.load(std::memory_order_relaxed)) {
                              return;
                          }
                          try {
                              domain::Candle normalized = candle;
                              normalized.openTime = normalize_to_milliseconds(normalized.openTime);
                              normalized.closeTime = normalize_to_milliseconds(normalized.closeTime);
                              if (intervalMs > 0 && normalized.openTime > 0) {
                                  normalized.closeTime = normalized.openTime + intervalMs - 1;
                              } else if (normalized.closeTime <= 0) {
                                  normalized.closeTime = normalized.openTime;
                              }
                              normalized.isClosed = candle.isClosed;

                              const LiveKey key{symbol, normalizedInterval, normalized.openTime};

                              domain::Candle snapshot{};
                              bool shouldBroadcast = false;
                              bool shouldPersist = false;

                              {
                                  std::lock_guard<std::mutex> lock(liveMutex_);
                                  auto [it, inserted] = liveCandles_.try_emplace(key, normalized);
                                  if (!inserted) {
                                      it->second = normalized;
                                  }
                                  snapshot = it->second;

                                  if (normalized.isClosed) {
                                      shouldBroadcast = true;
                                      shouldPersist = true;
                                      liveCandles_.erase(it);
                                      lastBroadcastMs_.erase(key);
                                  } else if (!emitPartials_) {
                                      lastBroadcastMs_.erase(key);
                                  } else if (partialThrottleMs_ <= 0) {
                                      shouldBroadcast = true;
                                      lastBroadcastMs_.erase(key);
                                  } else {
                                      const auto nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                                                          std::chrono::system_clock::now().time_since_epoch())
                                                          .count();
                                      auto [tsIt, insertedTs] = lastBroadcastMs_.try_emplace(key, nowMs);
                                      if (insertedTs) {
                                          shouldBroadcast = true;
                                      } else if (nowMs - tsIt->second >= partialThrottleMs_) {
                                          tsIt->second = nowMs;
                                          shouldBroadcast = true;
                                      }
                                  }
                              }

                              if (shouldPersist) {
                                  std::vector<domain::Candle> rows;
                                  rows.reserve(1);
                                  rows.push_back(snapshot);

                                  const bool persisted = repo_.upsert_batch(symbol, intervalLabel, rows);
                                  if (!persisted) {
                                      LOG_WARN(kLogCategory,
                                               "LiveIngestor: failed to persist live candle symbol=%s interval=%s open_ms=%lld",
                                               symbol.c_str(),
                                               intervalLabel.c_str(),
                                               static_cast<long long>(snapshot.openTime));
                                  } else {
                                      record_last_closed_open_(symbol, snapshot.openTime);
                                  }
                              }

                              if (shouldBroadcast) {
                                  broadcast_candle(symbol, intervalLabel, snapshot, snapshot.isClosed);
                              }
                          }
                          catch (const std::exception& ex) {
                              LOG_WARN(kLogCategory,
                                       "LiveIngestor: exception while handling live candle symbol=%s interval=%s error=%s",
                                       symbol.c_str(),
                                       intervalLabel.c_str(),
                                       ex.what());
                          }
                          catch (...) {
                              LOG_WARN(kLogCategory,
                                       "LiveIngestor: unknown error while handling live candle symbol=%s interval=%s",
                                       symbol.c_str(),
                                       intervalLabel.c_str());
                          }
                      });
    }
    catch (const std::exception& ex) {
        LOG_WARN(kLogCategory,
                 "LiveIngestor: websocket subscription failed interval=%s error=%s",
                 intervalLabel.c_str(),
                 ex.what());
    }
    catch (...) {
        LOG_WARN(kLogCategory,
                 "LiveIngestor: websocket subscription failed with unknown error interval=%s",
                 intervalLabel.c_str());
    }
}

void LiveIngestor::catch_up_(const std::vector<std::string>& symbols,
                             const std::string& intervalLabel,
                             domain::Interval interval,
                             std::int64_t intervalMs) {
    if (intervalMs <= 0 || stopRequested_.load(std::memory_order_relaxed)) {
        return;
    }

    const auto nowOpenOpt = current_open_candle_floor(intervalMs);
    if (!nowOpenOpt.has_value()) {
        return;
    }
    const auto nowOpenMs = *nowOpenOpt;
    std::vector<std::string> resyncedSymbols;

    for (const auto& symbol : symbols) {
        if (stopRequested_.load(std::memory_order_relaxed)) {
            break;
        }

        auto lastOpenOpt = get_last_closed_open_ms_(symbol, intervalLabel, intervalMs);
        if (!lastOpenOpt.has_value()) {
            LOG_DEBUG(kLogCategory,
                      "LiveIngestor: catch-up skipped symbol=%s interval=%s reason=no-last-open",
                      symbol.c_str(),
                      intervalLabel.c_str());
            continue;
        }

        auto currentStartOpen = *lastOpenOpt + intervalMs;
        if (currentStartOpen >= nowOpenMs) {
            LOG_DEBUG(kLogCategory,
                      "LiveIngestor: catch-up not required symbol=%s interval=%s start_open_ms=%lld now_open_ms=%lld",
                      symbol.c_str(),
                      intervalLabel.c_str(),
                      static_cast<long long>(currentStartOpen),
                      static_cast<long long>(nowOpenMs));
            continue;
        }
        currentStartOpen = std::max<std::int64_t>(0, currentStartOpen);

        LOG_INFO(kLogCategory,
                 "LiveIngestor: catch-up starting symbol=%s interval=%s from_open_ms=%lld to_open_ms=%lld",
                 symbol.c_str(),
                 intervalLabel.c_str(),
                 static_cast<long long>(currentStartOpen),
                 static_cast<long long>(nowOpenMs));

        std::size_t totalPersisted = 0;

        while (!stopRequested_.load(std::memory_order_relaxed) && currentStartOpen < nowOpenMs) {
            const auto maxWindow = static_cast<std::int64_t>(kResyncPageLimit) * intervalMs;
            auto endOpenMs = currentStartOpen + maxWindow;
            if (endOpenMs > nowOpenMs) {
                endOpenMs = nowOpenMs;
            }
            if (endOpenMs <= currentStartOpen) {
                break;
            }

            exchange::KlinesPage page;
            try {
                page = rest_.fetch_klines(symbol,
                                          interval,
                                          currentStartOpen / 1000,
                                          endOpenMs / 1000,
                                          kResyncPageLimit);
            }
            catch (const std::exception& ex) {
                LOG_WARN(kLogCategory,
                         "LiveIngestor: catch-up REST failed symbol=%s interval=%s start_open=%lld end_open=%lld error=%s",
                         symbol.c_str(),
                         intervalLabel.c_str(),
                         static_cast<long long>(currentStartOpen),
                         static_cast<long long>(endOpenMs),
                         ex.what());
                break;
            }
            catch (...) {
                LOG_WARN(kLogCategory,
                         "LiveIngestor: catch-up REST failed with unknown error symbol=%s interval=%s start_open=%lld end_open=%lld",
                         symbol.c_str(),
                         intervalLabel.c_str(),
                         static_cast<long long>(currentStartOpen),
                         static_cast<long long>(endOpenMs));
                break;
            }

            if (page.rows.empty()) {
                LOG_INFO(kLogCategory,
                         "LiveIngestor: catch-up returned empty page symbol=%s interval=%s start_open=%lld end_open=%lld",
                         symbol.c_str(),
                         intervalLabel.c_str(),
                         static_cast<long long>(currentStartOpen),
                         static_cast<long long>(endOpenMs));
                break;
            }

            auto repoRows = to_domain_candles(page.rows, intervalMs);
            while (!repoRows.empty()) {
                const auto lastOpenCandidate = domain::align_down_ms(repoRows.back().closeTime, intervalMs);
                if (lastOpenCandidate < nowOpenMs) {
                    break;
                }
                repoRows.pop_back();
            }

            if (repoRows.empty()) {
                LOG_INFO(kLogCategory,
                         "LiveIngestor: catch-up batch discarded symbol=%s interval=%s start_open=%lld",
                         symbol.c_str(),
                         intervalLabel.c_str(),
                         static_cast<long long>(currentStartOpen));
                break;
            }

            const bool persisted = repo_.upsert_batch(symbol, intervalLabel, repoRows);
            if (!persisted) {
                LOG_WARN(kLogCategory,
                         "LiveIngestor: failed to persist catch-up batch symbol=%s interval=%s size=%zu",
                         symbol.c_str(),
                         intervalLabel.c_str(),
                         repoRows.size());
                break;
            }

            ttp::common::metrics::Registry::instance().incrementCounter(
                "rest_catchup_candles_total",
                static_cast<std::uint64_t>(repoRows.size()));

            const auto lastCloseMs = repoRows.back().closeTime;
            const auto lastOpenMs = domain::align_down_ms(lastCloseMs, intervalMs);
            record_last_closed_open_(symbol, lastOpenMs);
            totalPersisted += repoRows.size();

            const auto nextStartOpen = lastOpenMs + intervalMs;
            if (nextStartOpen <= currentStartOpen) {
                LOG_WARN(kLogCategory,
                         "LiveIngestor: catch-up stalled symbol=%s interval=%s current_start=%lld next_start=%lld",
                         symbol.c_str(),
                         intervalLabel.c_str(),
                         static_cast<long long>(currentStartOpen),
                         static_cast<long long>(nextStartOpen));
                break;
            }

            currentStartOpen = nextStartOpen;
        }

        LOG_INFO(kLogCategory,
                 "LiveIngestor: catch-up completed symbol=%s interval=%s persisted=%zu",
                 symbol.c_str(),
                 intervalLabel.c_str(),
                 totalPersisted);

        if (totalPersisted > 0) {
            resyncedSymbols.push_back(symbol);
        }
    }

    if (!resyncedSymbols.empty()) {
        std::ostringstream oss;
        oss << "{\"type\":\"resync_done\",\"interval\":\"" << intervalLabel << "\",\"symbols\":[";
        for (std::size_t i = 0; i < resyncedSymbols.size(); ++i) {
            if (i > 0) {
                oss << ',';
            }
            oss << "\"" << resyncedSymbols[i] << "\"";
        }
        oss << "]}";
        ttp::api::broadcast(oss.str());
    }
}

std::optional<std::int64_t> LiveIngestor::get_last_closed_open_ms_(const std::string& symbol,
                                                                   const std::string& intervalLabel,
                                                                   std::int64_t intervalMs) {
    {
        std::lock_guard<std::mutex> lock(lastClosedMutex_);
        const auto it = lastClosedOpenMs_.find(symbol);
        if (it != lastClosedOpenMs_.end()) {
            return it->second;
        }
    }

    try {
        auto maxTsOpt = repo_.max_timestamp(symbol, intervalLabel);
        if (!maxTsOpt.has_value()) {
            return std::nullopt;
        }

        const auto lastMs = normalize_to_milliseconds(*maxTsOpt);
        const auto openMs = domain::align_down_ms(lastMs, intervalMs);
        if (openMs <= 0) {
            return std::nullopt;
        }
        record_last_closed_open_(symbol, openMs);
        return openMs;
    }
    catch (const std::exception& ex) {
        LOG_WARN(kLogCategory,
                 "LiveIngestor: failed to refresh last candle symbol=%s interval=%s error=%s",
                 symbol.c_str(),
                 intervalLabel.c_str(),
                 ex.what());
    }
    catch (...) {
        LOG_WARN(kLogCategory,
                 "LiveIngestor: failed to refresh last candle symbol=%s interval=%s unknown error",
                 symbol.c_str(),
                 intervalLabel.c_str());
    }

    return std::nullopt;
}

void LiveIngestor::record_last_closed_open_(const std::string& symbol, std::int64_t openMs) {
    if (openMs <= 0) {
        return;
    }
    std::lock_guard<std::mutex> lock(lastClosedMutex_);
    lastClosedOpenMs_[symbol] = openMs;
}

void LiveIngestor::stop() {
    stopRequested_.store(true, std::memory_order_relaxed);
    try {
        ws_.set_on_reconnected(nullptr);
        ws_.stop();
    }
    catch (const std::exception& ex) {
        LOG_WARN(kLogCategory, "LiveIngestor: stop signalled but websocket stop failed error=%s", ex.what());
    }
    catch (...) {
        LOG_WARN(kLogCategory, "LiveIngestor: stop signalled but websocket stop failed with unknown error");
    }

    if (worker_.joinable()) {
        worker_.join();
    }
}

}  // namespace app

