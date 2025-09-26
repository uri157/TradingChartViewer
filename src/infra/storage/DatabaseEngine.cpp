#include "infra/storage/DatabaseEngine.h"

#include "logging/Log.h"
#include "core/TimeUtils.h"

#include <filesystem>
#include <algorithm>
#include <chrono>
#include <thread>
#include <cstring>
#include <optional>
#include <limits>
#include <ctime>
#include <iomanip>
#include <sstream>

namespace infra::storage {

std::atomic<bool> DatabaseEngine::globalTraceEnabled_{false};

namespace {

constexpr long long kMinuteMs = core::TimeUtils::kMillisPerMinute;

DatabaseEngine::OHLC makeOHLC(const infra::storage::PriceData& data) {
    return DatabaseEngine::OHLC{ data.openTime, data.openPrice, data.highPrice, data.lowPrice, data.closePrice };
}

std::string buildDataPath(const config::Config& cfg) {
    std::filesystem::path base(cfg.dataDir.empty() ? std::filesystem::path{"."} : std::filesystem::path{cfg.dataDir});
    std::string fileName = cfg.symbol + "_" + cfg.interval + ".bin";
    base /= fileName;
    return base.string();
}

}  // namespace

DatabaseEngine::DatabaseEngine(const config::Config& config)
    : fetcher(),
      currentSymbol(config.symbol),
      interval(config.interval),
      manager(buildDataPath(config)) {
    fetcher.setRestHost(config.restHost);
    traceEnabled_ = globalTraceEnabled_.load(std::memory_order_relaxed);
    if (!traceEnabled_) {
        const char* debugEnv = std::getenv("TTP_DEBUG");
        traceEnabled_ = debugEnv && std::strcmp(debugEnv, "0") != 0;
    }

    loadHistoricalStore();

    infra::storage::PriceData lastRecord = manager.readLastRecord();
    previousTimestamp = domain::floorToMinuteMs(lastRecord.openTime);
}

DatabaseEngine::~DatabaseEngine() {
    stopBackgroundSync();
}

void DatabaseEngine::setGlobalTracing(bool enabled) {
    globalTraceEnabled_.store(enabled, std::memory_order_relaxed);
}

void DatabaseEngine::stopBackgroundSync() {
    stopRequested.store(true);
    if (historicalThread.joinable()) {
        historicalThread.join();
    }
    warmupRequested.store(false);
}

infra::storage::PriceData DatabaseEngine::normalizeRecord(const infra::storage::PriceData& record) const {
    infra::storage::PriceData normalized = record;
    const auto alignedOpen = domain::floorToMinuteMs(record.openTime);
    if (alignedOpen <= 0) {
        normalized.openTime = 0;
        normalized.closeTime = 0;
        return normalized;
    }
    normalized.openTime = alignedOpen;
    normalized.closeTime = alignedOpen + kMinuteMs - 1;
    return normalized;
}

std::vector<infra::storage::PriceData> DatabaseEngine::normalizeRecords(const std::vector<infra::storage::PriceData>& records) const {
    std::map<long long, infra::storage::PriceData> deduplicated;
    for (const auto& record : records) {
        infra::storage::PriceData normalized = normalizeRecord(record);
        if (!manager.isValidRecord(normalized)) {
            continue;
        }
        deduplicated[normalized.openTime] = normalized;
    }

    std::vector<infra::storage::PriceData> result;
    result.reserve(deduplicated.size());
    for (auto& entry : deduplicated) {
        result.push_back(entry.second);
    }
    return result;
}

void DatabaseEngine::loadHistoricalStore() {
    auto records = manager.readAllRecords();
    ingestRecords(records);
    rebuildCacheFromHistorical();
    previousTimestamp = maxOpenTime_.load(std::memory_order_relaxed);
    if (traceEnabled_) {
        std::shared_lock<std::shared_mutex> lock(historicalMutex_);
        if (!historicalData_.empty()) {
            LOG_TRACE(logging::LogCategory::DB,
                      "Historical store loaded: count=%zu first=%s last=%s",
                      static_cast<std::size_t>(historicalData_.size()),
                      formatTimestampUtc(historicalData_.begin()->first).c_str(),
                      formatTimestampUtc(historicalData_.rbegin()->first).c_str());
        }
        else {
            LOG_TRACE(logging::LogCategory::DB, "Historical store empty");
        }
    }
}

void DatabaseEngine::ingestRecords(const std::vector<infra::storage::PriceData>& records) {
    auto normalized = normalizeRecords(records);
    if (normalized.empty()) {
        return;
    }

    std::string readinessMessage;
    {
        std::unique_lock<std::shared_mutex> lock(historicalMutex_);
        bool readyBefore = dataReady_;
        for (const auto& record : normalized) {
            historicalData_[record.openTime] = record;
        }
        if (!historicalData_.empty()) {
            minOpenTime_.store(historicalData_.begin()->first, std::memory_order_relaxed);
            maxOpenTime_.store(historicalData_.rbegin()->first, std::memory_order_relaxed);
        }

        long long anchorTimestamp = 0;
        const bool hasWindow = hasConsecutiveCandlesLocked(WARMUP_CANDLES, anchorTimestamp);
        dataReady_ = hasWindow;
        if (hasWindow) {
            readinessAnchor_ = anchorTimestamp;
        }
        else {
            readinessAnchor_ = 0;
            readinessAnnounced_ = false;
        }

        const bool becameReady = (!readyBefore && dataReady_);
        if (becameReady && !readinessAnnounced_) {
            readinessAnnounced_ = true;
            readinessMessage = "DATA READY: " + std::to_string(WARMUP_CANDLES) +
                " consecutive candles ending at " + std::to_string(readinessAnchor_) +
                " (" + formatTimestampUtc(readinessAnchor_) + ")";
        }
    }

    if (!readinessMessage.empty()) {
        LOG_INFO(logging::LogCategory::DATA, "%s", readinessMessage.c_str());
    }

    if (traceEnabled_ && !normalized.empty()) {
        const auto firstTs = normalized.front().openTime;
        const auto lastTs = normalized.back().openTime;
        LOG_TRACE(logging::LogCategory::DB,
                  "Ingested records count=%zu span=%s -> %s",
                  static_cast<std::size_t>(normalized.size()),
                  formatTimestampUtc(firstTs).c_str(),
                  formatTimestampUtc(lastTs).c_str());
    }
}

void DatabaseEngine::ingestRecord(const infra::storage::PriceData& record) {
    ingestRecords(std::vector<infra::storage::PriceData>{ record });
}

bool DatabaseEngine::hasConsecutiveCandlesLocked(int required, long long& lastTimestamp) const {
    if (required <= 0) {
        return true;
    }
    if (historicalData_.empty()) {
        return false;
    }

    int count = 0;
    auto it = historicalData_.rbegin();
    long long expected = domain::floorToMinuteMs(it->first);
    lastTimestamp = it->first;
    for (; it != historicalData_.rend() && count < required; ++it) {
        if (it->first != expected) {
            return false;
        }
        ++count;
        expected -= kMinuteMs;
    }
    return count >= required;
}

void DatabaseEngine::rebuildCacheFromHistorical() {
    std::vector<infra::storage::PriceData> latest;
    {
        std::shared_lock<std::shared_mutex> lock(historicalMutex_);
        latest.reserve(std::min<std::size_t>(cacheSize, historicalData_.size()));
        auto it = historicalData_.rbegin();
        for (std::size_t i = 0; i < cacheSize && it != historicalData_.rend(); ++i, ++it) {
            latest.push_back(it->second);
        }
    }

    double maxPrice = std::numeric_limits<double>::lowest();
    double minPrice = std::numeric_limits<double>::max();
    for (const auto& record : latest) {
        maxPrice = std::max(maxPrice, record.highPrice);
        minPrice = std::min(minPrice, record.lowPrice);
    }

    std::vector<core::IPriceLimitObserver*> priceObserversCopy;
    std::optional<double> maxChanged;
    std::optional<double> minChanged;

    {
        std::lock_guard<std::mutex> lock(cacheMutex);
        priceObserversCopy = priceLimitObservers;
        cache.clear();
        for (const auto& record : latest) {
            cache.push_back(record);
        }
        if (!latest.empty()) {
            previousTimestamp = latest.front().openTime;
            if (maxPrice != currentMaxPrice) {
                currentMaxPrice = maxPrice;
                maxChanged = currentMaxPrice;
            }
            if (minPrice != currentMinPrice) {
                currentMinPrice = minPrice;
                minChanged = currentMinPrice;
            }
        }
    }

    if (maxChanged) {
        for (auto* observer : priceObserversCopy) {
            observer->onMaxPriceLimitChanged(*maxChanged);
        }
    }
    if (minChanged) {
        for (auto* observer : priceObserversCopy) {
            observer->onMinPriceLimitChanged(*minChanged);
        }
    }
}

void DatabaseEngine::warmupAsync() {
    bool expected = false;
    if (!warmupRequested.compare_exchange_strong(expected, true)) {
        return;
    }

    stopRequested.store(false);
    if (historicalThread.joinable()) {
        historicalThread.join();
    }

    historicalThread = std::thread([this]() {
        actualize();
        warmupRequested.store(false);
    });
}

void DatabaseEngine::ensureCacheSizeLocked() {
    while (cache.size() > cacheSize) {
        cache.pop_back();
    }
}

void DatabaseEngine::actualize() {
    bool finished = false;
    while (!finished && !stopRequested.load()) {
        std::vector<infra::storage::PriceData> fetched = fetcher.fetchHistoricalData(currentSymbol, interval, previousTimestamp);
        auto normalized = normalizeRecords(fetched);
        if (!normalized.empty()) {
            manager.saveRecords(normalized);
            {
                std::lock_guard<std::mutex> lock(cacheMutex);
                previousTimestamp = normalized.back().openTime;
            }
            ingestRecords(normalized);
            rebuildCacheFromHistorical();
            if (normalized.size() < 1000) {
                finished = true;
            }
        }
        else if (fetched.empty()) {
            finished = true;
        }

        if (!finished && !stopRequested.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }
    }

    if (stopRequested.load()) {
        return;
    }

    rebuildCacheFromHistorical();
    notifyObservers();
    notifyFullDataObservers();
}

void DatabaseEngine::updateWithNewData(const infra::storage::PriceData& data) {
    infra::storage::PriceData normalized = normalizeRecord(data);
    if (!manager.isValidRecord(normalized)) {
        return;
    }

    std::optional<infra::storage::PriceData> closedCandle;
    std::optional<double> maxChanged;
    std::optional<double> minChanged;
    bool notifyCache = false;
    bool notifyFull = false;
    std::vector<core::IPriceLimitObserver*> priceObserversCopy;

    {
        std::lock_guard<std::mutex> lock(cacheMutex);
        priceObserversCopy = priceLimitObservers;

        if (!cache.empty()) {
            if (normalized.openTime > cache.front().openTime) {
                closedCandle = cache.front();
                cache.push_front(normalized);
                ensureCacheSizeLocked();
                previousTimestamp = normalized.openTime;
                notifyCache = true;
                notifyFull = true;
                if (normalized.highPrice > currentMaxPrice) {
                    currentMaxPrice = normalized.highPrice;
                    maxChanged = currentMaxPrice;
                }
                if (normalized.lowPrice < currentMinPrice) {
                    currentMinPrice = normalized.lowPrice;
                    minChanged = currentMinPrice;
                }
            }
            else if (normalized.openTime == cache.front().openTime) {
                cache.front() = normalized;
                notifyFull = true;
                if (normalized.highPrice > currentMaxPrice) {
                    currentMaxPrice = normalized.highPrice;
                    maxChanged = currentMaxPrice;
                }
                if (normalized.lowPrice < currentMinPrice) {
                    currentMinPrice = normalized.lowPrice;
                    minChanged = currentMinPrice;
                }
            }
            else {
                LOG_DEBUG(logging::LogCategory::DATA,
                          "Ignoring stale market data with timestamp %lld",
                          static_cast<long long>(normalized.openTime));
                return;
            }
        }
        else {
            cache.push_front(normalized);
            previousTimestamp = normalized.openTime;
            currentMaxPrice = normalized.highPrice;
            currentMinPrice = normalized.lowPrice;
            notifyCache = true;
            notifyFull = true;
            maxChanged = currentMaxPrice;
            minChanged = currentMinPrice;
        }
    }

    if (closedCandle) {
        manager.saveRecord(*closedCandle);
    }

    ingestRecord(normalized);

    if (maxChanged) {
        for (auto* observer : priceObserversCopy) {
            observer->onMaxPriceLimitChanged(*maxChanged);
        }
    }
    if (minChanged) {
        for (auto* observer : priceObserversCopy) {
            observer->onMinPriceLimitChanged(*minChanged);
        }
    }

    if (notifyCache) {
        notifyObservers();
    }
    if (notifyFull) {
        notifyFullDataObservers();
    }
}

void DatabaseEngine::addObserver(core::ICacheObserver* observer, size_t index) {
    std::lock_guard<std::mutex> lock(cacheMutex);
    observerSubscriptions.push_back({ observer, index });
}

void DatabaseEngine::removeObserver(core::ICacheObserver* observer) {
    std::lock_guard<std::mutex> lock(cacheMutex);
    observerSubscriptions.erase(std::remove_if(observerSubscriptions.begin(), observerSubscriptions.end(),
        [observer](const core::ObserverSubscription& subscription) {
            return subscription.observer == observer;
        }), observerSubscriptions.end());
}

void DatabaseEngine::addFullDataObserver(core::IFullDataObserver* observer) {
    {
        std::lock_guard<std::mutex> lock(cacheMutex);
        fullDataObservers.push_back(observer);
    }
    notifyFullDataObservers();
}

void DatabaseEngine::removeFullDataObserver(core::IFullDataObserver* observer) {
    std::lock_guard<std::mutex> lock(cacheMutex);
    fullDataObservers.erase(std::remove(fullDataObservers.begin(), fullDataObservers.end(), observer),
        fullDataObservers.end());
}

void DatabaseEngine::addPriceLimitObserver(core::IPriceLimitObserver* observer) {
    std::optional<double> maxValue;
    std::optional<double> minValue;
    {
        std::lock_guard<std::mutex> lock(cacheMutex);
        priceLimitObservers.push_back(observer);
        if (currentMaxPrice > std::numeric_limits<double>::lowest()) {
            maxValue = currentMaxPrice;
        }
        if (currentMinPrice < std::numeric_limits<double>::max()) {
            minValue = currentMinPrice;
        }
    }
    if (maxValue) {
        observer->onMaxPriceLimitChanged(*maxValue);
    }
    if (minValue) {
        observer->onMinPriceLimitChanged(*minValue);
    }
}

void DatabaseEngine::removePriceLimitObserver(core::IPriceLimitObserver* observer) {
    std::lock_guard<std::mutex> lock(cacheMutex);
    priceLimitObservers.erase(std::remove(priceLimitObservers.begin(), priceLimitObservers.end(), observer),
        priceLimitObservers.end());
}

void DatabaseEngine::notifyObservers() {
    std::vector<std::pair<core::ICacheObserver*, infra::storage::PriceData>> notifications;
    {
        std::lock_guard<std::mutex> lock(cacheMutex);
        for (const auto& subscription : observerSubscriptions) {
            if (subscription.index < cache.size()) {
                notifications.emplace_back(subscription.observer, cache[subscription.index]);
            }
        }
    }
    for (auto& entry : notifications) {
        entry.first->onCacheUpdated(entry.second);
    }
}

void DatabaseEngine::notifyFullDataObservers() {
    std::vector<core::IFullDataObserver*> observersCopy;
    std::optional<infra::storage::PriceData> recentData;
    {
        std::lock_guard<std::mutex> lock(cacheMutex);
        if (!cache.empty()) {
            recentData = cache.front();
            observersCopy = fullDataObservers;
        }
    }
    if (recentData) {
        for (core::IFullDataObserver* observer : observersCopy) {
            observer->onFullDataUpdated(*recentData);
        }
    }
}

std::optional<long long> DatabaseEngine::getLastEventTimestamp() {
    const long long maxValue = maxOpenTime_.load(std::memory_order_relaxed);
    if (maxValue > 0) {
        return maxValue;
    }

    std::lock_guard<std::mutex> lock(cacheMutex);
    if (!cache.empty()) {
        return cache.front().openTime;
    }

    infra::storage::PriceData lastRecord = manager.readLastRecord();
    if (lastRecord.openTime > 0) {
        return lastRecord.openTime;
    }
    return std::nullopt;
}

std::optional<double> DatabaseEngine::SelectOpenPrice(long long timestamp) {
    double o, h, l, c;
    if (tryGetOHLC(timestamp, o, h, l, c)) {
        return o;
    }
    return std::nullopt;
}

std::optional<double> DatabaseEngine::SelectClosePrice(long long timestamp) {
    double o, h, l, c;
    if (tryGetOHLC(timestamp, o, h, l, c)) {
        return c;
    }
    return std::nullopt;
}

std::optional<double> DatabaseEngine::SelectHighPrice(long long timestamp) {
    double o, h, l, c;
    if (tryGetOHLC(timestamp, o, h, l, c)) {
        return h;
    }
    return std::nullopt;
}

std::optional<double> DatabaseEngine::SelectLowPrice(long long timestamp) {
    double o, h, l, c;
    if (tryGetOHLC(timestamp, o, h, l, c)) {
        return l;
    }
    return std::nullopt;
}

std::optional<std::pair<long long, long long>> DatabaseEngine::getOpenTimeRange() const {
    const long long minValue = minOpenTime_.load(std::memory_order_relaxed);
    const long long maxValue = maxOpenTime_.load(std::memory_order_relaxed);
    if (minValue <= 0 || maxValue <= 0 || maxValue < minValue) {
        return std::nullopt;
    }
    return std::make_pair(minValue, maxValue);
}

bool DatabaseEngine::isTimestampWithinRange(long long timestamp) const {
    const long long minValue = minOpenTime_.load(std::memory_order_relaxed);
    const long long maxValue = maxOpenTime_.load(std::memory_order_relaxed);
    if (minValue <= 0 || maxValue <= 0 || maxValue < minValue) {
        return false;
    }
    const auto aligned = domain::floorToMinuteMs(timestamp);
    return aligned >= minValue && aligned <= maxValue;
}

std::string DatabaseEngine::formatTimestampUtc(long long timestampMs) {
    if (timestampMs <= 0) {
        return "n/a";
    }

    std::time_t seconds = static_cast<std::time_t>(timestampMs / core::TimeUtils::kMillisPerSecond);
    std::tm utc{};
#if defined(_WIN32)
    gmtime_s(&utc, &seconds);
#else
    gmtime_r(&seconds, &utc);
#endif
    std::ostringstream oss;
    oss << std::put_time(&utc, "%Y-%m-%d %H:%MZ");
    return oss.str();
}

void DatabaseEngine::logLookupMissLocked(long long timestamp,
    std::map<long long, infra::storage::PriceData>::const_iterator lowerBound) const {
    if (!traceEnabled_) {
        return;
    }

    {
        std::lock_guard<std::mutex> guard(lookupMissMutex_);
        if (!lookupMissLogged_.insert(timestamp).second) {
            return;
        }
    }

    std::string prevKey = "n/a";
    std::string nextKey = "n/a";

    if (!historicalData_.empty()) {
        auto prevIt = historicalData_.end();
        if (lowerBound == historicalData_.end()) {
            prevIt = std::prev(historicalData_.end());
        }
        else if (lowerBound->first > timestamp) {
            if (lowerBound != historicalData_.begin()) {
                prevIt = std::prev(lowerBound);
            }
        }
        else if (lowerBound->first == timestamp) {
            if (lowerBound != historicalData_.begin()) {
                prevIt = std::prev(lowerBound);
            }
        }

        if (prevIt != historicalData_.end()) {
            prevKey = formatTimestampUtc(prevIt->first) + " (" + std::to_string(prevIt->first) + ")";
        }

        auto nextIt = lowerBound;
        if (nextIt != historicalData_.end() && nextIt->first == timestamp) {
            ++nextIt;
        }
        if (nextIt != historicalData_.end()) {
            nextKey = formatTimestampUtc(nextIt->first) + " (" + std::to_string(nextIt->first) + ")";
        }
    }

    LOG_DEBUG(logging::LogCategory::DB,
              "LOOKUP MISS for %s (%lld) — nearest keys: prev=%s next=%s",
              formatTimestampUtc(timestamp).c_str(),
              static_cast<long long>(timestamp),
              prevKey.c_str(),
              nextKey.c_str());
}

bool DatabaseEngine::tryGetOHLC(long long openTimeMs, double& o, double& h, double& l, double& c) const {
    const auto aligned = domain::floorToMinuteMs(openTimeMs);
    std::shared_lock<std::shared_mutex> lock(historicalMutex_);
    if (historicalData_.empty()) {
        return false;
    }

    auto it = historicalData_.find(aligned);
    if (it == historicalData_.end()) {
        auto lower = historicalData_.lower_bound(aligned);
        logLookupMissLocked(aligned, lower);
        return false;
    }

    o = it->second.openPrice;
    h = it->second.highPrice;
    l = it->second.lowPrice;
    c = it->second.closePrice;
    return true;
}

bool DatabaseEngine::tryGetOHLCSpan(long long startMs, int count, std::vector<OHLC>& out) const {
    out.clear();
    if (count <= 0) {
        return false;
    }

    const auto alignedStart = domain::floorToMinuteMs(startMs);
    std::shared_lock<std::shared_mutex> lock(historicalMutex_);
    if (historicalData_.empty()) {
        return false;
    }

    auto it = historicalData_.find(alignedStart);
    if (it == historicalData_.end()) {
        auto lower = historicalData_.lower_bound(alignedStart);
        logLookupMissLocked(alignedStart, lower);
        return false;
    }

    out.reserve(static_cast<std::size_t>(count));
    auto iter = it;
    long long expected = alignedStart;
    for (int i = 0; i < count; ++i) {
        if (iter == historicalData_.end() || iter->first != expected) {
            auto lower = historicalData_.lower_bound(expected);
            logLookupMissLocked(expected, lower);
            out.clear();
            return false;
        }
        out.push_back(makeOHLC(iter->second));
        ++iter;
        expected += kMinuteMs;
    }
    return true;
}

bool DatabaseEngine::tryGetLatestSpan(int count, std::vector<OHLC>& out) const {
    out.clear();
    if (count <= 0) {
        return false;
    }

    std::shared_lock<std::shared_mutex> lock(historicalMutex_);
    if (historicalData_.empty()) {
        return false;
    }

    out.reserve(static_cast<std::size_t>(count));
    auto it = historicalData_.rbegin();
    long long expected = domain::floorToMinuteMs(it->first);
    for (int i = 0; i < count; ++i) {
        if (it == historicalData_.rend() || it->first != expected) {
            return false;
        }
        out.push_back(makeOHLC(it->second));
        ++it;
        expected -= kMinuteMs;
    }
    std::reverse(out.begin(), out.end());
    return true;
}

bool DatabaseEngine::isReady() const {
    std::shared_lock<std::shared_mutex> lock(historicalMutex_);
    return dataReady_;
}

bool DatabaseEngine::traceEnabled() const {
    return traceEnabled_;
}

void DatabaseEngine::traceWindowAround(long long referenceTimeMs, int span) const {
    if (!traceEnabled_) {
        return;
    }
    if (span <= 0) {
        span = 10;
    }

    const auto alignedRef = domain::floorToMinuteMs(referenceTimeMs);
    const int candles = std::max(span, 1);
    const int half = candles / 2;
    const long long start = alignedRef - static_cast<long long>(half) * kMinuteMs;

    std::vector<OHLC> window;
    if (!tryGetOHLCSpan(start, candles, window)) {
        return;
    }

    LOG_TRACE(logging::LogCategory::DB,
              "Window around %s (%lld)",
              formatTimestampUtc(alignedRef).c_str(),
              static_cast<long long>(alignedRef));
    for (const auto& candle : window) {
        LOG_TRACE(logging::LogCategory::DB,
                  "  Candle %s open=%.6f high=%.6f low=%.6f close=%.6f",
                  formatTimestampUtc(candle.openTimeMs).c_str(),
                  candle.open,
                  candle.high,
                  candle.low,
                  candle.close);
    }
}

void DatabaseEngine::printDiagnostics(std::ostream& os) const {
    std::vector<OHLC> lastCandles;
    bool readySnapshot = false;
    long long anchorSnapshot = 0;
    {
        std::shared_lock<std::shared_mutex> lock(historicalMutex_);
        os << "CANDLES: " << historicalData_.size() << '\n';
        if (!historicalData_.empty()) {
            const auto firstIt = historicalData_.begin();
            const auto lastIt = historicalData_.rbegin();
            const auto firstOHLC = makeOHLC(firstIt->second);
            const auto lastOHLC = makeOHLC(lastIt->second);
            os << "FIRST: " << formatTimestampUtc(firstIt->first)
               << " (" << firstIt->first << ")"
               << " open=" << firstOHLC.open
               << " close=" << firstOHLC.close << '\n';
            os << "LAST:  " << formatTimestampUtc(lastIt->first)
               << " (" << lastIt->first << ")"
               << " open=" << lastOHLC.open
               << " close=" << lastOHLC.close << '\n';
        }
        const std::size_t toCollect = std::min<std::size_t>(10, historicalData_.size());
        auto it = historicalData_.rbegin();
        for (std::size_t i = 0; i < toCollect && it != historicalData_.rend(); ++i, ++it) {
            lastCandles.push_back(makeOHLC(it->second));
        }
        readySnapshot = dataReady_;
        anchorSnapshot = readinessAnchor_;
    }

    std::reverse(lastCandles.begin(), lastCandles.end());
    os << "LAST CANDLES:" << '\n';
    for (const auto& candle : lastCandles) {
        os << "  Candle " << formatTimestampUtc(candle.openTimeMs) << " (" << candle.openTimeMs
           << ")  open=" << candle.open
           << "  high=" << candle.high
           << "  low=" << candle.low
           << "  close=" << candle.close << '\n';
    }

    if (readySnapshot) {
        os << "READINESS: OK (>= " << WARMUP_CANDLES << " consecutive candles ending at "
           << anchorSnapshot << ")" << '\n';
    }
    else {
        os << "READINESS: MISSING consecutive window of " << WARMUP_CANDLES << " candles" << '\n';
    }
}

}  // namespace infra::storage
