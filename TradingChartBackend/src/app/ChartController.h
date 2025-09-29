#pragma once

#if 0
// TODO: legacy-ui


#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <vector>

#include "core/EventBus.h"
#include "core/Viewport.h"
#include "domain/DomainContracts.h"
#include "ui/RenderManager.h"
#include "ui/layout/LayoutEngine.h"

namespace app {

class ChartController {
public:
    ChartController();
    ~ChartController();

    void attachWindow(sf::RenderWindow* window);
    void setSeries(std::shared_ptr<const domain::CandleSeries> series);

    void setVisibleLimits(std::size_t minBars, std::size_t maxBars);
    void setSnapshotRequestCallback(std::function<void()> callback);
    void setBackfillRequestCallback(std::function<void(std::int64_t, std::size_t)> callback);
    void bindEventBus(core::EventBus* bus);
    void unbindEventBus();
    void setLayout(const ui::layout::Layout& layout);
    void setAutoViewportEnabled(bool enabled);

    void resetViewportToRecent(std::size_t lastCandles = 200);

    void onPanPixels(float dxPixels);
    void onZoomWheel(int wheelDelta, sf::Vector2f mousePx);
    void onMouseButtonPressed(sf::Mouse::Button button, sf::Vector2f position);
    void onMouseButtonReleased(sf::Mouse::Button button);
    void onMouseMoved(sf::Vector2f position);
    void onKeyPressed(sf::Keyboard::Key key);

    const core::Viewport& view() const noexcept { return currentView_; }
    bool consumeViewportDirty();
    void clampToSeries();
    void setRenderManager(ui::RenderManager* manager);

private:
    void subscribeInput();
    void unsubscribeInput();
    void requestSnapshot();
    void panByCandles(std::int64_t candles);
    void maybeRequestBackfill();
    void markViewportDirty();

    sf::RenderWindow* window_{nullptr};
    core::EventBus* eventBus_{nullptr};
    std::shared_ptr<const domain::CandleSeries> series_{};
    ui::RenderManager* renderManager_{nullptr};
    core::Viewport currentView_{};
    bool initialized_{false};
    std::int64_t lastSeriesLastOpen_{0};
    domain::Interval lastSeriesInterval_{};
    std::size_t lastSeriesTotal_{0};
    bool lastSeriesContinuing_{false};
    std::size_t minBars_{30};
    std::size_t maxBars_{5000};
    std::function<void()> snapshotRequest_{};
    std::function<void(std::int64_t, std::size_t)> backfillRequest_{};
    std::vector<core::EventBus::Subscription> inputSubs_{};
    bool panning_{false};
    sf::Mouse::Button panButton_{sf::Mouse::Left};
    float lastPanX_{0.0f};
    double panAccumulator_{0.0};
    bool backfillInFlight_{false};
    std::int64_t awaitedFirstOpen_{std::numeric_limits<std::int64_t>::max()};
    std::int64_t lastKnownFirstOpen_{0};
    ui::layout::Layout layout_{};
    bool panningInChart_{false};
    std::atomic<bool> viewportDirty_{true};
    std::optional<sf::Vector2u> lastResizeSize_{};
    bool autoViewportEnabled_{true};

    [[nodiscard]] std::int64_t intervalMs() const noexcept;
    void ensureVisibleWithinSeries();
};

ui::PanelId whichPanel(sf::Vector2i mousePx, const ui::layout::Layout& layout);

}  // namespace app

#endif  // legacy-ui

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <utility>

#include "core/Viewport.h"
#include "domain/DomainContracts.h"

namespace core {
class EventBus;
}

namespace ui {
class RenderManager;
namespace layout {
struct Layout;
}  // namespace layout
}  // namespace ui

namespace app {

class ChartController {
public:
    ChartController();
    ~ChartController();

    void attachWindow(void* /*window*/);
    void setSeries(std::shared_ptr<const domain::CandleSeries> series);

    void setVisibleLimits(std::size_t /*minBars*/, std::size_t /*maxBars*/);
    void setSnapshotRequestCallback(std::function<void()> callback);
    void setBackfillRequestCallback(std::function<void(std::int64_t, std::size_t)> callback);
    void bindEventBus(core::EventBus* bus);
    void unbindEventBus();
    void setLayout(const ui::layout::Layout& /*layout*/);
    void setAutoViewportEnabled(bool /*enabled*/);

    void resetViewportToRecent(std::size_t /*lastCandles*/ = 200);

    void onPanPixels(float /*dxPixels*/);
    void onZoomWheel(int /*wheelDelta*/, std::pair<float, float> /*mousePx*/);
    void onMouseButtonPressed(int /*button*/, std::pair<float, float> /*position*/);
    void onMouseButtonReleased(int /*button*/);
    void onMouseMoved(std::pair<float, float> /*position*/);
    void onKeyPressed(int /*key*/);

    const core::Viewport& view() const noexcept { return currentView_; }
    bool consumeViewportDirty();
    void clampToSeries();
    void setRenderManager(ui::RenderManager* /*manager*/);

private:
    std::shared_ptr<const domain::CandleSeries> series_{};
    std::function<void()> snapshotRequest_{};
    std::function<void(std::int64_t, std::size_t)> backfillRequest_{};
    core::EventBus* eventBus_{nullptr};
    core::Viewport currentView_{};
    bool viewportDirty_{true};
};

}  // namespace app

