#include "app/ChartController.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <SFML/Window/Event.hpp>

#include "core/EventBus.h"
#include "logging/Log.h"

namespace {
constexpr double kZoomBase = 1.2;
constexpr std::size_t kDefaultVisibleCandles = 300;
constexpr std::size_t kMinVisibleCandles = 5;
constexpr std::size_t kKeyboardPanStep = 10;
constexpr std::size_t kKeyboardFastPanStep = 60;
constexpr std::size_t kPanBackfillMarginCandles = 50;
constexpr std::size_t kBackfillBatchCandles = 2000;

std::int64_t inferIntervalMs(const domain::CandleSeries& series) {
    if (series.interval.valid()) {
        return series.interval.ms;
    }
    for (std::size_t i = 1; i < series.data.size(); ++i) {
        const auto delta = series.data[i].openTime - series.data[i - 1].openTime;
        if (delta > 0) {
            return delta;
        }
    }
    return 0;
}

std::size_t toSizeT(std::size_t value) {
    return value == 0 ? std::size_t{1} : value;
}

}  // namespace

namespace app {

ChartController::ChartController() {
    currentView_.minCandles = static_cast<float>(minBars_);
    currentView_.maxCandles = static_cast<float>(maxBars_);
}

ChartController::~ChartController() {
    unbindEventBus();
}

void ChartController::attachWindow(sf::RenderWindow* window) {
    window_ = window;
    if (renderManager_ && window_) {
        renderManager_->onResize(window_->getSize());
    }
}

void ChartController::setVisibleLimits(std::size_t minBars, std::size_t maxBars) {
    if (minBars == 0) {
        minBars = 1;
    }
    if (maxBars < minBars) {
        maxBars = minBars;
    }
    minBars_ = minBars;
    maxBars_ = maxBars;
    currentView_.minCandles = static_cast<float>(minBars_);
    currentView_.maxCandles = static_cast<float>(maxBars_);
    currentView_.clampVisibleRange();
    clampToSeries();
    requestSnapshot();
}

void ChartController::setSnapshotRequestCallback(std::function<void()> callback) {
    snapshotRequest_ = std::move(callback);
}

void ChartController::setBackfillRequestCallback(std::function<void(std::int64_t, std::size_t)> callback) {
    backfillRequest_ = std::move(callback);
}

void ChartController::setRenderManager(ui::RenderManager* manager) {
    renderManager_ = manager;
    if (renderManager_ && window_) {
        renderManager_->onResize(window_->getSize());
    }
}

void ChartController::bindEventBus(core::EventBus* bus) {
    if (eventBus_ == bus) {
        return;
    }
    unbindEventBus();
    eventBus_ = bus;
    subscribeInput();
}

void ChartController::unbindEventBus() {
    unsubscribeInput();
    eventBus_ = nullptr;
}

void ChartController::setSeries(std::shared_ptr<const domain::CandleSeries> series) {
    series_ = std::move(series);
    if (!series_ || series_->empty()) {
        initialized_ = false;
        lastSeriesLastOpen_ = 0;
        backfillInFlight_ = false;
        awaitedFirstOpen_ = std::numeric_limits<std::int64_t>::max();
        lastKnownFirstOpen_ = 0;
        return;
    }

    if (lastKnownFirstOpen_ == 0 || series_->firstOpen < lastKnownFirstOpen_) {
        backfillInFlight_ = false;
    }
    if (backfillInFlight_ && series_->firstOpen <= awaitedFirstOpen_) {
        backfillInFlight_ = false;
        awaitedFirstOpen_ = std::numeric_limits<std::int64_t>::max();
    }

    const std::size_t total = series_->data.size();
    const std::size_t safeTotal = std::max<std::size_t>(total, std::size_t{1});
    const std::size_t minVisible = std::min<std::size_t>(std::max<std::size_t>(minBars_, 1), safeTotal);
    const std::size_t maxVisible = std::min<std::size_t>(std::max<std::size_t>(maxBars_, minVisible), safeTotal);

    if (!initialized_) {
        std::size_t targetVisible = std::clamp<std::size_t>(kDefaultVisibleCandles, minVisible, maxVisible);
        if (safeTotal < minVisible) {
            targetVisible = safeTotal;
        }
        currentView_.candlesVisible = std::max<std::size_t>(targetVisible, std::size_t{1});
        currentView_.rightmostOpenTime = series_->lastOpen;
        initialized_ = true;
    }
    else {
        currentView_.candlesVisible = std::clamp<std::size_t>(currentView_.candlesVisible, minVisible, maxVisible);
        if (currentView_.rightmostOpenTime >= lastSeriesLastOpen_) {
            currentView_.rightmostOpenTime = series_->lastOpen;
        }
    }

    lastSeriesLastOpen_ = series_->lastOpen;
    lastKnownFirstOpen_ = series_->firstOpen;
    panAccumulator_ = 0.0;
    clampToSeries();
    requestSnapshot();
}

void ChartController::onPanPixels(float dxPixels) {
    if (!series_ || series_->empty()) {
        return;
    }
    const auto interval = intervalMs();
    if (interval <= 0) {
        return;
    }

    const unsigned width = window_ ? window_->getSize().x : 0;
    if (width == 0) {
        return;
    }

    const float pxPerCandle = static_cast<float>(width) /
                              static_cast<float>(std::max<std::size_t>(currentView_.candlesVisible, std::size_t{1}));
    if (!(pxPerCandle > 0.0f)) {
        return;
    }

    const double step = static_cast<double>(dxPixels) / static_cast<double>(pxPerCandle);
    const double total = panAccumulator_ + step;
    if (std::abs(total) < 1.0) {
        panAccumulator_ = total;
        return;
    }

    const double rawShift = (total > 0.0) ? std::floor(total) : std::ceil(total);
    const auto shift = static_cast<std::int64_t>(rawShift);
    panAccumulator_ = total - rawShift;
    if (shift == 0) {
        return;
    }

    panByCandles(shift);
}

void ChartController::panByCandles(std::int64_t candles) {
    if (candles == 0 || !series_ || series_->empty()) {
        return;
    }
    const auto interval = intervalMs();
    if (interval <= 0) {
        return;
    }

    const auto prevRight = currentView_.rightmostOpenTime;
    const auto prevVisible = currentView_.candlesVisible;
    currentView_.rightmostOpenTime -= candles * interval;
    clampToSeries();
    maybeRequestBackfill();
    if (currentView_.rightmostOpenTime != prevRight || currentView_.candlesVisible != prevVisible) {
        requestSnapshot();
    }
}

void ChartController::onZoomWheel(int wheelDelta, sf::Vector2f mousePx, sf::Vector2u canvasPx) {
    if (wheelDelta == 0 || !series_ || series_->empty()) {
        return;
    }
    const auto interval = intervalMs();
    if (interval <= 0) {
        return;
    }
    if (canvasPx.x == 0) {
        return;
    }

    const float width = static_cast<float>(canvasPx.x);
    const float clampedMouseX = std::clamp(mousePx.x, 0.0f, width);
    const float pxPerCandle = width /
        static_cast<float>(std::max<std::size_t>(currentView_.candlesVisible, std::size_t{1}));
    if (!(pxPerCandle > 0.0f)) {
        return;
    }

    const float idxFromRight = (width - clampedMouseX) / pxPerCandle;
    const auto floorIdx = static_cast<std::int64_t>(std::floor(std::max(idxFromRight, 0.0f)));

    const double factor = std::pow(kZoomBase, static_cast<double>(wheelDelta));
    if (!(factor > 0.0)) {
        return;
    }

    const float targetCandles = static_cast<float>(currentView_.candlesVisible) /
                                static_cast<float>(factor);
    std::size_t newCandles = currentView_.clampCandles(targetCandles);
    newCandles = std::clamp<std::size_t>(newCandles, std::size_t{1}, std::max<std::size_t>(1, maxBars_));
    if (newCandles == currentView_.candlesVisible) {
        return;
    }

    const auto offsetRight = static_cast<std::size_t>(std::max<std::int64_t>(floorIdx, 0));
    const std::size_t maxOffset = newCandles > 0 ? newCandles - 1 : 0;
    const std::size_t preservedOffset = std::min(offsetRight, maxOffset);

    const std::int64_t candleUnderCursor =
        currentView_.rightmostOpenTime - static_cast<std::int64_t>(offsetRight) * interval;

    const std::int64_t rightmostPrime =
        candleUnderCursor + static_cast<std::int64_t>(preservedOffset) * interval;
    const std::int64_t leftmostPrime =
        rightmostPrime - static_cast<std::int64_t>(toSizeT(newCandles) - 1) * interval;

    const auto prevRight = currentView_.rightmostOpenTime;
    const auto prevVisible = currentView_.candlesVisible;
    currentView_.candlesVisible = newCandles;
    currentView_.rightmostOpenTime = rightmostPrime;

    const std::int64_t leftAnchor = leftmostPrime;
    if (leftAnchor < series_->firstOpen) {
        currentView_.rightmostOpenTime = series_->firstOpen +
            static_cast<std::int64_t>(toSizeT(currentView_.candlesVisible) - 1) * interval;
    }

    clampToSeries();
    panAccumulator_ = 0.0;
    if (currentView_.candlesVisible != prevVisible || currentView_.rightmostOpenTime != prevRight) {
        requestSnapshot();
    }
}

void ChartController::onMouseButtonPressed(sf::Mouse::Button button, sf::Vector2f position) {
    if (button != sf::Mouse::Left && button != sf::Mouse::Middle) {
        return;
    }
    if (!window_) {
        return;
    }
    const auto size = window_->getSize();
    if (size.x == 0 || size.y == 0) {
        return;
    }
    if (position.x < 0.0f || position.y < 0.0f ||
        position.x >= static_cast<float>(size.x) || position.y >= static_cast<float>(size.y)) {
        return;
    }
    panning_ = true;
    panButton_ = button;
    lastPanX_ = position.x;
}

void ChartController::onMouseButtonReleased(sf::Mouse::Button button) {
    if (panning_ && button == panButton_) {
        panning_ = false;
        panAccumulator_ = 0.0;
    }
}

void ChartController::onMouseMoved(sf::Vector2f position) {
    if (!panning_) {
        return;
    }
    if (!window_) {
        return;
    }
    const auto size = window_->getSize();
    if (size.x == 0 || size.y == 0) {
        return;
    }
    const float clampedX = std::clamp(position.x, 0.0f, static_cast<float>(size.x));
    const float delta = clampedX - lastPanX_;
    if (std::abs(delta) > 0.0f) {
        onPanPixels(delta);
        lastPanX_ = clampedX;
    }
}

void ChartController::onKeyPressed(sf::Keyboard::Key key) {
    if (!series_ || series_->empty()) {
        return;
    }

    std::size_t step = kKeyboardPanStep;
    if (sf::Keyboard::isKeyPressed(sf::Keyboard::LShift) || sf::Keyboard::isKeyPressed(sf::Keyboard::RShift) ||
        sf::Keyboard::isKeyPressed(sf::Keyboard::LControl) || sf::Keyboard::isKeyPressed(sf::Keyboard::RControl)) {
        step = kKeyboardFastPanStep;
    }

    switch (key) {
    case sf::Keyboard::Left:
        panByCandles(static_cast<std::int64_t>(step));
        break;
    case sf::Keyboard::Right:
        panByCandles(-static_cast<std::int64_t>(step));
        break;
    case sf::Keyboard::Add:
        if (window_) {
            const auto size = window_->getSize();
            const sf::Vector2f center(static_cast<float>(size.x) * 0.5f, static_cast<float>(size.y) * 0.5f);
            onZoomWheel(1, center, size);
        }
        break;
    case sf::Keyboard::Subtract:
        if (window_) {
            const auto size = window_->getSize();
            const sf::Vector2f center(static_cast<float>(size.x) * 0.5f, static_cast<float>(size.y) * 0.5f);
            onZoomWheel(-1, center, size);
        }
        break;
    default:
        break;
    }
}

void ChartController::subscribeInput() {
    if (!eventBus_) {
        return;
    }
    unsubscribeInput();

    {
        const auto id = eventBus_->subscribe(sf::Event::MouseWheelScrolled, [this](const sf::Event& event) {
            if (!window_ || event.mouseWheelScroll.wheel != sf::Mouse::VerticalWheel) {
                return;
            }
            onZoomWheel(static_cast<int>(event.mouseWheelScroll.delta),
                        sf::Vector2f(static_cast<float>(event.mouseWheelScroll.x),
                                     static_cast<float>(event.mouseWheelScroll.y)),
                        window_->getSize());
        });
        eventSubscriptions_.emplace_back(EventSubscription{sf::Event::MouseWheelScrolled, id});
    }

    {
        const auto id = eventBus_->subscribe(sf::Event::MouseButtonPressed, [this](const sf::Event& event) {
            onMouseButtonPressed(event.mouseButton.button,
                                 sf::Vector2f(static_cast<float>(event.mouseButton.x),
                                              static_cast<float>(event.mouseButton.y)));
        });
        eventSubscriptions_.emplace_back(EventSubscription{sf::Event::MouseButtonPressed, id});
    }

    {
        const auto id = eventBus_->subscribe(sf::Event::MouseButtonReleased, [this](const sf::Event& event) {
            onMouseButtonReleased(event.mouseButton.button);
        });
        eventSubscriptions_.emplace_back(EventSubscription{sf::Event::MouseButtonReleased, id});
    }

    {
        const auto id = eventBus_->subscribe(sf::Event::MouseMoved, [this](const sf::Event& event) {
            onMouseMoved(sf::Vector2f(static_cast<float>(event.mouseMove.x),
                                      static_cast<float>(event.mouseMove.y)));
        });
        eventSubscriptions_.emplace_back(EventSubscription{sf::Event::MouseMoved, id});
    }

    {
        const auto id = eventBus_->subscribe(sf::Event::KeyPressed, [this](const sf::Event& event) {
            onKeyPressed(event.key.code);
        });
        eventSubscriptions_.emplace_back(EventSubscription{sf::Event::KeyPressed, id});
    }

    {
        const auto id = eventBus_->subscribe(sf::Event::Resized, [this](const sf::Event& event) {
            if (renderManager_) {
                renderManager_->onResize({event.size.width, event.size.height});
            }
            requestSnapshot();
        });
        eventSubscriptions_.emplace_back(EventSubscription{sf::Event::Resized, id});
    }
}

void ChartController::unsubscribeInput() {
    if (!eventBus_) {
        eventSubscriptions_.clear();
        return;
    }
    for (const auto& sub : eventSubscriptions_) {
        if (sub.id != 0) {
            eventBus_->unsubscribe(sub.type, sub.id);
        }
    }
    eventSubscriptions_.clear();
}

void ChartController::requestSnapshot() {
    if (snapshotRequest_) {
        snapshotRequest_();
    }
}

void ChartController::clampToSeries() {
    if (!series_ || series_->empty()) {
        currentView_.clampVisibleRange();
        return;
    }
    ensureVisibleWithinSeries();
}

std::int64_t ChartController::intervalMs() const noexcept {
    if (!series_ || series_->data.size() < 2) {
        if (series_ && series_->interval.valid()) {
            return series_->interval.ms;
        }
        return 0;
    }
    const auto explicitInterval = series_->interval.valid() ? series_->interval.ms : 0;
    if (explicitInterval > 0) {
        return explicitInterval;
    }
    return inferIntervalMs(*series_);
}

void ChartController::ensureVisibleWithinSeries() {
    if (!series_ || series_->empty()) {
        return;
    }

    const std::size_t total = series_->data.size();
    std::size_t minVisible = std::min<std::size_t>(std::max<std::size_t>(minBars_, 1), total);
    std::size_t maxVisible = std::min<std::size_t>(std::max<std::size_t>(maxBars_, minVisible), total);
    if (maxVisible == 0) {
        maxVisible = 1;
    }

    std::size_t desired = std::max<std::size_t>(1, currentView_.candlesVisible);
    desired = std::clamp(desired, minVisible, maxVisible);
    currentView_.candlesVisible = desired;

    const auto interval = intervalMs();
    if (interval <= 0) {
        currentView_.rightmostOpenTime = series_->lastOpen;
        LOG_DEBUG(logging::LogCategory::UI,
                  "VIEW bars=%zu spanMs=%lld first=%lld last=%lld",
                  currentView_.candlesVisible,
                  0LL,
                  static_cast<long long>(currentView_.rightmostOpenTime),
                  static_cast<long long>(currentView_.rightmostOpenTime));
        return;
    }

    const std::int64_t minRight = series_->firstOpen +
        static_cast<std::int64_t>(toSizeT(currentView_.candlesVisible) - 1) * interval;
    const std::int64_t maxRight = series_->lastOpen;
    if (maxRight < minRight) {
        currentView_.rightmostOpenTime = maxRight;
    }
    else {
        currentView_.rightmostOpenTime =
            std::clamp(currentView_.rightmostOpenTime, minRight, maxRight);
    }

    const std::int64_t span = (currentView_.candlesVisible > 1)
        ? static_cast<std::int64_t>(toSizeT(currentView_.candlesVisible) - 1) * interval
        : 0;
    const std::int64_t left = currentView_.rightmostOpenTime - span;

    LOG_DEBUG(logging::LogCategory::UI,
              "VIEW bars=%zu spanMs=%lld first=%lld last=%lld",
              currentView_.candlesVisible,
              static_cast<long long>(span),
              static_cast<long long>(left),
              static_cast<long long>(currentView_.rightmostOpenTime));
}

void ChartController::maybeRequestBackfill() {
    if (!series_ || series_->empty() || !backfillRequest_) {
        return;
    }

    const auto interval = intervalMs();
    if (interval <= 0) {
        return;
    }

    const std::int64_t span = (currentView_.candlesVisible > 1)
        ? static_cast<std::int64_t>(toSizeT(currentView_.candlesVisible) - 1) * interval
        : 0;
    const std::int64_t leftMost = currentView_.rightmostOpenTime - span;
    const std::int64_t margin = static_cast<std::int64_t>(kPanBackfillMarginCandles) * interval;
    if (leftMost > series_->firstOpen + margin) {
        return;
    }

    if (backfillInFlight_ && leftMost >= awaitedFirstOpen_) {
        return;
    }

    const std::int64_t desiredStart = std::max<std::int64_t>(0,
        series_->firstOpen - static_cast<std::int64_t>(kBackfillBatchCandles) * interval);
    if (desiredStart >= series_->firstOpen) {
        return;
    }

    backfillInFlight_ = true;
    awaitedFirstOpen_ = desiredStart;
    LOG_INFO(logging::LogCategory::UI,
             "UI PAN: requesting older history start=%lld count=%zu",
             static_cast<long long>(desiredStart),
             static_cast<std::size_t>(kBackfillBatchCandles));
    backfillRequest_(desiredStart, kBackfillBatchCandles);
}

}  // namespace app
