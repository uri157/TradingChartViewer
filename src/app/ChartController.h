#pragma once

#include <SFML/Graphics/RenderWindow.hpp>
#include <SFML/System/Vector2.hpp>
#include <SFML/Window/Event.hpp>
#include <SFML/Window/Keyboard.hpp>
#include <SFML/Window/Mouse.hpp>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <vector>

#include "core/EventBus.h"
#include "core/Viewport.h"
#include "domain/DomainContracts.h"
#include "ui/RenderManager.h"

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

    void onPanPixels(float dxPixels);
    void onZoomWheel(int wheelDelta, sf::Vector2f mousePx, sf::Vector2u canvasPx);
    void onMouseButtonPressed(sf::Mouse::Button button, sf::Vector2f position);
    void onMouseButtonReleased(sf::Mouse::Button button);
    void onMouseMoved(sf::Vector2f position);
    void onKeyPressed(sf::Keyboard::Key key);

    const core::Viewport& view() const noexcept { return currentView_; }
    void clampToSeries();
    void setRenderManager(ui::RenderManager* manager);

private:
    void subscribeInput();
    void unsubscribeInput();
    void requestSnapshot();
    void panByCandles(std::int64_t candles);
    void maybeRequestBackfill();

    sf::RenderWindow* window_{nullptr};
    core::EventBus* eventBus_{nullptr};
    std::shared_ptr<const domain::CandleSeries> series_{};
    ui::RenderManager* renderManager_{nullptr};
    core::Viewport currentView_{};
    bool initialized_{false};
    std::int64_t lastSeriesLastOpen_{0};
    std::size_t minBars_{30};
    std::size_t maxBars_{5000};
    std::function<void()> snapshotRequest_{};
    std::function<void(std::int64_t, std::size_t)> backfillRequest_{};
    struct EventSubscription {
        sf::Event::EventType type{};
        core::EventBus::CallbackID id{};
    };
    std::vector<EventSubscription> eventSubscriptions_{};
    bool panning_{false};
    sf::Mouse::Button panButton_{sf::Mouse::Left};
    float lastPanX_{0.0f};
    double panAccumulator_{0.0};
    bool backfillInFlight_{false};
    std::int64_t awaitedFirstOpen_{std::numeric_limits<std::int64_t>::max()};
    std::int64_t lastKnownFirstOpen_{0};

    [[nodiscard]] std::int64_t intervalMs() const noexcept;
    void ensureVisibleWithinSeries();
};

}  // namespace app
