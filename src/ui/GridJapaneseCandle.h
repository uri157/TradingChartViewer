#pragma once

#include <SFML/Graphics/RenderWindow.hpp>
#include <SFML/Graphics/Text.hpp>
#include <SFML/System/Vector2.hpp>
#include <chrono>
#include <cfloat>
#include <memory>
#include <vector>

#include "core/Timestamp.h"

// Forward declarations para aligerar el header
namespace infra { namespace storage { class DatabaseEngine; } }

namespace ui {

class JapaneseCandleService;
class GridValues;
class GridTime;
class RenderManager;
class ResourceProvider;

class GridJapaneseCandle {
    GridTime*                        gridTime;
    GridValues*                      gridValues;
    infra::storage::DatabaseEngine*  db;
    RenderManager*                   renderManager_ = nullptr;
    ResourceProvider*                resourceProvider_ = nullptr;

    std::chrono::steady_clock::time_point                lastUpdate;
    std::vector<std::shared_ptr<JapaneseCandleService>>  candles;
    std::shared_ptr<JapaneseCandleService>               highestCandle, lowestCandle;

    core::Timestamp  HighestTime, LowestTime;

    int    range   = 19;
    int    scopeId = 0;

    float  highestMax = -FLT_MAX;
    float  lowestMin  =  FLT_MAX;

    sf::Vector2f  lastMousePos;
    bool          isDragging     = false;
    bool          spectatorModeOn = false;
    bool          ready_          = false;
    bool          loggedNoData_   = false;

    long long  minOpenTime_  = 0;
    long long  maxOpenTime_  = 0;
    long long  lastLoggedMin_ = -1;
    long long  lastLoggedMax_ = -1;
    long long  lastKnownMaxTimestamp_ = 0;

    sf::Text  loadingText_;
    bool      loadingTextReady_ = false;

    bool         rebuildWindow(bool logInfo = true);
    bool         rebuildWindowFromHighest(long long highestTimestampMs, bool logInfo);
    bool         refreshDataRange();
    static long long alignToMinuteCeil(long long timestampMs);
    std::size_t  targetCandleCount() const;

    std::size_t  lastMemoryUsage = 0;

public:
    GridJapaneseCandle(int scopeId);
    ~GridJapaneseCandle();

    void   draw(sf::RenderWindow& w);
    double calculateMemoryUsage();
    void   actualize();
    void   displacement();
    void   spectatorModeActualize();
    float  getHighestMax();
    float  getLowestMin();
    void   spectatorMode(bool onOf);
    bool   isReady() const;
};

} // namespace ui
