#if 0
// TODO: legacy-ui
// Application.cpp
#include "app/Application.h"


#include "app/SyncOrchestrator.h"
#include "bootstrap/DIContainer.h"
#include "core/SeriesCache.h"
#include "core/TimeUtils.h"
#ifdef TTP_ENABLE_DIAG
#include "core/Diag.h"
#endif
#include "domain/DomainContracts.h"
#include "indicators/IndicatorCoordinator.h"
#include "infra/exchange/ExchangeGateway.h"
#include "infra/net/WebSocketClient.h"
#include "infra/storage/DatabaseEngine.h"
#include "infra/storage/PriceData.h"
#include "infra/storage/PriceDataTimeSeriesRepository.h"
#include "logging/Log.h"
#include "ui/ChartsScene.h"
#include "ui/RenderManager.h"
#include "ui/ResourceProvider.h"

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <chrono>
#include <memory>
#include <mutex>
#include <cctype>
#include <cmath>
#include <ctime>
#include <iomanip>
#include <limits>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <utility>

namespace {
std::mutex gRenderMutex;
std::condition_variable gRenderCv;
bool gNeedsDraw = false;
}

namespace {
const sf::Color kBullishColor(48, 190, 120);
const sf::Color kBearishColor(220, 85, 85);
const sf::Color kGridColor(55, 66, 86, 120);
const sf::Color kHorizontalGridColor(58, 70, 92, 140);
const sf::Color kAxisColor(120, 130, 150);
const sf::Color kCrosshairColor(230, 235, 245, 200);
const sf::Color kTooltipBgColor(18, 22, 30, 220);
const sf::Color kIndicatorColor(255, 206, 86);
constexpr std::size_t kUiMinHistoryCandles{300};

const char* uiDataStateLabel(core::UiDataState state) {
    switch (state) {
    case core::UiDataState::Loading:
        return "Loading";
    case core::UiDataState::LiveOnly:
        return "LiveOnly";
    case core::UiDataState::Ready:
        return "Ready";
    }
    return "Unknown";
}

std::tm toUtcTm(long long timestampMs) {
    const std::time_t seconds = static_cast<std::time_t>(timestampMs / 1000);
    std::tm tm{};
#if defined(_WIN32)
    gmtime_s(&tm, &seconds);
#else
    gmtime_r(&seconds, &tm);
#endif
    return tm;
}

std::string formatTimeShort(long long timestampMs) {
    if (timestampMs <= 0) {
        return "--:--";
    }
    std::tm tm = toUtcTm(timestampMs);
    std::ostringstream oss;
    oss << std::put_time(&tm, "%H:%M");
    return oss.str();
}

std::string formatPriceShort(double value, int decimals) {
    std::ostringstream oss;
    oss.setf(std::ios::fixed, std::ios::floatfield);
    oss << std::setprecision(std::clamp(decimals, 0, 8)) << value;
    return oss.str();
}

int extractDecimals(const std::string& text) {
    const auto pos = text.find('.');
    if (pos == std::string::npos) {
        return 0;
    }
    const std::size_t decimals = text.size() - pos - 1;
    return static_cast<int>(decimals);
}
}

namespace {
domain::Interval clampInterval(domain::Interval interval) {
    if (interval.valid()) {
        return interval;
    }
    domain::Interval fallback;
    fallback.ms = core::TimeUtils::kMillisPerMinute;
    return fallback;
}

std::string toLower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

std::string intervalLabelOrFallback(const domain::Interval& interval) {
    auto label = domain::interval_label(interval);
    if (!label.empty()) {
        return label;
    }
    if (interval.valid()) {
        return std::to_string(interval.ms) + "ms";
    }
    return "?";
}

domain::TimestampMs parseLookbackMs(const std::string& raw, domain::TimestampMs fallback) {
    auto trim = [](const std::string& s) {
        std::size_t start = 0;
        while (start < s.size() && std::isspace(static_cast<unsigned char>(s[start])) != 0) {
            ++start;
        }
        std::size_t end = s.size();
        while (end > start && std::isspace(static_cast<unsigned char>(s[end - 1])) != 0) {
            --end;
        }
        return s.substr(start, end - start);
    };

    const auto value = trim(raw);
    if (value.empty()) {
        return fallback;
    }
    if (value == "0") {
        return 0;
    }

    std::size_t idx = 0;
    while (idx < value.size() && std::isdigit(static_cast<unsigned char>(value[idx])) != 0) {
        ++idx;
    }
    if (idx == 0) {
        return fallback;
    }

    long long magnitude = 0;
    try {
        magnitude = std::stoll(value.substr(0, idx));
    }
    catch (...) {
        return fallback;
    }
    if (magnitude < 0) {
        return fallback;
    }

    std::string suffix = toLower(trim(value.substr(idx)));
    const long long maxMs = std::numeric_limits<domain::TimestampMs>::max();

    auto safeMul = [&](long long base, long long mul) -> domain::TimestampMs {
        if (base == 0 || mul == 0) {
            return 0;
        }
        if (base > 0 && mul > 0 && base > maxMs / mul) {
            return maxMs;
        }
        return static_cast<domain::TimestampMs>(base * mul);
    };

    if (suffix.empty() || suffix == "ms") {
        return static_cast<domain::TimestampMs>(magnitude);
    }
    if (suffix == "s") {
        return safeMul(magnitude, core::TimeUtils::kMillisPerSecond);
    }
    if (suffix == "m") {
        return safeMul(magnitude, core::TimeUtils::kMillisPerMinute);
    }
    if (suffix == "h") {
        return safeMul(magnitude, core::TimeUtils::kMillisPerMinute * 60);
    }
    if (suffix == "d") {
        return safeMul(magnitude, core::TimeUtils::kMillisPerMinute * 60 * 24);
    }
    if (suffix == "w") {
        return safeMul(magnitude, core::TimeUtils::kMillisPerMinute * 60 * 24 * 7);
    }

    return fallback;
}

app::SyncConfig::BackfillMode parseBackfillMode(const std::string& value) {
    const auto lower = toLower(value);
    if (lower == "reverse") {
        return app::SyncConfig::BackfillMode::Reverse;
    }
    if (lower == "forward") {
        return app::SyncConfig::BackfillMode::Forward;
    }
    return app::SyncConfig::BackfillMode::Auto;
}
}  // namespace

namespace app {

void requestDraw() {
    std::lock_guard<std::mutex> lock(gRenderMutex);
    gNeedsDraw = true;
    gRenderCv.notify_one();
}

}  // namespace app

namespace app {

Application::Application(const config::Config& config)
    : window_(bootstrap::DIContainer::resolve<sf::RenderWindow>("RenderWindow")),
      cursor_(bootstrap::DIContainer::resolve<ui::Cursor>("Cursor")),
      database_(bootstrap::DIContainer::resolve<infra::storage::DatabaseEngine>("DatabaseEngine")),
      renderManager_(bootstrap::DIContainer::resolve<ui::RenderManager>("RenderManager")),
      wsClient_(bootstrap::DIContainer::resolve<infra::net::WebSocketClient>("WebSocketClient")),
      eventBus_(bootstrap::DIContainer::resolve<core::EventBus>("EventBus")),
      resourceProvider_(bootstrap::DIContainer::resolve<ui::ResourceProvider>("ResourceProvider")),
      config_(config),
      snapshotBuilder_(0.75f, 1.5f),
      sessionController_({config.symbol, clampInterval(domain::interval_from_label(config.interval))}) {
    if (resourceProvider_) {
        overlayFont_ = resourceProvider_->getFont("ui");
        inputOverlay_.setResourceProvider(resourceProvider_);
        topToolbar_.setResourceProvider(resourceProvider_);
    }

    layoutConfig_.topToolbarH = config_.topToolbarHeight;
    layoutConfig_.leftSidebarW = config_.leftSidebarWidth;
    layoutConfig_.rightAxisW = config_.rightAxisWidth;
    layoutConfig_.bottomAxisH = config_.bottomAxisHeight;
    if (layoutConfig_.topToolbarH <= 0) {
        layoutConfig_.topToolbarH = 28;
    }
    axisFontSizePx_ = std::max(config_.uiAxisFontSizePx, 1);
    chartFontSizePx_ = std::max(config_.uiChartFontSizePx, 1);

    const std::string themeName = toLower(config_.uiTheme);
    if (themeName == "light") {
        chartBgColor_ = sf::Color(245, 247, 250);
        rightAxisBgColor_ = sf::Color(235, 238, 242);
        bottomAxisBgColor_ = sf::Color(235, 238, 242);
        leftSidebarBgColor_ = sf::Color(240, 242, 245);
        topToolbarBgColor_ = sf::Color(230, 233, 238);
        axisTextColor_ = sf::Color(40, 44, 52);
        chartTextColor_ = sf::Color(40, 44, 52);
    }
    else {
        chartBgColor_ = sf::Color(11, 13, 18);
        rightAxisBgColor_ = sf::Color(15, 17, 21);
        bottomAxisBgColor_ = sf::Color(15, 17, 21);
        leftSidebarBgColor_ = sf::Color(11, 13, 18);
        topToolbarBgColor_ = sf::Color(16, 18, 24);
        axisTextColor_ = sf::Color(210, 214, 228);
        chartTextColor_ = sf::Color(210, 214, 228);
    }

    topToolbar_.setBackgroundColor(topToolbarBgColor_);

    if (renderManager_) {
        renderManager_->setDrawRequestCallback(requestDraw);
        renderManager_->setPanelBackground(ui::PanelId::Chart, chartBgColor_);
        renderManager_->setPanelBackground(ui::PanelId::RightAxis, rightAxisBgColor_);
        renderManager_->setPanelBackground(ui::PanelId::BottomAxis, bottomAxisBgColor_);
        renderManager_->setPanelBackground(ui::PanelId::LeftSidebar, leftSidebarBgColor_);
        renderManager_->setPanelBackground(ui::PanelId::TopToolbar, sf::Color(0, 0, 0, 0));
    }

    chartController_.attachWindow(window_);
    chartController_.setRenderManager(renderManager_);
    chartController_.setVisibleLimits(30, 5000);
    chartController_.setAutoViewportEnabled(config_.autoViewport);
    chartController_.setSnapshotRequestCallback([this]() {
        snapshotRequested_.store(true, std::memory_order_release);
    });
    if (eventBus_) {
        chartController_.bindEventBus(eventBus_);
    }

    if (window_) {
        window_->setVerticalSyncEnabled(true);
        window_->setFramerateLimit(60);

        const auto size = window_->getSize();
        layout_ = ui::layout::computeLayout(size, layoutConfig_);
        topToolbar_.setBounds(sf::FloatRect(static_cast<float>(layout_.topToolbar.x),
                                            static_cast<float>(layout_.topToolbar.y),
                                            static_cast<float>(layout_.topToolbar.w),
                                            static_cast<float>(layout_.topToolbar.h)));
        chartController_.setLayout(layout_);
        chartWorld_.size = sf::Vector2f(static_cast<float>(std::max(layout_.chart.w, 1)),
                                        static_cast<float>(std::max(layout_.chart.h, 1)));
        chartWorld_.center = sf::Vector2f(chartWorld_.size.x * 0.5f, chartWorld_.size.y * 0.5f);
        panelViews_ = ui::views::buildViews(layout_, size, chartWorld_);
    }

    seriesCache_ = std::make_shared<core::SeriesCache>();
    infra::exchange::ExchangeGatewayConfig gatewayCfg;
    gatewayCfg.restHost = config_.restHost;
    gatewayCfg.wsHost = config_.wsHost;
    gatewayCfg.wsPathTemplate = config_.wsPathTemplate;
    gatewayCfg.trace = config::logLevelAtLeast(config_.logLevel, config::LogLevel::Debug);
    marketGateway_ = std::make_shared<infra::exchange::ExchangeGateway>(gatewayCfg);

    domain::Interval repoInterval = domain::interval_from_label(config_.interval);
    repoInterval = clampInterval(repoInterval);
    auto diskRepo = std::make_shared<infra::storage::PriceDataTimeSeriesRepository>();
    diskRepo->bind(config_.symbol, repoInterval, infra::storage::Paths{config_.cacheDir});
    timeSeriesRepo_ = diskRepo;
    indicatorCoordinator_ = std::make_shared<indicators::IndicatorCoordinator>(timeSeriesRepo_);
    snapshotBuilder_.setIndicatorCoordinator(indicatorCoordinator_);

    app::SyncConfig syncCfg;
    syncCfg.lookbackMaxMs = parseLookbackMs(config_.lookbackMax, syncCfg.lookbackMaxMs);
    if (config_.backfillChunk > 0) {
        syncCfg.backfillChunk = static_cast<int>(config_.backfillChunk);
    }
    syncCfg.backfillMode = parseBackfillMode(config_.backfillMode);
    syncCfg.wsWarmup = config_.wsWarmup;
    syncCfg.seriesCache = seriesCache_.get();
    syncCfg.eventBus = eventBus_;
    syncCfg.publishCandles = std::max<std::size_t>(config_.publishCandles, std::size_t{1});

    orchestrator_ = std::make_shared<app::SyncOrchestrator>(*marketGateway_, *diskRepo, infra::storage::Paths{config_.cacheDir}, indicatorCoordinator_.get(), syncCfg);

    auto initialLabel = intervalLabelOrFallback(sessionController_.current().interval);
    topToolbar_.setSymbolInterval(sessionController_.current().symbol, initialLabel);
    seriesKey_ = sessionController_.current().symbol + "@" + initialLabel;
    snapshotBuilder_.setUiState(core::UiDataState::Loading,
                                sessionController_.current().symbol,
                                initialLabel,
                                -1.0f);
    lastSnapshot_.ui.state = core::UiDataState::Loading;
    lastSnapshot_.ui.symbol = sessionController_.current().symbol;
    lastSnapshot_.ui.interval = initialLabel;
    lastSnapshot_.ui.progress = -1.0f;
    lastUiDataState_ = core::UiDataState::Loading;
    snapshotVersionSeen_.store(0, std::memory_order_release);
    snapshotBuildPending_ = false;
    lastSnapshotBuildValid_ = false;

    sessionController_.onChange([this](const SessionState& oldS, const SessionState& newS) {
        const std::string oldLabel = intervalLabelOrFallback(oldS.interval);
        const std::string newLabel = intervalLabelOrFallback(newS.interval);
        LOG_INFO(logging::LogCategory::UI,
                 "SESSION:switch from=<%s,%s> to=<%s,%s>",
                 oldS.symbol.c_str(),
                 oldLabel.c_str(),
                 newS.symbol.c_str(),
                 newLabel.c_str());

        seriesKey_ = newS.symbol + "@" + newLabel;
        topToolbar_.setSymbolInterval(newS.symbol, newLabel);
        snapshotBuilder_.setUiState(core::UiDataState::Loading, newS.symbol, newLabel, -1.0f);
        lastSnapshot_.ui.state = core::UiDataState::Loading;
        lastSnapshot_.ui.symbol = newS.symbol;
        lastSnapshot_.ui.interval = newLabel;
        lastSnapshot_.ui.progress = -1.0f;
        lastUiDataState_ = core::UiDataState::Loading;
        snapshotVersionSeen_.store(0, std::memory_order_release);
        snapshotBuildPending_ = false;
        lastSnapshotBuildValid_ = false;
        snapshotRequested_.store(true, std::memory_order_release);
        pendingSeriesUpdate_.store(true, std::memory_order_release);

        if (indicatorCoordinator_) {
            try {
                indicatorCoordinator_->invalidateAll();
            } catch (...) {
                LOG_WARN(logging::LogCategory::UI, "Indicator invalidation threw during session switch");
            }
        }

        chartController_.setSeries(nullptr);
        if (config_.autoViewport) {
            chartController_.resetViewportToRecent();
        }
        lastSeries_.reset();
        lastSeriesVersion_ = 0;

        if (orchestrator_) {
            orchestrator_->switchTo(newS);
        }

        if (renderManager_) {
            renderManager_->markNeedsDraw();
        }
        requestDraw();
    });

    orchestrator_->start(sessionController_.current());

    wsClient_->setDataHandler([this](const infra::storage::PriceData& data) {
        database_->updateWithNewData(data);
    });

    database_->warmupAsync();
    wsClient_->connect();

    scene_ = std::make_unique<ui::ChartsScene>();
    window_->setFramerateLimit(60);

    if (eventBus_) {
        seriesUpdatedSubscription_ = eventBus_->subscribeSeriesUpdated([this](const core::EventBus::SeriesUpdated&) {
            pendingSeriesUpdate_.store(true, std::memory_order_release);
            if (renderManager_) {
                renderManager_->markNeedsDraw();
            }
            requestDraw();
        });
    }

    if (renderManager_) {
        renderManager_->markNeedsDraw();
    }
    requestDraw();
}

void Application::openSessionInput_() {
    inputOverlay_.open(
        "Enter: SYMBOL INTERVAL (e.g. ETHUSDT 5m) — ESC to cancel",
        [this](const std::string& line) {
            std::istringstream iss(line);
            std::string symbol;
            std::string intervalStr;
            iss >> symbol >> intervalStr;
            if (symbol.empty() || intervalStr.empty()) {
                LOG_WARN(logging::LogCategory::UI,
                         "SESSION: invalid input (expected 'SYMBOL INTERVAL'), got='%s'",
                         line.c_str());
                return;
            }
            std::string normalizedSymbol = symbol;
            std::transform(normalizedSymbol.begin(),
                           normalizedSymbol.end(),
                           normalizedSymbol.begin(),
                           [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
            const domain::Interval parsed = domain::interval_from_label(intervalStr);
            if (!parsed.valid()) {
                LOG_WARN(logging::LogCategory::UI,
                         "SESSION: unknown interval='%s'",
                         intervalStr.c_str());
                return;
            }
            const std::string intervalLabel = intervalLabelOrFallback(parsed);
            LOG_INFO(logging::LogCategory::UI,
                     "SESSION: request switch to=<%s,%s>",
                     normalizedSymbol.c_str(),
                     intervalLabel.c_str());
            topToolbar_.setSymbolInterval(normalizedSymbol, intervalLabel);
            sessionController_.switchTo(std::move(normalizedSymbol), parsed);
        },
        [this]() {
            LOG_INFO(logging::LogCategory::UI, "SESSION: switch canceled by user");
        });
    if (renderManager_) {
        renderManager_->markNeedsDraw();
    }
    requestDraw();
}

void Application::run() {
    window_->setMouseCursorVisible(false);
    sf::Event e;
    auto handleOverlayInteraction = [this](const sf::Event& event) {
        if (event.type == sf::Event::KeyPressed) {
            if (inputOverlay_.isOpen()) {
                inputOverlay_.handleEvent(event);
                if (renderManager_) {
                    renderManager_->markNeedsDraw();
                }
                requestDraw();
                return true;
            }
            if (event.key.code == sf::Keyboard::L && (event.key.control || event.key.system)) {
                openSessionInput_();
                return true;
            }
        }
        if (inputOverlay_.isOpen() && event.type == sf::Event::TextEntered) {
            inputOverlay_.handleEvent(event);
            if (renderManager_) {
                renderManager_->markNeedsDraw();
            }
            requestDraw();
            return true;
        }
        return false;
    };
    auto handleTopToolbarInteraction = [this](const sf::Event& event) {
        if (inputOverlay_.isOpen()) {
            return false;
        }
        if (event.type == sf::Event::MouseButtonPressed && event.mouseButton.button == sf::Mouse::Left) {
            const sf::Vector2f mousePos(static_cast<float>(event.mouseButton.x),
                                        static_cast<float>(event.mouseButton.y));
            if (topToolbar_.hitChangeButton(mousePos)) {
                openSessionInput_();
                return true;
            }
        }
        return false;
    };
    auto dispatchEvent = [&](const sf::Event& event) {
        if (event.type == sf::Event::Closed) {
            window_->close();
            return true;
        }
        if (handleOverlayInteraction(event)) {
            return true;
        }
        if (handleTopToolbarInteraction(event)) {
            return true;
        }
        if (eventBus_) {
            eventBus_->publish(event);
        }
        if (event.type != sf::Event::Closed) {
            handleEvent(event);
        }
        return true;
    };
#ifdef TTP_ENABLE_DIAG
    struct FrameGuard {
        std::chrono::steady_clock::time_point start{std::chrono::steady_clock::now()};
        core::diag::ScopedTimer timer{core::diag::timer("ui.frame")};
        ~FrameGuard() {
            const auto end = std::chrono::steady_clock::now();
            const auto nanos = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
            if (nanos > 16'000'000) {
                core::diag::incr("ui.frame.gt16ms");
            }
        }
    };
#endif
    while (window_->isOpen()) {
#ifdef TTP_ENABLE_DIAG
        core::diag::diag_tick();
        FrameGuard frameGuard;
#endif
        bool eventOccurred = false;
        while (window_->pollEvent(e)) {
            if (dispatchEvent(e)) {
                eventOccurred = true;
            }
        }

        if (!window_->isOpen()) {
            break;
        }

        bool drawRequested = false;
        {
            std::unique_lock<std::mutex> lk(gRenderMutex);
            if (!gNeedsDraw) {
                gRenderCv.wait_for(lk, std::chrono::milliseconds(2), [] { return gNeedsDraw; });
            }
            if (gNeedsDraw) {
                drawRequested = true;
                gNeedsDraw = false;
            }
        }

        const bool managerNeedsDraw = renderManager_ && renderManager_->needsDraw();
        if (managerNeedsDraw) {
            drawRequested = true;
        }

        if (!drawRequested) {
            if (!eventOccurred) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            continue;
        }

        enqueueCandleRender();

        if (!renderManager_ || !renderManager_->hasCommands()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }

        renderManager_->render(*window_, panelViews_, layout_);
        window_->setView(window_->getDefaultView());
        topToolbar_.draw(*window_);
        if (inputOverlay_.isOpen()) {
            inputOverlay_.draw(*window_);
        }
        window_->display();
    }
    wsClient_->disconnect();
}

Application::~Application() {
    seriesUpdatedSubscription_.reset();
    chartController_.unbindEventBus();
    if (orchestrator_) {
        orchestrator_->stop();
    }
    scene_.reset();
    if (eventBus_) {
        eventBus_->clearAll();
    }
}

void Application::handleEvent(const sf::Event& event) {
    (void)event;
}

void Application::enqueueCandleRender() {
    if (!renderManager_ || !window_) {
        return;
    }

    const auto size = window_->getSize();
    if (size.x == 0 || size.y == 0) {
        return;
    }

    bool layoutChanged = false;
    if (size != lastWindowSize_) {
        lastWindowSize_ = size;
        layoutDirty_ = true;
    }

    if (layoutDirty_) {
        layout_ = ui::layout::computeLayout(size, layoutConfig_);
        topToolbar_.setBounds(sf::FloatRect(static_cast<float>(layout_.topToolbar.x),
                                            static_cast<float>(layout_.topToolbar.y),
                                            static_cast<float>(layout_.topToolbar.w),
                                            static_cast<float>(layout_.topToolbar.h)));
        chartController_.setLayout(layout_);

        const unsigned layoutChartWidth = layout_.chart.w > 0 ? static_cast<unsigned>(layout_.chart.w)
                                                             : static_cast<unsigned>(size.x);
        const unsigned layoutChartHeight = layout_.chart.h > 0 ? static_cast<unsigned>(layout_.chart.h)
                                                              : static_cast<unsigned>(size.y);
        if (layoutChartWidth == 0 || layoutChartHeight == 0) {
            return;
        }
        chartWorld_.size = sf::Vector2f(static_cast<float>(layoutChartWidth), static_cast<float>(layoutChartHeight));
        chartWorld_.center = sf::Vector2f(chartWorld_.size.x * 0.5f, chartWorld_.size.y * 0.5f);
        panelViews_ = ui::views::buildViews(layout_, size, chartWorld_);
        layoutDirty_ = false;
        layoutChanged = true;
    }

    const unsigned chartWidth = layout_.chart.w > 0 ? static_cast<unsigned>(layout_.chart.w)
                                                    : static_cast<unsigned>(size.x);
    const unsigned chartHeight = layout_.chart.h > 0 ? static_cast<unsigned>(layout_.chart.h)
                                                     : static_cast<unsigned>(size.y);
    if (chartWidth == 0 || chartHeight == 0) {
        return;
    }

    if (eventBus_ && eventBus_->consumeSeriesChanged()) {
        pendingSeriesUpdate_.store(true, std::memory_order_release);
        snapshotRequested_.store(true, std::memory_order_release);
    }

    const bool viewRequested = snapshotRequested_.exchange(false, std::memory_order_acq_rel);
    const bool viewportChanged = chartController_.consumeViewportDirty();

    std::shared_ptr<const domain::CandleSeries> cacheSeries;
    std::uint64_t cacheVersion = 0;
    if (seriesCache_) {
        cacheVersion = seriesCache_->version();
        cacheSeries = seriesCache_->snapshot();
    }

    const bool eventTriggered = pendingSeriesUpdate_.exchange(false, std::memory_order_acq_rel);
    const bool cacheChanged = cacheSeries && !cacheSeries->empty() && cacheVersion != lastSeriesVersion_;

    if (cacheSeries && !cacheSeries->empty() && (cacheChanged || eventTriggered || !lastSeries_)) {
        LOG_DEBUG(logging::LogCategory::UI,
                  "Series update version=%llu candles=%zu first=%lld last=%lld",
                  static_cast<unsigned long long>(cacheVersion),
                  cacheSeries->data.size(),
                  static_cast<long long>(cacheSeries->firstOpen),
                  static_cast<long long>(cacheSeries->lastOpen));
        chartController_.setSeries(cacheSeries);
        lastSeries_ = cacheSeries;
        lastSeriesVersion_ = cacheVersion;
    }
    else if ((!cacheSeries || cacheSeries->empty()) && eventTriggered) {
        chartController_.setSeries(nullptr);
        lastSeries_.reset();
        lastSeriesVersion_ = 0;
    }

    std::shared_ptr<const domain::CandleSeries> renderSeries = lastSeries_;
    if ((!renderSeries || renderSeries->empty()) && cacheSeries && !cacheSeries->empty()) {
        renderSeries = cacheSeries;
    }

    if ((!renderSeries || renderSeries->empty()) && timeSeriesRepo_) {
        const std::size_t repoCount = timeSeriesRepo_->candleCount();
        if (repoCount > 0) {
            const std::size_t requestCount = std::min<std::size_t>(repoCount, kMaxFetchCandles);
            auto latest = timeSeriesRepo_->getLatest(requestCount);
            if (!latest.failed() && !latest.value.data.empty()) {
                auto fallback = std::make_shared<domain::CandleSeries>(std::move(latest.value));
                LOG_DEBUG(logging::LogCategory::UI,
                          "Repository fallback candles=%zu first=%lld last=%lld",
                          fallback->data.size(),
                          static_cast<long long>(fallback->firstOpen),
                          static_cast<long long>(fallback->lastOpen));
                renderSeries = fallback;
                chartController_.setSeries(renderSeries);
                lastSeries_ = renderSeries;
                lastSeriesVersion_ = cacheVersion;
            }
        }
    }

    bool cursorActive = false;
    sf::Vector2i currentCursor{-1, -1};
    app::RenderSnapshotBuilder::CursorState cursorState;
    std::optional<app::RenderSnapshotBuilder::CursorState> cursorOpt;
    const sf::Vector2i mousePixel = sf::Mouse::getPosition(*window_);
    if (mousePixel.x >= 0 && mousePixel.y >= 0 &&
        mousePixel.x < static_cast<int>(size.x) && mousePixel.y < static_cast<int>(size.y)) {
        const ui::PanelId panel = whichPanel(mousePixel, layout_);
        if (panel == ui::PanelId::Chart) {
            cursorActive = true;
            currentCursor = mousePixel;
            cursorState.active = true;
            cursorState.x = static_cast<float>(mousePixel.x - layout_.chart.x);
            cursorState.y = static_cast<float>(mousePixel.y - layout_.chart.y);
            cursorOpt = cursorState;
        }
    }
    const bool cursorChanged = (cursorActive != lastCursorActive_) ||
                               (cursorActive && currentCursor != lastCursorPosition_);

    app::RenderSnapshotBuilder::RepoView repoView{};
    if (timeSeriesRepo_) {
        const auto meta = timeSeriesRepo_->metadata();
        repoView.candleCount = meta.count;
        repoView.hasGap = timeSeriesRepo_->hasGap();
        repoView.intervalMs = timeSeriesRepo_->intervalMs();
        repoView.lastClosedOpenTime = timeSeriesRepo_->lastClosedOpenTime();
        repoView.seriesKey = seriesKey_;
        if ((cacheChanged || eventTriggered) && meta.count > 0) {
            LOG_DEBUG(logging::LogCategory::UI,
                      "Repo stats candles=%zu first=%lld last=%lld",
                      meta.count,
                      static_cast<long long>(meta.minOpen),
                      static_cast<long long>(meta.maxOpen));
        }
    } else if (renderSeries) {
        repoView.candleCount = renderSeries->data.size();
        repoView.hasGap = false;
        repoView.intervalMs = renderSeries->interval.valid() ? renderSeries->interval.ms : core::TimeUtils::kMillisPerMinute;
        repoView.lastClosedOpenTime = renderSeries->data.empty() ? 0 : renderSeries->data.back().openTime;
        repoView.seriesKey = seriesKey_;
    }

    app::RenderSnapshotBuilder::ConnectivityView netView{};
    if (wsClient_) {
        netView.wsConnected = wsClient_->isWsConnected();
        netView.lastTickMs = wsClient_->lastTickMs();
    }
    if (orchestrator_) {
        netView.backfilling = orchestrator_->isBackfilling();
        netView.gapDetected = orchestrator_->hasLiveGap();
    }

    app::RenderSnapshotBuilder::StateInputs stateInputs{repoView, netView};
    std::optional<app::RenderSnapshotBuilder::StateInputs> stateInputsOpt(stateInputs);

    domain::CandleSeries emptySeries;
    if (repoView.intervalMs > 0) {
        emptySeries.interval.ms = repoView.intervalMs;
    }

    const SessionState& session = sessionController_.current();
    const std::string intervalLabel = intervalLabelOrFallback(session.interval);
    core::UiDataState dataState = core::UiDataState::Loading;
    if (repoView.candleCount > 0 && repoView.candleCount < kUiMinHistoryCandles) {
        if (netView.wsConnected || netView.lastTickMs > 0) {
            dataState = core::UiDataState::LiveOnly;
        }
    } else if (repoView.candleCount >= kUiMinHistoryCandles) {
        dataState = core::UiDataState::Ready;
    }
    snapshotBuilder_.setUiState(dataState, session.symbol, intervalLabel, -1.0f);

    const bool hasRenderableSeries = renderSeries && !renderSeries->empty();
    const domain::CandleSeries& buildSeries = hasRenderableSeries ? *renderSeries : emptySeries;
    const bool uiDrivenChange = layoutChanged || viewportChanged || cursorChanged || viewRequested || !lastSnapshot_.valid;
    bool dataDrivenChange = cacheChanged || eventTriggered;
    const std::uint64_t currentSnapshotVersion = orchestrator_ ? orchestrator_->snapshotVersion() : 0;
    const std::uint64_t seenSnapshotVersion = snapshotVersionSeen_.load(std::memory_order_acquire);
    const bool versionChanged = currentSnapshotVersion != seenSnapshotVersion;
    if (dataDrivenChange && !versionChanged) {
        dataDrivenChange = false;
    }
    bool shouldBuildSnapshot = uiDrivenChange || dataDrivenChange || versionChanged || snapshotBuildPending_;

    const auto now = std::chrono::steady_clock::now();
    constexpr auto kMinSnapshotInterval = std::chrono::milliseconds(16);
    const bool throttleActive = lastSnapshotBuildValid_ && (now - lastSnapshotBuildTime_) < kMinSnapshotInterval;

    if (!shouldBuildSnapshot) {
        return;
    }

    if (throttleActive) {
        snapshotBuildPending_ = true;
        requestDraw();
        return;
    }

    snapshotBuildPending_ = false;
    lastSnapshotBuildValid_ = true;
    lastSnapshotBuildTime_ = now;

    core::RenderSnapshot newSnapshot;
    if (hasRenderableSeries) {
        newSnapshot = snapshotBuilder_.build(buildSeries,
                                             chartController_.view(),
                                             chartWidth,
                                             chartHeight,
                                             cursorOpt,
                                             stateInputsOpt);
    } else {
        newSnapshot = snapshotBuilder_.buildUiOnly(stateInputsOpt);
        newSnapshot.canvasWidth = chartWidth;
        newSnapshot.canvasHeight = chartHeight;
    }

    const auto newVersion = ++renderVersionCounter_;
    newSnapshot.version_.store(newVersion, std::memory_order_release);
    lastSnapshot_ = std::move(newSnapshot);
    snapshotVersionSeen_.store(currentSnapshotVersion, std::memory_order_release);
    lastCursorActive_ = cursorActive;
    lastCursorPosition_ = cursorActive ? currentCursor : sf::Vector2i{-1, -1};

    if (lastSnapshot_.ui.state != lastUiDataState_) {
        LOG_INFO(logging::LogCategory::SNAPSHOT,
                 "SNAPSHOT:publish state=%s symbol=%s interval=%s candles=%zu",
                 uiDataStateLabel(lastSnapshot_.ui.state),
                 lastSnapshot_.ui.symbol.c_str(),
                 lastSnapshot_.ui.interval.c_str(),
                 repoView.candleCount);
        lastUiDataState_ = lastSnapshot_.ui.state;
    }

    if (lastSnapshot_.state != lastUiState_) {
        // LEGACY-UI removed LOG_INFO(logging::LogCategory::UI,
        //                          "UI state changed %d -> %d",
        //                          static_cast<int>(lastUiState_),
        //                          static_cast<int>(lastSnapshot_.state));
        lastUiState_ = lastSnapshot_.state;
    }

    const core::RenderSnapshot& snapshot = lastSnapshot_;

    if (resourceProvider_) {
        ui::HUD* hud = &hud_;
        ui::ResourceProvider* rp = resourceProvider_;
        core::RenderSnapshot overlaySnapshot = snapshot;
        renderManager_->addRenderCommand(ui::PanelId::Chart, ui::LayerId::HUD, [hud, rp, overlaySnapshot](sf::RenderTarget& target) mutable {
            if (hud && rp) {
                hud->drawStateOverlay(target, overlaySnapshot, *rp);
            }
        });
    }

    if (!snapshot.valid) {
        return;
    }

    renderManager_->addRenderCommand(ui::PanelId::Chart, ui::LayerId::Grid, [snapshot](sf::RenderTarget& target) mutable {
        sf::VertexArray grid(sf::Lines);
        for (const auto& label : snapshot.timeLabels) {
            grid.append(sf::Vertex(sf::Vector2f(label.x, 0.0f), kGridColor));
            grid.append(sf::Vertex(sf::Vector2f(label.x, static_cast<float>(snapshot.canvasHeight)), kGridColor));
        }
        for (const auto& label : snapshot.priceLabels) {
            grid.append(sf::Vertex(sf::Vector2f(0.0f, label.y), kHorizontalGridColor));
            grid.append(sf::Vertex(sf::Vector2f(static_cast<float>(snapshot.canvasWidth), label.y), kHorizontalGridColor));
        }
        if (grid.getVertexCount() > 0) {
            target.draw(grid);
        }
    });

    renderManager_->addRenderCommand(ui::PanelId::Chart, ui::LayerId::Candles, [snapshot](sf::RenderTarget& target) mutable {
        const std::size_t wickVertices = snapshot.wicks.size() * 2;
        // sf::VertexArray does not support reserve(); construct with the required size instead.
        sf::VertexArray wicks(sf::Lines, wickVertices);
        std::size_t wickIndex = 0;
        for (std::size_t i = 0; i < snapshot.wicks.size(); ++i) {
            const auto& wick = snapshot.wicks[i];
            const bool bullish = i < snapshot.candles.size() ? snapshot.candles[i].bullish : true;
            const sf::Color color = bullish ? kBullishColor : kBearishColor;
            wicks[wickIndex    ].position = sf::Vector2f(wick.x, wick.top);
            wicks[wickIndex++  ].color = color;
            wicks[wickIndex    ].position = sf::Vector2f(wick.x, wick.bottom);
            wicks[wickIndex++  ].color = color;
        }
        if (wicks.getVertexCount() > 0) {
            target.draw(wicks);
        }
    });

    renderManager_->addRenderCommand(ui::PanelId::Chart, ui::LayerId::Candles, [snapshot](sf::RenderTarget& target) mutable {
        sf::RectangleShape body;
        for (const auto& candle : snapshot.candles) {
            body.setSize(sf::Vector2f(candle.halfWidth * 2.0f, candle.height));
            body.setOrigin(candle.halfWidth, 0.0f);
            body.setPosition(candle.centerX, candle.top);
            body.setFillColor(candle.bullish ? kBullishColor : kBearishColor);
            target.draw(body);
        }
    });

    renderManager_->addRenderCommand(ui::PanelId::Chart, ui::LayerId::Overlays, [snapshot](sf::RenderTarget& target) mutable {
        if (snapshot.indicators.empty() || snapshot.candles.empty()) {
            return;
        }

        auto priceToY = [&](double price) {
            const double delta = price - snapshot.visiblePriceMin;
            const double offset = delta * static_cast<double>(snapshot.pxPerPrice);
            const double y = static_cast<double>(snapshot.canvasHeight) - offset;
            return static_cast<float>(y);
        };

        for (const auto& entry : snapshot.indicators) {
            const auto& indicator = entry.second;
            if (indicator.values.size() <= snapshot.firstVisibleIndex) {
                continue;
            }

            sf::VertexArray strip(sf::LineStrip);
            for (std::size_t i = 0; i < snapshot.candles.size(); ++i) {
                const std::size_t globalIndex = snapshot.firstVisibleIndex + i;
                if (globalIndex >= indicator.values.size()) {
                    break;
                }
                const float value = indicator.values[globalIndex];
                if (!std::isfinite(value)) {
                    if (strip.getVertexCount() >= 2) {
                        target.draw(strip);
                    }
                    strip.clear();
                    continue;
                }
                const float x = snapshot.candles[i].centerX;
                const float y = priceToY(static_cast<double>(value));
                strip.append(sf::Vertex(sf::Vector2f(x, y), kIndicatorColor));
            }
            if (strip.getVertexCount() >= 2) {
                target.draw(strip);
            }
        }
    });
    const float axisTickLength = 6.0f;
    if (!snapshot.timeTicks.empty() && layout_.bottomAxis.h > 0) {
        renderManager_->addRenderCommand(ui::PanelId::BottomAxis, ui::LayerId::Axes,
            [snapshot, axisTickLength](sf::RenderTarget& target) mutable {
                sf::VertexArray ticks(sf::Lines);
                for (const auto& tick : snapshot.timeTicks) {
                    ticks.append(sf::Vertex(sf::Vector2f(tick.x1, 0.0f), kAxisColor));
                    ticks.append(sf::Vertex(sf::Vector2f(tick.x1, axisTickLength), kAxisColor));
                }
                if (ticks.getVertexCount() > 0) {
                    target.draw(ticks);
                }
            });
    }

    if (!snapshot.priceTicks.empty() && layout_.rightAxis.w > 0) {
        renderManager_->addRenderCommand(ui::PanelId::RightAxis, ui::LayerId::Axes,
            [snapshot, axisTickLength](sf::RenderTarget& target) mutable {
                sf::VertexArray ticks(sf::Lines);
                for (const auto& tick : snapshot.priceTicks) {
                    const float y = tick.y1;
                    ticks.append(sf::Vertex(sf::Vector2f(0.0f, y), kAxisColor));
                    ticks.append(sf::Vertex(sf::Vector2f(axisTickLength, y), kAxisColor));
                }
                if (ticks.getVertexCount() > 0) {
                    target.draw(ticks);
                }
            });
    }

    if (overlayFont_) {
        auto font = overlayFont_;
        if (!snapshot.timeLabels.empty() && layout_.bottomAxis.h > 0) {
            const float axisHeight = static_cast<float>(layout_.bottomAxis.h);
            const sf::Color textColor = axisTextColor_;
            const unsigned int axisFontSize = static_cast<unsigned int>(axisFontSizePx_);
            renderManager_->addRenderCommand(ui::PanelId::BottomAxis, ui::LayerId::Labels,
                [snapshot, font, textColor, axisHeight, axisFontSize](sf::RenderTarget& target) mutable {
                    sf::Text text;
                    text.setFont(*font);
                    text.setFillColor(textColor);
                    text.setCharacterSize(axisFontSize);
                    for (const auto& label : snapshot.timeLabels) {
                        text.setString(label.text);
                        const auto bounds = text.getLocalBounds();
                        text.setOrigin(bounds.left + bounds.width * 0.5f, bounds.top + bounds.height);
                        text.setPosition(label.x, axisHeight - 4.0f);
                        target.draw(text);
                    }
                });
        }

        if (!snapshot.priceLabels.empty() && layout_.rightAxis.w > 0) {
            const float axisWidth = static_cast<float>(layout_.rightAxis.w);
            const sf::Color textColor = axisTextColor_;
            const unsigned int axisFontSize = static_cast<unsigned int>(axisFontSizePx_);
            renderManager_->addRenderCommand(ui::PanelId::RightAxis, ui::LayerId::Labels,
                [snapshot, font, textColor, axisWidth, axisFontSize](sf::RenderTarget& target) mutable {
                    sf::Text text;
                    text.setFont(*font);
                    text.setFillColor(textColor);
                    text.setCharacterSize(axisFontSize);
                    for (const auto& label : snapshot.priceLabels) {
                        text.setString(label.text);
                        const auto bounds = text.getLocalBounds();
                        text.setOrigin(bounds.left + bounds.width, bounds.top + bounds.height * 0.5f);
                        text.setPosition(axisWidth - 6.0f, label.y);
                        target.draw(text);
                    }
                });
        }
    }

    if (snapshot.crosshair) {
        const auto cross = *snapshot.crosshair;
        renderManager_->addRenderCommand(ui::PanelId::Chart, ui::LayerId::Cursor,
            [cross](sf::RenderTarget& target) mutable {
                sf::VertexArray lines(sf::Lines);
                const sf::Vector2f viewSize = target.getView().getSize();
                lines.append(sf::Vertex(sf::Vector2f(cross.x, 0.0f), kCrosshairColor));
                lines.append(sf::Vertex(sf::Vector2f(cross.x, viewSize.y), kCrosshairColor));
                lines.append(sf::Vertex(sf::Vector2f(0.0f, cross.y), kCrosshairColor));
                lines.append(sf::Vertex(sf::Vector2f(viewSize.x, cross.y), kCrosshairColor));
                target.draw(lines);
            });

        if (overlayFont_ && layout_.rightAxis.w > 0) {
            auto font = overlayFont_;
            const unsigned int axisFontSize = static_cast<unsigned int>(axisFontSizePx_);
            const sf::Color textColor = axisTextColor_;
            const float axisWidth = static_cast<float>(layout_.rightAxis.w);
            renderManager_->addRenderCommand(ui::PanelId::RightAxis, ui::LayerId::Cursor,
                [cross, font, axisFontSize, textColor, axisWidth](sf::RenderTarget& target) mutable {
                    sf::Text text(cross.priceText, *font, axisFontSize);
                    text.setFillColor(textColor);
                    const auto bounds = text.getLocalBounds();
                    sf::RectangleShape background(sf::Vector2f(bounds.width + 8.0f, bounds.height + 6.0f));
                    background.setFillColor(kTooltipBgColor);
                    background.setOrigin(0.0f, 0.0f);
                    const sf::Vector2f viewSize = target.getView().getSize();
                    const float maxY = std::max(0.0f, viewSize.y - background.getSize().y);
                    const float posY = std::clamp(cross.y - background.getSize().y * 0.5f, 0.0f, maxY);
                    background.setPosition(axisWidth - background.getSize().x - 2.0f, posY);
                    target.draw(background);
                    text.setOrigin(bounds.left, bounds.top);
                    text.setPosition(background.getPosition().x + 4.0f,
                                     background.getPosition().y + 3.0f);
                    target.draw(text);
                });
        }

        if (overlayFont_ && layout_.bottomAxis.h > 0) {
            auto font = overlayFont_;
            const unsigned int axisFontSize = static_cast<unsigned int>(axisFontSizePx_);
            const sf::Color textColor = axisTextColor_;
            const float axisHeight = static_cast<float>(layout_.bottomAxis.h);
            renderManager_->addRenderCommand(ui::PanelId::BottomAxis, ui::LayerId::Cursor,
                [cross, font, axisFontSize, textColor, axisHeight](sf::RenderTarget& target) mutable {
                    sf::Text text(cross.timeText, *font, axisFontSize);
                    text.setFillColor(textColor);
                    const auto bounds = text.getLocalBounds();
                    sf::RectangleShape background(sf::Vector2f(bounds.width + 8.0f, bounds.height + 6.0f));
                    background.setFillColor(kTooltipBgColor);
                    background.setOrigin(background.getSize().x * 0.5f, 0.0f);
                    const sf::Vector2f viewSize = target.getView().getSize();
                    const float minX = background.getSize().x * 0.5f;
                    const float maxX = std::max(minX, viewSize.x - background.getSize().x * 0.5f);
                    const float clampedX = std::clamp(cross.x, minX, maxX);
                    background.setPosition(clampedX, axisHeight - background.getSize().y - 2.0f);
                    target.draw(background);
                    text.setOrigin(bounds.left, bounds.top);
                    text.setPosition(background.getPosition().x - background.getSize().x * 0.5f + 4.0f,
                                     background.getPosition().y + 3.0f);
                    target.draw(text);
                });
        }
    }

    std::string hudText;
    if (!snapshot.priceLabels.empty()) {
        const int decimals = extractDecimals(snapshot.priceLabels.front().text);
        std::ostringstream hud;
        hud << "candles=" << snapshot.visibleCount
            << "  time=" << formatTimeShort(snapshot.logicalRange.fromMs)
            << ".." << formatTimeShort(snapshot.logicalRange.toMs)
            << "  price=" << formatPriceShort(snapshot.visiblePriceMin, decimals)
            << ".." << formatPriceShort(snapshot.visiblePriceMax, decimals);
        hudText = hud.str();
    }
    else {
        std::ostringstream hud;
        hud << "candles=" << snapshot.visibleCount
            << "  time=" << formatTimeShort(snapshot.logicalRange.fromMs)
            << ".." << formatTimeShort(snapshot.logicalRange.toMs);
        hudText = hud.str();
    }

    if (!hudText.empty() && overlayFont_) {
        auto font = overlayFont_;
        const sf::Color textColor = chartTextColor_;
        renderManager_->addRenderCommand(ui::PanelId::Chart, ui::LayerId::HUD, [hudText, font, textColor](sf::RenderTarget& target) mutable {
            sf::Text hud(hudText, *font, 14);
            hud.setFillColor(textColor);
            hud.setPosition(12.0f, 10.0f);
            target.draw(hud);
        });
    }
}

}  // namespace app

#endif  // legacy-ui

#include "app/Application.h"

#include "logging/Log.h"

namespace app {

Application::Application(const config::Config& config) : config_(config) {}

Application::~Application() = default;

void Application::run() {
    LOG_INFO(logging::LogCategory::UI, "UI subsystem disabled (legacy).");
}

}  // namespace app
