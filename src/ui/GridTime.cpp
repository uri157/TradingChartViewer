#include "ui/GridTime.h"
#include "logging/Log.h"

#include "bootstrap/DIContainer.h"
#include "infra/storage/DatabaseEngine.h"

#include <algorithm>
#include <cmath>

namespace {
constexpr float kGridWidthPixels = 1800.0f;
constexpr float kGridBaselineY = 980.0f;
constexpr int kVisibleTimestamps = 20;
}

namespace ui {

long long GridTime::alignToMinuteCeil(long long timestampMs) {
    if (timestampMs <= 0) {
        return 0;
    }
    const auto aligned = domain::alignToMinute(timestampMs);
    if (aligned < timestampMs) {
        return aligned + core::TimeUtils::kMillisPerMinute;
    }
    return aligned;
}

GridTime::GridTime(int scopeId)
    : scopeId(scopeId),
      db(bootstrap::DIContainer::resolve<infra::storage::DatabaseEngine>("DatabaseEngine")),
      w(bootstrap::DIContainer::resolve<sf::RenderWindow>("RenderWindow")),
      renderManager(bootstrap::DIContainer::resolve<ui::RenderManager>("RenderManager")),
      resourceProvider(bootstrap::DIContainer::resolve<ui::ResourceProvider>("ResourceProvider")),
      lastUpdate(std::chrono::steady_clock::now()) {
    if (!db) {
        LOG_ERROR(logging::LogCategory::UI, "GridTime initialization failed: DatabaseEngine unavailable.");
        return;
    }

    const auto highestAligned = db->getLastEventTimestamp().value_or(0);
    if (!rebuildTimeline(domain::alignToMinute(highestAligned)) && !loggedNoData_) {
        LOG_WARN(logging::LogCategory::UI, "GridTime not ready: unable to build initial timeline.");
        loggedNoData_ = true;
    }
}

bool GridTime::refreshDataRange() {
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

bool GridTime::rebuildTimeline(long long highestTimestampMs) {
    if (!refreshDataRange()) {
        Timestamps.clear();
        ready_ = false;
        return false;
    }

    const long long maxAligned = domain::alignToMinute(maxOpenTime_);
    const long long minAligned = alignToMinuteCeil(minOpenTime_);
    if (highestTimestampMs <= 0 && maxAligned > 0) {
        highestTimestampMs = maxAligned;
    }
    if (maxAligned > 0 && highestTimestampMs > maxAligned) {
        highestTimestampMs = maxAligned;
    }
    if (minAligned > 0 && highestTimestampMs < minAligned) {
        highestTimestampMs = minAligned;
    }

    if (highestTimestampMs <= 0) {
        Timestamps.clear();
        ready_ = false;
        return false;
    }

    highestTime = core::Timestamp(highestTimestampMs);
    Timestamps.clear();
    Timestamps.reserve(kVisibleTimestamps);

    long long currentTimestamp = highestTimestampMs;
    for (int i = 0; i < kVisibleTimestamps; ++i) {
        if (minAligned > 0 && currentTimestamp < minAligned) {
            break;
        }
        if (!db->isTimestampWithinRange(currentTimestamp)) {
            break;
        }

        auto ts = std::make_shared<core::Timestamp>(currentTimestamp);
        ts->setPosition(0.f, kGridBaselineY);
        Timestamps.push_back(std::move(ts));
        currentTimestamp -= core::TimeUtils::kMillisPerMinute;
    }

    if (Timestamps.empty()) {
        ready_ = false;
        return false;
    }

    const long long lowestTimestampMs = Timestamps.back()->getTimestamp();
    lowestRealTime = domain::millisToMinutes(lowestTimestampMs);
    range = static_cast<long double>(domain::millisToMinutes(highestTime.getTimestamp()) - lowestRealTime);
    if (range < 0) {
        range = 0;
    }

    const long double possibleValuesInRange = range + 1.0L;
    minutesperpixel = possibleValuesInRange / kGridWidthPixels;
    if (minutesperpixel <= 0.0L) {
        minutesperpixel = 1.0L / kGridWidthPixels;
    }

    for (const auto& ts : Timestamps) {
        const auto minutes = domain::millisToMinutes(ts->getTimestamp());
        ts->setPosition(getValuePosX(static_cast<long long>(minutes)), kGridBaselineY);
    }

    ready_ = true;
    loggedNoData_ = false;

    return true;
}

std::optional<core::Timestamp> GridTime::getHighestTime() const {
    if (!ready_) {
        return std::nullopt;
    }
    return highestTime;
}

float GridTime::getValuePosX(long long dateTimeInMinutes) {
    if (!ready_ || minutesperpixel == 0.0L) {
        return 0.0f;
    }
    return static_cast<float>((dateTimeInMinutes - lowestRealTime) / minutesperpixel);
}

void GridTime::spectatorModeActualize(core::Timestamp newHighestTime) {
    long long requestedTimestamp = newHighestTime.getTimestamp();
    refreshDataRange();
    if (!ready_) {
        rebuildTimeline(requestedTimestamp);
        return;
    }

    const long long maxAligned = domain::alignToMinute(maxOpenTime_);
    const long long minAligned = alignToMinuteCeil(minOpenTime_);
    if (maxAligned > 0 && requestedTimestamp > maxAligned) {
        requestedTimestamp = maxAligned;
    }
    if (minAligned > 0 && requestedTimestamp < minAligned) {
        requestedTimestamp = minAligned;
    }

    requestedTimestamp = domain::alignToMinute(requestedTimestamp);

    if (requestedTimestamp != highestTime.getTimestamp()) {
        rebuildTimeline(requestedTimestamp);
    }
}

void GridTime::displacement(float deltaX) {
    if (!ready_) {
        return;
    }

    refreshDataRange();
    lowestRealTime -= minutesperpixel * deltaX;
    const auto visibleRangeMinutes = std::max<long long>(1, static_cast<long long>(std::llround(range + 1.0L)));
    const auto shiftSeconds = static_cast<int>(visibleRangeMinutes * core::TimeUtils::kSecondsPerMinute);

    const long long maxAligned = domain::alignToMinute(maxOpenTime_);
    const long long minAligned = alignToMinuteCeil(minOpenTime_);

    for (auto& timestampPtr : Timestamps) {
        const auto minutes = domain::millisToMinutes(timestampPtr->getTimestamp());
        timestampPtr->setPosition(getValuePosX(minutes), kGridBaselineY);
        if (timestampPtr->getPositionX() > kGridWidthPixels) {
            const long long candidateTimestamp = timestampPtr->getTimestamp() -
                static_cast<long long>(shiftSeconds) * core::TimeUtils::kMillisPerSecond;
            if (minAligned > 0 && candidateTimestamp < minAligned) {
                continue;
            }
            *timestampPtr -= shiftSeconds;
            highestTime -= static_cast<int>(core::TimeUtils::kSecondsPerMinute);
        }
        else if (timestampPtr->getPositionX() < 0.0f) {
            const long long candidateTimestamp = timestampPtr->getTimestamp() +
                static_cast<long long>(shiftSeconds) * core::TimeUtils::kMillisPerSecond;
            if (maxAligned > 0 && candidateTimestamp > maxAligned) {
                continue;
            }
            *timestampPtr += shiftSeconds;
            highestTime += static_cast<int>(core::TimeUtils::kSecondsPerMinute);
        }
    }

    long long highestTimestampMs = highestTime.getTimestamp();
    if (maxAligned > 0 && highestTimestampMs > maxAligned) {
        highestTimestampMs = maxAligned;
    }
    if (minAligned > 0 && highestTimestampMs < minAligned) {
        highestTimestampMs = minAligned;
    }

    if (highestTimestampMs != highestTime.getTimestamp()) {
        rebuildTimeline(highestTimestampMs);
    }
}

int GridTime::getRange() {
    if (!ready_) {
        return 0;
    }
    return static_cast<int>(range);
}

void GridTime::draw() {
    if (!ready_) {
        return;
    }

    // Rendering handled elsewhere.
}

double GridTime::calculateMemoryUsage(bool detailed) {
    size_t totalSize = 0;

    totalSize += sizeof(lowestRealTime);
    totalSize += sizeof(range);
    totalSize += sizeof(minutesperpixel);
    totalSize += sizeof(lastUpdate);
    totalSize += sizeof(highestTime);
    totalSize += sizeof(lastMousePos);
    totalSize += sizeof(isDragging);
    totalSize += sizeof(spectatorModeOn);
    totalSize += sizeof(ready_);
    totalSize += sizeof(loggedNoData_);

    size_t timestampsSize = sizeof(Timestamps) + (sizeof(std::shared_ptr<core::Timestamp>) * Timestamps.capacity());
    for (const auto& timestampPtr : Timestamps) {
        timestampsSize += sizeof(core::Timestamp);
    }

    totalSize += timestampsSize;

    if (detailed) {
        LOG_DEBUG(logging::LogCategory::RENDER,
                  "Vector Timestamps: %.3f KB, size=%zu",
                  static_cast<double>(timestampsSize) / 1024.0,
                  static_cast<std::size_t>(Timestamps.size()));
    }

    return static_cast<double>(totalSize) / (1024 * 1024);
}

void GridTime::onCacheUpdated(const infra::storage::PriceData& priceData) {
    core::Timestamp latest(priceData.openTime);
    if (!ready_) {
        rebuildTimeline(latest.getTimestamp());
        return;
    }

    if (priceData.openTime >= highestTime.getTimestamp()) {
        rebuildTimeline(latest.getTimestamp());
    }
}

void GridTime::subscribeToCache(int cacheIndex) {
    if (!db) {
        return;
    }
    db->addObserver(this, cacheIndex);
    subscribedToCache = true;
    LOG_INFO(logging::LogCategory::CACHE, "Subscribed to cache updates.");
}

void GridTime::unsubscribeFromCache() {
    if (!db) {
        return;
    }
    db->removeObserver(this);
    subscribedToCache = false;
    LOG_INFO(logging::LogCategory::CACHE, "Unsubscribed from cache updates.");
}

void GridTime::spectatorMode(bool onOf) {
    if (onOf) {
        subscribeToCache(0);
    }
    else {
        unsubscribeFromCache();
    }
    spectatorModeOn = onOf;
}

} // namespace ui
