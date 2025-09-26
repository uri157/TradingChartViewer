#pragma once

#include "app/ChartController.h"
#include "app/RenderSnapshotBuilder.h"
#include "config/Config.h"
#include "core/EventBus.h"
#include "core/RenderSnapshot.h"
#include "ui/HUD.h"
#include "ui/Scene.h"

#include <SFML/Window/Event.hpp>

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>

namespace sf {
class RenderWindow;
class Font;
}

namespace core {
class SeriesCache;
}

namespace domain {
class TimeSeriesRepository;
struct CandleSeries;
}

namespace infra::exchange {
class ExchangeGateway;
}

namespace infra::net {
class WebSocketClient;
}

namespace infra::storage {
class DatabaseEngine;
class PriceDataTimeSeriesRepository;
}

namespace indicators {
class IndicatorCoordinator;
}

namespace ui {
class Cursor;
class RenderManager;
class ResourceProvider;
}

namespace app {

class SyncOrchestrator;

class Application {
public:
    explicit Application(const config::Config& config);
    ~Application();

    void run();

private:
    void enqueueCandleRender();
    void handleEvent(const sf::Event& event);

    sf::RenderWindow* window_;
    ui::Scene* scene_;
    ui::Cursor* cursor_;
    infra::storage::DatabaseEngine* database_;
    infra::net::WebSocketClient* wsClient_;
    core::EventBus* eventBus_;
    ui::RenderManager* renderManager_;
    ui::ResourceProvider* resourceProvider_;
    config::Config config_;
    RenderSnapshotBuilder snapshotBuilder_;
    std::shared_ptr<sf::Font> overlayFont_;

    core::RenderSnapshot lastSnapshot_;
    core::UiState lastUiState_{core::UiState::NoData};
    ui::HUD hud_;
    ChartController chartController_;
    std::atomic<bool> snapshotRequested_{true};

    static constexpr std::size_t kMaxFetchCandles = 2000;

    std::shared_ptr<core::SeriesCache> seriesCache_;
    std::shared_ptr<domain::TimeSeriesRepository> timeSeriesRepo_;
    std::shared_ptr<infra::exchange::ExchangeGateway> marketGateway_;
    std::shared_ptr<SyncOrchestrator> orchestrator_;
    std::shared_ptr<indicators::IndicatorCoordinator> indicatorCoordinator_;
    std::string seriesKey_;
    std::shared_ptr<const domain::CandleSeries> lastSeries_;
    std::uint64_t lastSeriesVersion_{0};
    std::atomic<bool> pendingSeriesUpdate_{false};
    core::EventBus::CallbackID seriesUpdatedSubscription_{0};
};

}  // namespace app
