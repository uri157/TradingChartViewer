#include "ui/GridJapaneseCandle.h"

#include "logging/Log.h"

#include "bootstrap/DIContainer.h"
#include "core/TimeUtils.h"
#include "infra/storage/DatabaseEngine.h"
#include "ui/GridTime.h"
#include "ui/GridValues.h"
#include "ui/JapaneseCandleService.h"
#include "ui/RenderManager.h"
#include "ui/ResourceProvider.h"

#include <algorithm>

namespace ui {

long long GridJapaneseCandle::alignToMinuteCeil(long long timestampMs) {
    if (timestampMs <= 0) {
        return 0;
    }
    const auto aligned = domain::alignToMinute(timestampMs);
    if (aligned < timestampMs) {
        return aligned + core::TimeUtils::kMillisPerMinute;
    }
    return aligned;
}

std::size_t GridJapaneseCandle::targetCandleCount() const {
    return static_cast<std::size_t>(range) + 1;
}

GridJapaneseCandle::GridJapaneseCandle(int scopeId)

    : gridTime(bootstrap::DIContainer::resolve<ui::GridTime>("GridTime", scopeId)),
      gridValues(bootstrap::DIContainer::resolve<ui::GridValues>("GridValues", scopeId)),
      db(bootstrap::DIContainer::resolve<infra::storage::DatabaseEngine>("DatabaseEngine")),
      renderManager_(bootstrap::DIContainer::resolve<ui::RenderManager>("RenderManager")),
      resourceProvider_(bootstrap::DIContainer::resolve<ui::ResourceProvider>("ResourceProvider")),
      lastUpdate(std::chrono::steady_clock::now()),
      scopeId(scopeId) {
    if (!db) {
        LOG_ERROR(logging::LogCategory::UI, "GridJapaneseCandle initialization failed: DatabaseEngine unavailable.");
        return;
    }

    if (resourceProvider_) {
        auto fontRes = resourceProvider_->getFontResource("ui");
        if (fontRes.ready && fontRes.font) {
            loadingText_.setFont(*fontRes.font);
            loadingText_.setFillColor(sf::Color(200, 205, 220));
            loadingText_.setCharacterSize(32);
            loadingTextReady_ = true;
        }
    }
    loadingText_.setString("Loading historical data…");
    loadingText_.setPosition(80.f, 80.f);

    if (!rebuildWindow(true) && !loggedNoData_) {
        LOG_WARN(logging::LogCategory::UI, "GridJapaneseCandle not ready: no historical data available yet.");
        loggedNoData_ = true;
    }
}

GridJapaneseCandle::~GridJapaneseCandle() = default;

bool GridJapaneseCandle::refreshDataRange() {
    if (!db) {
        return false;
    }
    auto rangeOpt = db->getOpenTimeRange();
    if (!rangeOpt) {
        minOpenTime_ = 0;
        maxOpenTime_ = 0;
        return false;
    }
    minOpenTime_ = rangeOpt->first;
    maxOpenTime_ = rangeOpt->second;
    return maxOpenTime_ > 0 && maxOpenTime_ >= minOpenTime_;
}

bool GridJapaneseCandle::rebuildWindow(bool logInfo) {
    if (!db) {
        return false;
    }

    if (!db->isReady()) {
        ready_ = false;
        if (!loggedNoData_) {
            LOG_INFO(logging::LogCategory::SNAPSHOT, "GridJapaneseCandle waiting for historical data…");
            loggedNoData_ = true;
        }
        return false;
    }

    if (!refreshDataRange()) {
        candles.clear();
        highestCandle.reset();
        lowestCandle.reset();
        ready_ = false;
        loggedNoData_ = true;
        return false;
    }

    bool shouldLogRange = logInfo && (minOpenTime_ != lastLoggedMin_ || maxOpenTime_ != lastLoggedMax_);
    if (shouldLogRange) {
        LOG_INFO(logging::LogCategory::DB,
                 "Range min=%lld max=%lld",
                 static_cast<long long>(minOpenTime_),
                 static_cast<long long>(maxOpenTime_));
        lastLoggedMin_ = minOpenTime_;
        lastLoggedMax_ = maxOpenTime_;
    }

    const auto highestAligned = domain::alignToMinute(maxOpenTime_);
    if (highestAligned <= 0) {
        candles.clear();
        highestCandle.reset();
        lowestCandle.reset();
        ready_ = false;
        return false;
    }

    lastKnownMaxTimestamp_ = highestAligned;
    return rebuildWindowFromHighest(lastKnownMaxTimestamp_, logInfo);
}

bool GridJapaneseCandle::rebuildWindowFromHighest(long long highestTimestampMs, bool logInfo) {
    candles.clear();
    highestCandle.reset();
    lowestCandle.reset();
    highestMax = -FLT_MAX;
    lowestMin = FLT_MAX;

    if (!db) {
        ready_ = false;
        return false;
    }

    const long long maxAligned = domain::alignToMinute(maxOpenTime_);
    const long long minAligned = alignToMinuteCeil(minOpenTime_);
    if (maxAligned > 0 && highestTimestampMs > maxAligned) {
        highestTimestampMs = maxAligned;
    }
    if (minAligned > 0 && highestTimestampMs < minAligned) {
        highestTimestampMs = minAligned;
    }

    const auto desiredCount = targetCandleCount();
    const long long startTimestamp = highestTimestampMs -
        static_cast<long long>(desiredCount - 1) * core::TimeUtils::kMillisPerMinute;

    std::vector<infra::storage::DatabaseEngine::OHLC> span;
    if (!db->tryGetOHLCSpan(startTimestamp, static_cast<int>(desiredCount), span)) {
        if (!db->tryGetLatestSpan(static_cast<int>(desiredCount), span)) {
            ready_ = false;
            return false;
        }
        highestTimestampMs = span.back().openTimeMs;
    }

    std::vector<std::shared_ptr<ui::JapaneseCandleService>> newCandles;
    newCandles.reserve(span.size());

    for (auto it = span.rbegin(); it != span.rend(); ++it) {
        auto service = std::make_shared<ui::JapaneseCandleService>(scopeId, core::Timestamp(it->openTimeMs));
        service->setOHLC(*it);
        newCandles.push_back(service);

        if (auto maxPrice = service->getMaxPrice()) {
            highestMax = std::max(highestMax, static_cast<float>(*maxPrice));
        }
        if (auto minPrice = service->getMinPrice()) {
            lowestMin = std::min(lowestMin, static_cast<float>(*minPrice));
        }
    }

    if (newCandles.empty()) {
        ready_ = false;
        candles.clear();
        highestCandle.reset();
        lowestCandle.reset();
        return false;
    }

    candles = std::move(newCandles);
    highestCandle = candles.front();
    lowestCandle = candles.back();
    HighestTime = core::Timestamp(span.back().openTimeMs);
    LowestTime = core::Timestamp(span.front().openTimeMs);
    lastKnownMaxTimestamp_ = span.back().openTimeMs;
    ready_ = true;
    loggedNoData_ = false;

    if (highestMax == -FLT_MAX) {
        highestMax = 0.0f;
    }
    if (lowestMin == FLT_MAX) {
        lowestMin = 0.0f;
    }

    if (logInfo) {
        LOG_INFO(logging::LogCategory::RENDER,
                 "desired_candles=%zu highest=%lld lowest=%lld",
                 static_cast<std::size_t>(desiredCount),
                 static_cast<long long>(HighestTime.getTimestamp()),
                 static_cast<long long>(LowestTime.getTimestamp()));
        if (db->traceEnabled()) {
            db->traceWindowAround(HighestTime.getTimestamp());
        }
    }

    return true;
}

void GridJapaneseCandle::actualize() {
    if (!ready_ || candles.empty()) {
        return;
    }
    for (const auto& candle : candles) {
        candle->actualize();
    }
}

void GridJapaneseCandle::displacement() {
    if (!db || !db->isReady()) {
        ready_ = false;
        return;
    }

    if (!ready_ || candles.empty()) {
        return;
    }

    refreshDataRange();
    const long long maxAligned = domain::alignToMinute(maxOpenTime_);
    const long long minAligned = alignToMinuteCeil(minOpenTime_);
    long long highestTimestampMs = HighestTime.getTimestamp();
    if (maxAligned > 0 && highestTimestampMs > maxAligned) {
        highestTimestampMs = maxAligned;
    }
    if (minAligned > 0 && highestTimestampMs < minAligned) {
        highestTimestampMs = minAligned;
    }

    if (highestTimestampMs != HighestTime.getTimestamp()) {
        rebuildWindowFromHighest(highestTimestampMs, false);
    }
}

void GridJapaneseCandle::spectatorModeActualize() {
    if (!gridTime) {
        return;
    }

    if (!db || !db->isReady()) {
        ready_ = false;
        return;
    }

    auto highestOpt = gridTime->getHighestTime();
    if (!highestOpt) {
        return;
    }

    if (!refreshDataRange()) {
        candles.clear();
        ready_ = false;
        return;
    }

    if (minOpenTime_ != lastLoggedMin_ || maxOpenTime_ != lastLoggedMax_) {
        LOG_INFO(logging::LogCategory::DB,
                 "Range min=%lld max=%lld",
                 static_cast<long long>(minOpenTime_),
                 static_cast<long long>(maxOpenTime_));
        lastLoggedMin_ = minOpenTime_;
        lastLoggedMax_ = maxOpenTime_;
    }

    long long requestedHighest = domain::alignToMinute(highestOpt->getTimestamp());
    const long long maxAligned = domain::alignToMinute(maxOpenTime_);
    const long long minAligned = alignToMinuteCeil(minOpenTime_);
    if (maxAligned > 0 && requestedHighest > maxAligned) {
        requestedHighest = maxAligned;
    }
    if (minAligned > 0 && requestedHighest < minAligned) {
        requestedHighest = minAligned;
    }

    bool rebuilt = false;
    if (!ready_ || requestedHighest != HighestTime.getTimestamp()) {
        rebuilt = rebuildWindowFromHighest(requestedHighest, true);
    }

    if (!rebuilt) {
        if (auto latestOpt = db->getLastEventTimestamp()) {
            const auto alignedLatest = domain::alignToMinute(*latestOpt);
            if (alignedLatest != lastKnownMaxTimestamp_) {
                rebuildWindowFromHighest(alignedLatest, true);
            }
        }
    }
}

float GridJapaneseCandle::getHighestMax() {
    if (!ready_) {
        return 0.0f;
    }
    return highestMax;
}

float GridJapaneseCandle::getLowestMin() {
    if (!ready_) {
        return 0.0f;
    }
    return lowestMin;
}

void GridJapaneseCandle::spectatorMode(bool onOf) {
    spectatorModeOn = onOf;
    if (!isReady()) {
        return;
    }

    if (onOf) {
        for (std::size_t i = 0; i < candles.size(); ++i) {
            candles[i]->subscribeToCache(static_cast<int>(i));
        }
        candles.front()->subscribeToFullData();
    }
    else {
        for (const auto& candle : candles) {
            candle->unsubscribeFromCache();
        }
        candles.front()->unsubscribeFromFullData();
    }
}

void GridJapaneseCandle::draw(sf::RenderWindow& w) {
    (void)w;
    if (!isReady()) {
        if (renderManager_ && loadingTextReady_) {
            sf::Text textCopy = loadingText_;
            renderManager_->addRenderCommand(1, [textCopy](sf::RenderTarget& target) mutable {
                target.draw(textCopy, sf::RenderStates::Default);
            });
        }
        return;
    }
    // Rendering handled by individual candle services.
}

double GridJapaneseCandle::calculateMemoryUsage() {
    size_t totalSize = sizeof(*this);
    totalSize += candles.capacity() * sizeof(std::shared_ptr<ui::JapaneseCandleService>);
    for (const auto& candle : candles) {
        totalSize += sizeof(*candle);
    }
    return static_cast<double>(totalSize);
}

bool GridJapaneseCandle::isReady() const {
    return ready_ && !candles.empty();
}

}  // namespace ui
