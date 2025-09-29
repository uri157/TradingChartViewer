#pragma once

#include "config/Config.h"
#include "infra/tools/CryptoDataFetcher.h"
#include "infra/storage/PriceData.h"
#include "infra/storage/PriceDataManager.h"
#include "core/ICacheObserver.h"
#include "core/IFullDataObserver.h"
#include "core/IPriceLimitObserver.h"

#include <atomic>
#include <deque>
#include <map>
#include <mutex>
#include <optional>
#include <ostream>
#include <cstdint>
#include <shared_mutex>
#include <string>
#include <thread>
#include <unordered_set>
#include <utility>
#include <vector>

namespace infra::storage {

class DatabaseEngine {
public:
    struct ObserverHandle {
        DatabaseEngine* engine{nullptr};
        std::size_t id{0};
        bool alive{false};

        ObserverHandle() = default;
        ObserverHandle(DatabaseEngine* eng, std::size_t handleId)
            : engine(eng), id(handleId), alive(true) {}
        ObserverHandle(ObserverHandle&& other) noexcept { *this = std::move(other); }
        ObserverHandle& operator=(ObserverHandle&& other) noexcept {
            if (this != &other) {
                reset();
                engine = other.engine;
                id = other.id;
                alive = other.alive;
                other.engine = nullptr;
                other.id = 0;
                other.alive = false;
            }
            return *this;
        }
        ObserverHandle(const ObserverHandle&) = delete;
        ObserverHandle& operator=(const ObserverHandle&) = delete;
        ~ObserverHandle() { reset(); }

        void reset();
    };

    struct PriceLimitHandle {
        DatabaseEngine* engine{nullptr};
        std::size_t id{0};
        bool alive{false};

        PriceLimitHandle() = default;
        PriceLimitHandle(DatabaseEngine* eng, std::size_t handleId)
            : engine(eng), id(handleId), alive(true) {}
        PriceLimitHandle(PriceLimitHandle&& other) noexcept { *this = std::move(other); }
        PriceLimitHandle& operator=(PriceLimitHandle&& other) noexcept {
            if (this != &other) {
                reset();
                engine = other.engine;
                id = other.id;
                alive = other.alive;
                other.engine = nullptr;
                other.id = 0;
                other.alive = false;
            }
            return *this;
        }
        PriceLimitHandle(const PriceLimitHandle&) = delete;
        PriceLimitHandle& operator=(const PriceLimitHandle&) = delete;
        ~PriceLimitHandle() { reset(); }

        void reset();
    };

    struct OHLC {
        long long openTimeMs{0};
        double open{0.0};
        double high{0.0};
        double low{0.0};
        double close{0.0};
    };

    explicit DatabaseEngine(const config::Config& config);
    ~DatabaseEngine();

    static void setGlobalTracing(bool enabled);

    void warmupAsync();
    void actualize();

    void updateWithNewData(const infra::storage::PriceData& data);

    ObserverHandle addObserver(core::ICacheObserver* observer, std::uint64_t mask);
    void removeObserver(std::size_t id);
    void addFullDataObserver(core::IFullDataObserver* observer);
    void removeFullDataObserver(core::IFullDataObserver* observer);

    std::optional<long long> getLastEventTimestamp();
    std::optional<double> SelectOpenPrice(long long timestamp);
    std::optional<double> SelectClosePrice(long long timestamp);
    std::optional<double> SelectHighPrice(long long timestamp);
    std::optional<double> SelectLowPrice(long long timestamp);
    std::optional<std::pair<long long, long long>> getOpenTimeRange() const;
    bool isTimestampWithinRange(long long timestamp) const;

    PriceLimitHandle addPriceLimitObserver(core::IPriceLimitObserver* observer);
    void removePriceLimitObserver(std::size_t id);

    bool tryGetOHLC(long long openTimeMs, double& o, double& h, double& l, double& c) const;
    bool tryGetOHLCSpan(long long startMs, int count, std::vector<OHLC>& out) const;
    bool tryGetLatestSpan(int count, std::vector<OHLC>& out) const;

    bool isReady() const;
    bool traceEnabled() const;
    void traceWindowAround(long long referenceTimeMs, int span = 10) const;
    void printDiagnostics(std::ostream& os) const;

private:
    static constexpr int WARMUP_CANDLES = 200;

    void notifyObservers();
    void notifyFullDataObservers();
    void stopBackgroundSync();
    void ensureCacheSizeLocked();
    void loadHistoricalStore();
    void ingestRecords(const std::vector<infra::storage::PriceData>& records);
    void ingestRecord(const infra::storage::PriceData& record);
    std::vector<infra::storage::PriceData> normalizeRecords(const std::vector<infra::storage::PriceData>& records) const;
    infra::storage::PriceData normalizeRecord(const infra::storage::PriceData& record) const;
    bool updateReadinessLocked();
    bool hasConsecutiveCandlesLocked(int required, long long& lastTimestamp) const;
    void rebuildCacheFromHistorical();
    void logLookupMissLocked(long long timestamp,
                             std::map<long long, infra::storage::PriceData>::const_iterator lowerBound) const;
    static std::string formatTimestampUtc(long long timestampMs);

    infra::tools::CryptoDataFetcher fetcher;
    std::deque<infra::storage::PriceData> cache;
    const size_t cacheSize = 20;

    std::string currentSymbol;
    std::string interval;
    long long previousTimestamp = 0;

    PriceDataManager manager;

    double currentMaxPrice = std::numeric_limits<double>::lowest();
    double currentMinPrice = std::numeric_limits<double>::max();

    struct CacheObserverEntry {
        std::size_t id{0};
        core::ICacheObserver* observer{nullptr};
        std::uint64_t mask{0};
    };
    struct PriceLimitObserverEntry {
        std::size_t id{0};
        core::IPriceLimitObserver* observer{nullptr};
    };

    std::vector<CacheObserverEntry> cacheObservers_;
    std::vector<core::IFullDataObserver*> fullDataObservers;
    std::vector<PriceLimitObserverEntry> priceLimitObservers_;

    mutable std::mutex cacheMutex;
    mutable std::shared_mutex historicalMutex_;
    mutable std::mutex lookupMissMutex_;
    std::map<long long, infra::storage::PriceData> historicalData_;
    mutable std::unordered_set<long long> lookupMissLogged_;
    bool dataReady_{false};
    long long readinessAnchor_{0};
    bool readinessAnnounced_{false};
    bool traceEnabled_{false};
    static std::atomic<bool> globalTraceEnabled_;

    std::thread historicalThread;
    std::atomic<bool> warmupRequested{false};
    std::atomic<bool> stopRequested{false};
    std::atomic<long long> minOpenTime_{0};
    std::atomic<long long> maxOpenTime_{0};
    std::size_t nextObserverId_{1};
    std::size_t nextPriceLimitObserverId_{1};
};

}  // namespace infra::storage
