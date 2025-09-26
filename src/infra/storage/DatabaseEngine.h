#pragma once

#include "config/Config.h"
#include "infra/tools/CryptoDataFetcher.h"
#include "infra/storage/PriceData.h"
#include "infra/storage/PriceDataManager.h"
#include "core/ICacheObserver.h"
#include "core/IFullDataObserver.h"
#include "core/ObserverSuscription.h"
#include "core/FullDataObserverSubscription.h"
#include "core/IPriceLimitObserver.h"

#include <atomic>
#include <deque>
#include <map>
#include <mutex>
#include <optional>
#include <ostream>
#include <shared_mutex>
#include <string>
#include <thread>
#include <unordered_set>
#include <utility>
#include <vector>

namespace infra::storage {

class DatabaseEngine {
public:
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

    void addObserver(core::ICacheObserver* observer, size_t index);
    void removeObserver(core::ICacheObserver* observer);
    void addFullDataObserver(core::IFullDataObserver* observer);
    void removeFullDataObserver(core::IFullDataObserver* observer);

    std::optional<long long> getLastEventTimestamp();
    std::optional<double> SelectOpenPrice(long long timestamp);
    std::optional<double> SelectClosePrice(long long timestamp);
    std::optional<double> SelectHighPrice(long long timestamp);
    std::optional<double> SelectLowPrice(long long timestamp);
    std::optional<std::pair<long long, long long>> getOpenTimeRange() const;
    bool isTimestampWithinRange(long long timestamp) const;

    void addPriceLimitObserver(core::IPriceLimitObserver* observer);
    void removePriceLimitObserver(core::IPriceLimitObserver* observer);

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

    std::vector<core::ObserverSubscription> observerSubscriptions;
    std::vector<core::FullDataObserverSubscription> fullDataObserverSubscriptions;
    std::vector<core::ICacheObserver*> observers;
    std::vector<core::IFullDataObserver*> fullDataObservers;
    std::vector<core::IPriceLimitObserver*> priceLimitObservers;

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
};

}  // namespace infra::storage
