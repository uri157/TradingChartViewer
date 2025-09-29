#if 0
// TODO: legacy-ui
#include "app/ChartController.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include "core/EventBus.h"
#include "logging/Log.h"

#ifdef DIAG_SYNC
#include "core/LogUtils.h"
#endif

namespace app {
void requestDraw();
}

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

bool sameInterval(const domain::Interval& lhs, const domain::Interval& rhs) {
    if (!lhs.valid() && !rhs.valid()) {
        return true;
    }
    if (!lhs.valid() || !rhs.valid()) {
        return false;
    }
    return lhs.ms == rhs.ms;
}

}  // namespace

namespace app {

ui::PanelId whichPanel(sf::Vector2i mousePx, const ui::layout::Layout& layout) {
    auto contains = [](const ui::layout::LayoutRect& rect, sf::Vector2i p) {
        if (rect.w <= 0 || rect.h <= 0) {
            return false;
        }
        return p.x >= rect.x && p.y >= rect.y &&
               p.x < rect.x + rect.w && p.y < rect.y + rect.h;
    };

    if (contains(layout.topToolbar, mousePx)) {
        return ui::PanelId::TopToolbar;
    }
    if (contains(layout.leftSidebar, mousePx)) {
        return ui::PanelId::LeftSidebar;
    }
    if (contains(layout.rightAxis, mousePx)) {
        return ui::PanelId::RightAxis;
    }
    if (contains(layout.bottomAxis, mousePx)) {
        return ui::PanelId::BottomAxis;
    }
    if (contains(layout.chart, mousePx)) {
        return ui::PanelId::Chart;
    }
    return ui::PanelId::Chart;
}

ChartController::ChartController() {
    currentView_.minCandles = static_cast<float>(minBars_);
    currentView_.maxCandles = static_cast<float>(maxBars_);
}

ChartController::~ChartController() {
    unbindEventBus();
}

void ChartController::attachWindow(sf::RenderWindow* window) {
    window_ = window;
    if (window_) {
        lastResizeSize_ = window_->getSize();
    } else {
        lastResizeSize_.reset();
    }
    if (renderManager_ && window_) {
        renderManager_->onResize(*lastResizeSize_);
        renderManager_->markNeedsDraw();
    }
    markViewportDirty();
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
    markViewportDirty();
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
        lastResizeSize_ = window_->getSize();
        renderManager_->onResize(*lastResizeSize_);
        renderManager_->markNeedsDraw();
    }
    markViewportDirty();
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

void ChartController::setLayout(const ui::layout::Layout& layout) {
    layout_ = layout;
    markViewportDirty();
}

void ChartController::setAutoViewportEnabled(bool enabled) {
    if (autoViewportEnabled_ == enabled) {
        return;
    }
    autoViewportEnabled_ = enabled;
    clampToSeries();
    markViewportDirty();
    requestSnapshot();
}

void ChartController::resetViewportToRecent(std::size_t lastCandles) {
    if (lastCandles == 0) {
        lastCandles = 1;
    }

#ifdef DIAG_SYNC
    static core::LogRateLimiter diagResetViewportLimiter{std::chrono::milliseconds(150)};
#endif

    const std::size_t clampUpper = (maxBars_ > 0)
        ? maxBars_
        : std::numeric_limits<std::size_t>::max();
    const std::size_t minClamp = std::max<std::size_t>(minBars_, std::size_t{1});
    const std::size_t target = std::clamp<std::size_t>(std::max<std::size_t>(1, lastCandles), minClamp, clampUpper);

    if (autoViewportEnabled_) {
        currentView_.candlesVisible = target;
    }
    else {
        if (currentView_.candlesVisible == 0) {
            currentView_.candlesVisible = target;
        }
        else {
            currentView_.candlesVisible = std::clamp<std::size_t>(currentView_.candlesVisible, std::size_t{1}, clampUpper);
        }
    }

    if (series_ && !series_->empty()) {
        std::size_t effectiveTotal = series_->data.size();
        if (initialized_ && lastSeriesContinuing_) {
            // Short snapshots (e.g. publishCount capped to 120) should not permanently
            // shrink the viewport when we already had a wider view during this session.
            effectiveTotal = std::max<std::size_t>(effectiveTotal, lastSeriesTotal_);
        }
        else {
            lastSeriesTotal_ = effectiveTotal;
        }
        if (autoViewportEnabled_ && effectiveTotal > 0) {
            currentView_.candlesVisible = std::min<std::size_t>(currentView_.candlesVisible, effectiveTotal);
        }
        currentView_.rightmostOpenTime = series_->lastOpen;
        initialized_ = true;

#ifdef DIAG_SYNC
        if (diagResetViewportLimiter.allow()) {
            LOG_INFO(logging::LogCategory::UI,
                     "DIAG_SYNC resetViewport recent target=%zu finalVisible=%zu effectiveTotal=%zu lastTotal=%zu auto=%d continuing=%d rightmost=%lld",
                     target,
                     currentView_.candlesVisible,
                     effectiveTotal,
                     lastSeriesTotal_,
                     autoViewportEnabled_ ? 1 : 0,
                     lastSeriesContinuing_ ? 1 : 0,
                     static_cast<long long>(currentView_.rightmostOpenTime));
        }
#endif
    } else {
        initialized_ = false;
        currentView_.rightmostOpenTime = 0;

#ifdef DIAG_SYNC
        if (diagResetViewportLimiter.allow()) {
            LOG_INFO(logging::LogCategory::UI,
                     "DIAG_SYNC resetViewport empty target=%zu auto=%d",
                     target,
                     autoViewportEnabled_ ? 1 : 0);
        }
#endif
    }

    clampToSeries();
    markViewportDirty();
    LOG_INFO(logging::LogCategory::UI,
             "UI:viewport reset lastCandles=%zu visible=%zu",
             target,
             currentView_.candlesVisible);
    requestSnapshot();
}

void ChartController::setSeries(std::shared_ptr<const domain::CandleSeries> series) {
    const bool wasInitialized = initialized_;
    const auto previousInterval = lastSeriesInterval_;
    const std::size_t previousKnownTotal = lastSeriesTotal_;

#ifdef DIAG_SYNC
    static core::LogRateLimiter diagSetSeriesLimiter{std::chrono::milliseconds(150)};
#endif

    series_ = std::move(series);
    if (!series_ || series_->empty()) {
        initialized_ = false;
        lastSeriesLastOpen_ = 0;
        backfillInFlight_ = false;
        awaitedFirstOpen_ = std::numeric_limits<std::int64_t>::max();
        lastKnownFirstOpen_ = 0;
        lastSeriesInterval_ = {};
        lastSeriesTotal_ = 0;
        lastSeriesContinuing_ = false;
        markViewportDirty();
#ifdef DIAG_SYNC
        if (diagSetSeriesLimiter.allow()) {
            LOG_INFO(logging::LogCategory::UI,
                     "DIAG_SYNC setSeries cleared auto=%d initialized=%d",
                     autoViewportEnabled_ ? 1 : 0,
                     wasInitialized ? 1 : 0);
        }
#endif
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
    const bool continuingSession = wasInitialized && sameInterval(previousInterval, series_->interval);
    // When the backend publishes a shortened snapshot (for example capped to 120
    // candles due to publishCount), treat it as a continuation of the existing
    // session so we keep the wider viewport the user already had.
    lastSeriesContinuing_ = continuingSession;
    if (!continuingSession) {
        lastSeriesTotal_ = total;
    }
    else {
        lastSeriesTotal_ = std::max(previousKnownTotal, total);
    }
    lastSeriesInterval_ = series_->interval;

    const std::size_t available = std::max<std::size_t>(total, std::size_t{1});
    const std::size_t historical = std::max<std::size_t>(lastSeriesTotal_, available);
    const std::size_t clampUpper = (maxBars_ > 0)
        ? maxBars_
        : std::numeric_limits<std::size_t>::max();

    std::size_t minVisible{};
    std::size_t maxVisible{};
    if (autoViewportEnabled_) {
        minVisible = std::min<std::size_t>(std::max<std::size_t>(minBars_, std::size_t{1}), historical);
        maxVisible = std::min<std::size_t>(std::max<std::size_t>(clampUpper, minVisible), historical);
    }
    else {
        minVisible = std::max<std::size_t>(minBars_, std::size_t{1});
        maxVisible = std::max<std::size_t>(minVisible, std::max<std::size_t>(clampUpper, std::size_t{1}));
    }

    if (!initialized_) {
        std::size_t baseVisible = autoViewportEnabled_
            ? kDefaultVisibleCandles
            : (currentView_.candlesVisible > 0 ? currentView_.candlesVisible : kDefaultVisibleCandles);
        std::size_t targetVisible = std::clamp<std::size_t>(baseVisible, std::size_t{1}, maxVisible);
        if (autoViewportEnabled_ && historical < minVisible) {
            targetVisible = historical;
        }
        currentView_.candlesVisible = std::max<std::size_t>(targetVisible, std::size_t{1});
        currentView_.rightmostOpenTime = series_->lastOpen;
        initialized_ = true;
    }
    else {
        std::size_t desiredVisible = currentView_.candlesVisible > 0
            ? currentView_.candlesVisible
            : std::size_t{1};
        desiredVisible = std::clamp<std::size_t>(desiredVisible, minVisible, maxVisible);
        if (autoViewportEnabled_ && !continuingSession && total > 0) {
            desiredVisible = std::min<std::size_t>(desiredVisible, total);
        }
        currentView_.candlesVisible = desiredVisible;
        if (currentView_.rightmostOpenTime >= lastSeriesLastOpen_) {
            currentView_.rightmostOpenTime = series_->lastOpen;
        }
    }

    lastSeriesLastOpen_ = series_->lastOpen;
    lastKnownFirstOpen_ = series_->firstOpen;
    panAccumulator_ = 0.0;
    markViewportDirty();
    clampToSeries();

#ifdef DIAG_SYNC
    if (diagSetSeriesLimiter.allow()) {
        LOG_INFO(logging::LogCategory::UI,
                 "DIAG_SYNC setSeries total=%zu historical=%zu visible=%zu minVisible=%zu maxVisible=%zu auto=%d continuing=%d rightmost=%lld firstOpen=%lld lastOpen=%lld",
                 total,
                 lastSeriesTotal_,
                 currentView_.candlesVisible,
                 minVisible,
                 maxVisible,
                 autoViewportEnabled_ ? 1 : 0,
                 lastSeriesContinuing_ ? 1 : 0,
                 static_cast<long long>(currentView_.rightmostOpenTime),
                 static_cast<long long>(series_->firstOpen),
                 static_cast<long long>(series_->lastOpen));
    }
#endif
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

    unsigned width = 0;
    if (layout_.chart.w > 0) {
        width = static_cast<unsigned>(layout_.chart.w);
    }
    else if (window_) {
        width = window_->getSize().x;
    }
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
        markViewportDirty();
        requestSnapshot();
    }
}

void ChartController::onZoomWheel(int wheelDelta, sf::Vector2f mousePx) {
    if (wheelDelta == 0 || !series_ || series_->empty()) {
        return;
    }
    const auto interval = intervalMs();
    if (interval <= 0) {
        return;
    }
    const auto panel = whichPanel(sf::Vector2i(static_cast<int>(mousePx.x),
                                              static_cast<int>(mousePx.y)),
                                  layout_);
    if (panel != ui::PanelId::Chart) {
        return;
    }

    float width = static_cast<float>(layout_.chart.w);
    float localMouseX = mousePx.x - static_cast<float>(layout_.chart.x);
    if (!(width > 0.0f)) {
        if (!window_) {
            return;
        }
        width = static_cast<float>(window_->getSize().x);
        localMouseX = mousePx.x;
    }
    if (!(width > 0.0f)) {
        return;
    }

    const float clampedMouseX = std::clamp(localMouseX, 0.0f, width);
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
        markViewportDirty();
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
    const auto panel = whichPanel(sf::Vector2i(static_cast<int>(position.x),
                                               static_cast<int>(position.y)),
                                  layout_);
    panningInChart_ = (panel == ui::PanelId::Chart);
    if (!panningInChart_) {
        return;
    }

    const float maxX = (layout_.chart.w > 0)
        ? static_cast<float>(layout_.chart.w)
        : static_cast<float>(size.x);
    const float localX = position.x - static_cast<float>(layout_.chart.x);
    lastPanX_ = std::clamp(localX, 0.0f, maxX);
    panning_ = true;
    panButton_ = button;
}

void ChartController::onMouseButtonReleased(sf::Mouse::Button button) {
    if (panning_ && button == panButton_) {
        panning_ = false;
        panAccumulator_ = 0.0;
    }
    panningInChart_ = false;
}

void ChartController::onMouseMoved(sf::Vector2f position) {
    if (!panning_ || !panningInChart_) {
        return;
    }
    if (!window_) {
        return;
    }
    const auto size = window_->getSize();
    if (size.x == 0 || size.y == 0) {
        return;
    }
    const float local = position.x - static_cast<float>(layout_.chart.x);
    const float maxX = (layout_.chart.w > 0)
        ? static_cast<float>(layout_.chart.w)
        : static_cast<float>(size.x);
    const float clampedX = std::clamp(local, 0.0f, maxX);
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
            const float centerX = (layout_.chart.w > 0)
                ? static_cast<float>(layout_.chart.x + layout_.chart.w / 2)
                : static_cast<float>(size.x) * 0.5f;
            const float centerY = (layout_.chart.h > 0)
                ? static_cast<float>(layout_.chart.y + layout_.chart.h / 2)
                : static_cast<float>(size.y) * 0.5f;
            onZoomWheel(1, sf::Vector2f(centerX, centerY));
        }
        break;
    case sf::Keyboard::Subtract:
        if (window_) {
            const auto size = window_->getSize();
            const float centerX = (layout_.chart.w > 0)
                ? static_cast<float>(layout_.chart.x + layout_.chart.w / 2)
                : static_cast<float>(size.x) * 0.5f;
            const float centerY = (layout_.chart.h > 0)
                ? static_cast<float>(layout_.chart.y + layout_.chart.h / 2)
                : static_cast<float>(size.y) * 0.5f;
            onZoomWheel(-1, sf::Vector2f(centerX, centerY));
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

    inputSubs_.push_back(eventBus_->subscribe(sf::Event::MouseWheelScrolled, [this](const sf::Event& event) {
            if (!window_ || event.mouseWheelScroll.wheel != sf::Mouse::VerticalWheel) {
                return;
            }
            onZoomWheel(static_cast<int>(event.mouseWheelScroll.delta),
                        sf::Vector2f(static_cast<float>(event.mouseWheelScroll.x),
                                     static_cast<float>(event.mouseWheelScroll.y)));
        }));

    inputSubs_.push_back(eventBus_->subscribe(sf::Event::MouseButtonPressed, [this](const sf::Event& event) {
            onMouseButtonPressed(event.mouseButton.button,
                                 sf::Vector2f(static_cast<float>(event.mouseButton.x),
                                              static_cast<float>(event.mouseButton.y)));
        }));

    inputSubs_.push_back(eventBus_->subscribe(sf::Event::MouseButtonReleased, [this](const sf::Event& event) {
            onMouseButtonReleased(event.mouseButton.button);
        }));

    inputSubs_.push_back(eventBus_->subscribe(sf::Event::MouseMoved, [this](const sf::Event& event) {
            onMouseMoved(sf::Vector2f(static_cast<float>(event.mouseMove.x),
                                      static_cast<float>(event.mouseMove.y)));
        }));

    inputSubs_.push_back(eventBus_->subscribe(sf::Event::KeyPressed, [this](const sf::Event& event) {
            onKeyPressed(event.key.code);
        }));

    inputSubs_.push_back(eventBus_->subscribe(sf::Event::Resized, [this](const sf::Event& event) {
            const sf::Vector2u newSize{event.size.width, event.size.height};
            if (lastResizeSize_ && *lastResizeSize_ == newSize) {
                return;
            }
            lastResizeSize_ = newSize;
            if (renderManager_) {
                renderManager_->onResize(newSize);
                renderManager_->markNeedsDraw();
            }
            requestDraw();
            requestSnapshot();
        }));
}

void ChartController::unsubscribeInput() {
    inputSubs_.clear();
}

void ChartController::requestSnapshot() {
    if (!snapshotRequest_) {
        return;
    }
    if (!series_ || series_->empty()) {
        return;
    }
    snapshotRequest_();
    if (renderManager_) {
        renderManager_->markNeedsDraw();
    }
    requestDraw();
}

bool ChartController::consumeViewportDirty() {
    return viewportDirty_.exchange(false, std::memory_order_acq_rel);
}

void ChartController::markViewportDirty() {
    viewportDirty_.store(true, std::memory_order_release);
    if (renderManager_) {
        renderManager_->markNeedsDraw();
    }
    requestDraw();
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
    // Preserve the user's zoom level even if a transiently short snapshot arrives.
    const std::size_t historical = std::max<std::size_t>(std::max<std::size_t>(std::size_t{1}, lastSeriesTotal_), total);
    const std::size_t clampUpper = (maxBars_ > 0)
        ? maxBars_
        : std::numeric_limits<std::size_t>::max();

    std::size_t minVisible{};
    std::size_t maxVisible{};
    if (autoViewportEnabled_) {
        minVisible = std::min<std::size_t>(std::max<std::size_t>(minBars_, std::size_t{1}), historical);
        maxVisible = std::min<std::size_t>(std::max<std::size_t>(clampUpper, minVisible), historical);
    }
    else {
        minVisible = std::max<std::size_t>(minBars_, std::size_t{1});
        maxVisible = std::max<std::size_t>(minVisible, std::max<std::size_t>(clampUpper, std::size_t{1}));
    }
    if (maxVisible == 0) {
        maxVisible = 1;
    }

    std::size_t desired = currentView_.candlesVisible > 0
        ? currentView_.candlesVisible
        : std::size_t{1};
    desired = std::clamp<std::size_t>(desired, minVisible, maxVisible);
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

#endif  // legacy-ui

#include "app/ChartController.h"

#include <utility>

#include "core/EventBus.h"

namespace app {

ChartController::ChartController() = default;

ChartController::~ChartController() = default;

void ChartController::attachWindow(void* ) {}

void ChartController::setSeries(std::shared_ptr<const domain::CandleSeries> series) {
    series_ = std::move(series);
    viewportDirty_ = true;
    if (snapshotRequest_) {
        snapshotRequest_();
    }
}

void ChartController::setVisibleLimits(std::size_t, std::size_t) {}

void ChartController::setSnapshotRequestCallback(std::function<void()> callback) {
    snapshotRequest_ = std::move(callback);
}

void ChartController::setBackfillRequestCallback(std::function<void(std::int64_t, std::size_t)> callback) {
    backfillRequest_ = std::move(callback);
}

void ChartController::bindEventBus(core::EventBus* bus) {
    eventBus_ = bus;
}

void ChartController::unbindEventBus() {
    eventBus_ = nullptr;
}

void ChartController::setLayout(const ui::layout::Layout&) {}

void ChartController::setAutoViewportEnabled(bool) {}

void ChartController::resetViewportToRecent(std::size_t) {}

void ChartController::onPanPixels(float) {}

void ChartController::onZoomWheel(int, std::pair<float, float>) {}

void ChartController::onMouseButtonPressed(int, std::pair<float, float>) {}

void ChartController::onMouseButtonReleased(int) {}

void ChartController::onMouseMoved(std::pair<float, float>) {}

void ChartController::onKeyPressed(int) {}

bool ChartController::consumeViewportDirty() {
    const bool dirty = viewportDirty_;
    viewportDirty_ = false;
    return dirty;
}

void ChartController::clampToSeries() {
    viewportDirty_ = true;
}

void ChartController::setRenderManager(ui::RenderManager*) {}

}  // namespace app
