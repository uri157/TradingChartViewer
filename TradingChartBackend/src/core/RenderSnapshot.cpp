#include "core/RenderSnapshot.h"

#include <utility>

namespace core {

RenderSnapshot::RenderSnapshot()
    : version_(0), lastPublishedVersion_(0) {}

RenderSnapshot::RenderSnapshot(const RenderSnapshot& other)
    : version_(0), lastPublishedVersion_(0) {
    copyFrom(other);
}

RenderSnapshot& RenderSnapshot::operator=(const RenderSnapshot& other) {
    if (this != &other) {
        copyFrom(other);
    }
    return *this;
}

RenderSnapshot::RenderSnapshot(RenderSnapshot&& other) noexcept
    : version_(0), lastPublishedVersion_(0) {
    moveFrom(std::move(other));
}

RenderSnapshot& RenderSnapshot::operator=(RenderSnapshot&& other) noexcept {
    if (this != &other) {
        moveFrom(std::move(other));
    }
    return *this;
}

void RenderSnapshot::copyFrom(const RenderSnapshot& other) {
    logicalRange = other.logicalRange;
    intervalMs = other.intervalMs;
    canvasWidth = other.canvasWidth;
    canvasHeight = other.canvasHeight;
    firstVisibleIndex = other.firstVisibleIndex;
    visibleCount = other.visibleCount;
    visiblePriceMin = other.visiblePriceMin;
    visiblePriceMax = other.visiblePriceMax;
    pxPerCandle = other.pxPerCandle;
    pxPerPrice = other.pxPerPrice;
    valid = other.valid;
    snappedToLatest = other.snappedToLatest;
    state = other.state;
    stateMessage = other.stateMessage;
    ui = other.ui;
    axes = other.axes;
    timeTicks = other.timeTicks;
    priceTicks = other.priceTicks;
    candles = other.candles;
    wicks = other.wicks;
    timeLabels = other.timeLabels;
    priceLabels = other.priceLabels;
    crosshair = other.crosshair;
    indicators = other.indicators;
    version_.store(other.version_.load(std::memory_order_relaxed), std::memory_order_relaxed);
    lastPublishedVersion_ = other.lastPublishedVersion_;
}

void RenderSnapshot::moveFrom(RenderSnapshot&& other) noexcept {
    logicalRange = other.logicalRange;
    intervalMs = other.intervalMs;
    canvasWidth = other.canvasWidth;
    canvasHeight = other.canvasHeight;
    firstVisibleIndex = other.firstVisibleIndex;
    visibleCount = other.visibleCount;
    visiblePriceMin = other.visiblePriceMin;
    visiblePriceMax = other.visiblePriceMax;
    pxPerCandle = other.pxPerCandle;
    pxPerPrice = other.pxPerPrice;
    valid = other.valid;
    snappedToLatest = other.snappedToLatest;
    state = other.state;
    stateMessage = std::move(other.stateMessage);
    ui = std::move(other.ui);
    axes = std::move(other.axes);
    timeTicks = std::move(other.timeTicks);
    priceTicks = std::move(other.priceTicks);
    candles = std::move(other.candles);
    wicks = std::move(other.wicks);
    timeLabels = std::move(other.timeLabels);
    priceLabels = std::move(other.priceLabels);
    crosshair = std::move(other.crosshair);
    indicators = std::move(other.indicators);
    version_.store(other.version_.load(std::memory_order_relaxed), std::memory_order_relaxed);
    lastPublishedVersion_ = other.lastPublishedVersion_;
    other.version_.store(0, std::memory_order_relaxed);
    other.lastPublishedVersion_ = 0;
}

}  // namespace core
