// Application.cpp
#include "app/Application.h"

#include <SFML/Graphics/RectangleShape.hpp>
#include <SFML/Graphics/Text.hpp>
#include <SFML/Graphics/VertexArray.hpp>
#include <SFML/Window/Event.hpp>

#include "app/SyncOrchestrator.h"
#include "bootstrap/DIContainer.h"
#include "core/SeriesCache.h"
#include "core/TimeUtils.h"
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
#include <cmath>
#include <ctime>
#include <filesystem>
#include <iomanip>
#include <optional>
#include <sstream>
#include <string>

namespace {
const sf::Color kBullishColor(48, 190, 120);
const sf::Color kBearishColor(220, 85, 85);
const sf::Color kGridColor(55, 66, 86, 120);
const sf::Color kHorizontalGridColor(58, 70, 92, 140);
const sf::Color kAxisColor(120, 130, 150);
const sf::Color kLabelColor(210, 214, 228);
const sf::Color kCrosshairColor(230, 235, 245, 200);
const sf::Color kTooltipBgColor(18, 22, 30, 220);
const sf::Color kHudColor(185, 190, 205);
const sf::Color kIndicatorColor(255, 206, 86);

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
std::string buildRepositoryPath(const config::Config& config) {
    std::filesystem::path base(config.cacheDir.empty() ? std::filesystem::path{"."} : std::filesystem::path{config.cacheDir});
    std::string fileName = config.symbol + "_" + config.interval + "_timeseries.bin";
    base /= fileName;
    return base.string();
}

domain::Interval clampInterval(domain::Interval interval) {
    if (interval.valid()) {
        return interval;
    }
    domain::Interval fallback;
    fallback.ms = core::TimeUtils::kMillisPerMinute;
    return fallback;
}
}  // namespace

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
      snapshotBuilder_(0.75f, 1.5f) {
    if (resourceProvider_) {
        overlayFont_ = resourceProvider_->getFont("ui");
    }

    chartController_.attachWindow(window_);
    chartController_.setRenderManager(renderManager_);
    chartController_.setVisibleLimits(30, 5000);
    chartController_.setSnapshotRequestCallback([this]() {
        snapshotRequested_.store(true, std::memory_order_release);
    });
    chartController_.setBackfillRequestCallback([this](std::int64_t start, std::size_t count) {
        if (orchestrator_) {
            orchestrator_->requestBackfillOlder(start, count);
        }
    });
    if (eventBus_) {
        chartController_.bindEventBus(eventBus_);
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
    timeSeriesRepo_ = std::make_shared<infra::storage::PriceDataTimeSeriesRepository>(
        buildRepositoryPath(config_),
        config_.symbol,
        repoInterval);
    indicatorCoordinator_ = std::make_shared<indicators::IndicatorCoordinator>(timeSeriesRepo_);
    snapshotBuilder_.setIndicatorCoordinator(indicatorCoordinator_);

    app::SyncConfig syncCfg;
    syncCfg.symbol = config_.symbol;
    syncCfg.interval = config_.interval;
    syncCfg.candleInterval = repoInterval;
    syncCfg.trace = config::logLevelAtLeast(config_.logLevel, config::LogLevel::Trace);
    seriesKey_ = syncCfg.symbol + "@" + syncCfg.interval;
    orchestrator_ = std::make_shared<app::SyncOrchestrator>(timeSeriesRepo_, seriesCache_, marketGateway_, eventBus_, syncCfg);
    orchestrator_->start();

    wsClient_->setDataHandler([this](const infra::storage::PriceData& data) {
        database_->updateWithNewData(data);
    });

    database_->warmupAsync();
    wsClient_->connect();

    scene_ = new ui::ChartsScene();
    window_->setFramerateLimit(60);

    if (eventBus_) {
        seriesUpdatedSubscription_ = eventBus_->subscribeSeriesUpdated([this](const core::EventBus::SeriesUpdated&) {
            pendingSeriesUpdate_.store(true, std::memory_order_release);
        });
    }
}

void Application::run() {
    window_->setMouseCursorVisible(false);
    sf::Event e;
    while (window_->isOpen()) {
        bool eventOccurred = false;
        while (window_->pollEvent(e)) {
            if (e.type == sf::Event::Closed) {
                window_->close();
            }
            else {
                eventBus_->publish(e);
                handleEvent(e);
                eventOccurred = true;
            }
        }

        if (!eventOccurred && !renderManager_->hasCommands()) {
            if (window_->waitEvent(e)) {
                if (e.type == sf::Event::Closed) {
                    window_->close();
                }
                eventBus_->publish(e);
                if (e.type != sf::Event::Closed) {
                    handleEvent(e);
                }
            }
        }

        enqueueCandleRender();
        renderManager_->render(*window_);
        window_->display();
    }
    wsClient_->disconnect();
}

Application::~Application() {
    if (eventBus_ && seriesUpdatedSubscription_ != 0) {
        eventBus_->unsubscribeSeriesUpdated(seriesUpdatedSubscription_);
        seriesUpdatedSubscription_ = 0;
    }
    chartController_.unbindEventBus();
    if (orchestrator_) {
        orchestrator_->stop();
    }
    delete scene_;
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

    const bool viewRequested = snapshotRequested_.exchange(false, std::memory_order_acq_rel);
    (void)viewRequested;

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

    app::RenderSnapshotBuilder::CursorState cursorState;
    std::optional<app::RenderSnapshotBuilder::CursorState> cursorOpt;
    const sf::Vector2i mousePixel = sf::Mouse::getPosition(*window_);
    if (mousePixel.x >= 0 && mousePixel.y >= 0 &&
        mousePixel.x < static_cast<int>(size.x) && mousePixel.y < static_cast<int>(size.y)) {
        cursorState.active = true;
        cursorState.x = static_cast<float>(mousePixel.x);
        cursorState.y = static_cast<float>(mousePixel.y);
        cursorOpt = cursorState;
    }

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
    }

    app::RenderSnapshotBuilder::StateInputs stateInputs{repoView, netView};
    std::optional<app::RenderSnapshotBuilder::StateInputs> stateInputsOpt(stateInputs);

    domain::CandleSeries emptySeries;
    if (repoView.intervalMs > 0) {
        emptySeries.interval.ms = repoView.intervalMs;
    }

    const domain::CandleSeries& buildSeries = (renderSeries && !renderSeries->empty()) ? *renderSeries : emptySeries;
    core::RenderSnapshot snapshot = snapshotBuilder_.build(buildSeries,
                                                    chartController_.view(),
                                                    size.x,
                                                    size.y,
                                                    cursorOpt,
                                                    stateInputsOpt);

    lastSnapshot_ = snapshot;

    if (snapshot.state != lastUiState_) {
        LOG_INFO(logging::LogCategory::UI,
                 "UI state changed %d -> %d",
                 static_cast<int>(lastUiState_),
                 static_cast<int>(snapshot.state));
        lastUiState_ = snapshot.state;
    }

    if (resourceProvider_) {
        ui::HUD* hud = &hud_;
        ui::ResourceProvider* rp = resourceProvider_;
        core::RenderSnapshot overlaySnapshot = snapshot;
        renderManager_->addRenderCommand(90, [hud, rp, overlaySnapshot](sf::RenderTarget& target) mutable {
            if (hud && rp) {
                hud->drawStateOverlay(target, overlaySnapshot, *rp);
            }
        });
    }

    if (!snapshot.valid) {
        return;
    }

    renderManager_->addRenderCommand(5, [snapshot](sf::RenderTarget& target) mutable {
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

    renderManager_->addRenderCommand(8, [snapshot](sf::RenderTarget& target) mutable {
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

    renderManager_->addRenderCommand(10, [snapshot](sf::RenderTarget& target) mutable {
        sf::RectangleShape body;
        for (const auto& candle : snapshot.candles) {
            body.setSize(sf::Vector2f(candle.halfWidth * 2.0f, candle.height));
            body.setOrigin(candle.halfWidth, 0.0f);
            body.setPosition(candle.centerX, candle.top);
            body.setFillColor(candle.bullish ? kBullishColor : kBearishColor);
            target.draw(body);
        }
    });

    renderManager_->addRenderCommand(11, [snapshot](sf::RenderTarget& target) mutable {
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

    renderManager_->addRenderCommand(12, [snapshot](sf::RenderTarget& target) mutable {
        const std::size_t axesVertices = snapshot.axes.size() * 2 + snapshot.timeTicks.size() * 2 + snapshot.priceTicks.size() * 2;
        // sf::VertexArray does not support reserve(); construct with the required size instead.
        sf::VertexArray axes(sf::Lines, axesVertices);
        std::size_t axisIndex = 0;
        auto appendLine = [&](sf::Vector2f start, sf::Vector2f end, sf::Color color) {
            axes[axisIndex    ].position = start;
            axes[axisIndex++  ].color = color;
            axes[axisIndex    ].position = end;
            axes[axisIndex++  ].color = color;
        };
        for (const auto& axis : snapshot.axes) {
            appendLine(sf::Vector2f(axis.x1, axis.y1), sf::Vector2f(axis.x2, axis.y2), kAxisColor);
        }
        for (const auto& tick : snapshot.timeTicks) {
            appendLine(sf::Vector2f(tick.x1, tick.y1), sf::Vector2f(tick.x2, tick.y2), kAxisColor);
        }
        for (const auto& tick : snapshot.priceTicks) {
            appendLine(sf::Vector2f(tick.x1, tick.y1), sf::Vector2f(tick.x2, tick.y2), kAxisColor);
        }
        if (axes.getVertexCount() > 0) {
            target.draw(axes);
        }
    });

    if (overlayFont_) {
        auto font = overlayFont_;
        renderManager_->addRenderCommand(15, [snapshot, font](sf::RenderTarget& target) mutable {
            sf::Text text;
            text.setFont(*font);
            text.setFillColor(kLabelColor);
            text.setCharacterSize(13);
            for (const auto& label : snapshot.timeLabels) {
                text.setString(label.text);
                const auto bounds = text.getLocalBounds();
                text.setOrigin(bounds.left + bounds.width * 0.5f, bounds.top + bounds.height);
                text.setPosition(label.x, static_cast<float>(snapshot.canvasHeight) - 6.0f);
                target.draw(text);
            }

            text.setCharacterSize(12);
            for (const auto& label : snapshot.priceLabels) {
                text.setString(label.text);
                const auto bounds = text.getLocalBounds();
                text.setOrigin(bounds.left + bounds.width, bounds.top + bounds.height * 0.5f);
                text.setPosition(static_cast<float>(snapshot.canvasWidth) - 6.0f, label.y);
                target.draw(text);
            }
        });
    }

    if (snapshot.crosshair) {
        auto font = overlayFont_;
        renderManager_->addRenderCommand(20, [snapshot, font](sf::RenderTarget& target) mutable {
            if (!snapshot.crosshair) {
                return;
            }
            const auto cross = *snapshot.crosshair;
            sf::VertexArray lines(sf::Lines);
            lines.append(sf::Vertex(sf::Vector2f(cross.x, 0.0f), kCrosshairColor));
            lines.append(sf::Vertex(sf::Vector2f(cross.x, static_cast<float>(snapshot.canvasHeight)), kCrosshairColor));
            lines.append(sf::Vertex(sf::Vector2f(0.0f, cross.y), kCrosshairColor));
            lines.append(sf::Vertex(sf::Vector2f(static_cast<float>(snapshot.canvasWidth), cross.y), kCrosshairColor));
            target.draw(lines);

            if (font) {
                sf::Text tooltip(cross.labelOHLC, *font, 14);
                tooltip.setFillColor(kLabelColor);
                const auto bounds = tooltip.getLocalBounds();
                const float margin = 8.0f;
                sf::Vector2f pos(cross.x + 12.0f, cross.y + 12.0f);
                float boxWidth = bounds.width + margin * 2.0f;
                float boxHeight = bounds.height + margin * 2.0f;
                if (pos.x + boxWidth > static_cast<float>(snapshot.canvasWidth)) {
                    pos.x = cross.x - boxWidth - 12.0f;
                }
                if (pos.y + boxHeight > static_cast<float>(snapshot.canvasHeight)) {
                    pos.y = cross.y - boxHeight - 12.0f;
                }
                sf::RectangleShape background(sf::Vector2f(boxWidth, boxHeight));
                background.setPosition(pos);
                background.setFillColor(kTooltipBgColor);
                tooltip.setOrigin(bounds.left, bounds.top);
                tooltip.setPosition(pos.x + margin, pos.y + margin);
                target.draw(background);
                target.draw(tooltip);
            }
        });
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
        renderManager_->addRenderCommand(40, [hudText, font](sf::RenderTarget& target) mutable {
            sf::Text hud(hudText, *font, 14);
            hud.setFillColor(kHudColor);
            hud.setPosition(12.0f, 10.0f);
            target.draw(hud);
        });
    }
}

}  // namespace app
