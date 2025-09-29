#pragma once

#include <atomic>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>
#include <functional>

#include "domain/exchange/IExchangeKlines.hpp"

namespace adapters::duckdb {
class DuckCandleRepo;
}

namespace app {

class LiveIngestor {
public:
    LiveIngestor(adapters::duckdb::DuckCandleRepo& repo,
                 domain::IExchangeKlines& rest,
                 domain::IExchangeLiveKlines& ws);

    ~LiveIngestor();

    void run(const std::vector<std::string>& symbols, domain::Interval interval);

    void stop();

private:
    struct LiveKey {
        std::string symbol;
        domain::Interval interval;
        std::int64_t openMs{0};

        bool operator==(const LiveKey& other) const noexcept {
            return openMs == other.openMs && interval.ms == other.interval.ms && symbol == other.symbol;
        }
    };

    struct LiveKeyHash {
        std::size_t operator()(const LiveKey& key) const noexcept {
            std::size_t seed = std::hash<std::string>{}(key.symbol);
            const std::size_t intervalHash = std::hash<std::int64_t>{}(key.interval.ms);
            seed ^= intervalHash + 0x9e3779b9 + (seed << 6) + (seed >> 2);
            const std::size_t openHash = std::hash<std::int64_t>{}(key.openMs);
            seed ^= openHash + 0x9e3779b9 + (seed << 6) + (seed >> 2);
            return seed;
        }
    };

    void run_worker_(std::vector<std::string> symbols, domain::Interval interval);
    void catch_up_(const std::vector<std::string>& symbols,
                   const std::string& intervalLabel,
                   domain::Interval interval,
                   std::int64_t intervalMs);
    std::optional<std::int64_t> get_last_closed_open_ms_(const std::string& symbol,
                                                         const std::string& intervalLabel,
                                                         std::int64_t intervalMs);
    void record_last_closed_open_(const std::string& symbol, std::int64_t openMs);

    adapters::duckdb::DuckCandleRepo& repo_;
    domain::IExchangeKlines& rest_;
    domain::IExchangeLiveKlines& ws_;
    std::atomic<bool> stopRequested_{false};
    std::thread worker_;
    std::mutex lastClosedMutex_;
    std::unordered_map<std::string, std::int64_t> lastClosedOpenMs_;
    std::mutex liveMutex_;
    std::unordered_map<LiveKey, domain::Candle, LiveKeyHash> liveCandles_;
    std::unordered_map<LiveKey, std::int64_t, LiveKeyHash> lastBroadcastMs_;
    std::int64_t partialThrottleMs_{0};
    bool emitPartials_{true};
};

}  // namespace app

