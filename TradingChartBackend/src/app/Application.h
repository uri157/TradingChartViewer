#pragma once

#if 0
// TODO: legacy-ui

#include "app/ChartController.h"
#include "app/RenderSnapshotBuilder.h"
#include "app/SessionController.h"
#include "ui/InputOverlay.h"
#include "config/Config.h"
#include "core/EventBus.h"
#include "core/RenderSnapshot.h"
#include "ui/HUD.h"
#include "ui/Scene.h"
#include "ui/TopToolbar.h"
#include "ui/layout/LayoutEngine.h"
#include "ui/views/ViewRegistry.h"


#include <atomic>
#include <chrono>
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
    std::unique_ptr<ui::Scene> scene_;
    ui::Cursor* cursor_;
    infra::storage::DatabaseEngine* database_;
    infra::net::WebSocketClient* wsClient_;
    core::EventBus* eventBus_;
    ui::RenderManager* renderManager_;
    ui::ResourceProvider* resourceProvider_;
    config::Config config_;
    RenderSnapshotBuilder snapshotBuilder_;
    std::shared_ptr<sf::Font> overlayFont_;
    ui::InputOverlay inputOverlay_{};
    ui::TopToolbar topToolbar_{};
    SessionController sessionController_;

    core::RenderSnapshot lastSnapshot_;
    core::UiState lastUiState_{core::UiState::NoData};
    core::UiDataState lastUiDataState_{core::UiDataState::Loading};
    ui::HUD hud_;
    ChartController chartController_;
    std::atomic<bool> snapshotRequested_{true};
    std::atomic<std::uint64_t> snapshotVersionSeen_{0};

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
    core::EventBus::Subscription seriesUpdatedSubscription_{};
    ui::layout::LayoutConfig layoutConfig_{};
    ui::layout::Layout layout_{};
    ui::views::PanelViews panelViews_{};
    ui::views::ChartWorld chartWorld_{};
    sf::Color chartBgColor_{};
    sf::Color rightAxisBgColor_{};
    sf::Color bottomAxisBgColor_{};
    sf::Color leftSidebarBgColor_{};
    sf::Color topToolbarBgColor_{};
    sf::Color axisTextColor_{};
    sf::Color chartTextColor_{};
    int axisFontSizePx_{11};
    int chartFontSizePx_{12};
    sf::Vector2u lastWindowSize_{0U, 0U};
    bool layoutDirty_{true};
    bool lastCursorActive_{false};
    sf::Vector2i lastCursorPosition_{-1, -1};
    std::uint64_t renderVersionCounter_{0};
    bool snapshotBuildPending_{false};
    bool lastSnapshotBuildValid_{false};
    std::chrono::steady_clock::time_point lastSnapshotBuildTime_{};

    void openSessionInput_();
};

}  // namespace app

#endif  // legacy-ui

#include "config/Config.h"

namespace app {

class Application {
public:
    explicit Application(const config::Config& config);
    ~Application();

    void run();

private:
    config::Config config_;
};

}  // namespace app
