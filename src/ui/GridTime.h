#pragma once

#include <SFML/Graphics.hpp>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "core/ICacheObserver.h"
#include "core/TimeUtils.h"
#include "core/Timestamp.h"
#include "infra/storage/PriceData.h"

// Forward declarations para evitar headers pesados en el .h
namespace infra { namespace storage { class DatabaseEngine; } }
namespace ui { class RenderManager; }

namespace ui {

class ResourceProvider;

class GridTime : public core::ICacheObserver {
    long double lowestRealTime; // minutos reales desde epoch (punto más bajo)
    long double range;
    long double minutesperpixel;
    std::chrono::steady_clock::time_point lastUpdate;

    infra::storage::DatabaseEngine* db;
    core::Timestamp                 highestTime;
    std::vector<std::shared_ptr<core::Timestamp>> Timestamps;

    sf::Vector2f lastMousePos;
    bool isDragging      = false;
    bool spectatorModeOn = false;
    bool subscribedToCache = false;
    bool ready_          = false;
    bool loggedNoData_   = false;

    int  scopeId;

    ui::RenderManager* renderManager = nullptr;
    sf::RenderWindow*  w             = nullptr;
    ResourceProvider*  resourceProvider = nullptr;

    bool        refreshDataRange();
    bool        rebuildTimeline(long long highestTimestampMs);
    static long long alignToMinuteCeil(long long timestampMs);

    long long minOpenTime_ = 0;
    long long maxOpenTime_ = 0;

public:
    GridTime(int scopeId);

    std::optional<core::Timestamp> getHighestTime() const;
    float  getValuePosX(long long dateTimeInMinutes);
    void   spectatorModeActualize(core::Timestamp newHighestTime);
    void   displacement(float deltaX);
    int    getRange();
    void   draw();
    double calculateMemoryUsage(bool detailed = false);

    // OBSERVER
    void onCacheUpdated(const infra::storage::PriceData& priceData) override;

    void subscribeToCache(int cacheIndex);
    void unsubscribeFromCache();

    void spectatorMode(bool onOf);
};

} // namespace ui
