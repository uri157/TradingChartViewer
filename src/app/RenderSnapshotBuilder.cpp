#include "app/RenderSnapshotBuilder.h"

#include "core/TimeUtils.h"
#include "indicators/IndicatorCoordinator.h"
#include "logging/Log.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <limits>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace {

constexpr float kMinPxPerCandle = 3.0f;
constexpr float kTimeTickLengthPx = 6.0f;
constexpr float kPriceTickLengthPx = 6.0f;
constexpr float kAxisOffsetPx = 1.0f;
constexpr std::size_t kMinTimeLabels = 6;
constexpr std::size_t kMaxTimeLabels = 10;
constexpr float kTimeLabelMinSpacingPx = 80.0f;
constexpr float kTimeLabelTargetSpacingPx = 100.0f;
constexpr int kTargetPriceTickCount = 8;
constexpr int kMinPriceTicks = 4;
constexpr int kMaxPriceTicks = 12;
constexpr float kPriceLabelMinSpacingPx = 14.0f;
constexpr std::size_t kMinVisibleCandlesClamp = 5;

std::size_t clampFromFloat(float value, std::size_t fallback) {
    if (!std::isfinite(value)) {
        return std::max<std::size_t>(fallback, std::size_t{1});
    }
    const float safe = std::max(value, 1.0f);
    const auto rounded = static_cast<long long>(std::llround(safe));
    if (rounded <= 0) {
        return std::max<std::size_t>(fallback, std::size_t{1});
    }
    return static_cast<std::size_t>(rounded);
}

long long inferIntervalMs(const domain::CandleSeries& series) {
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

float priceToY(double price, double minPrice, float pxPerPrice, unsigned canvasHeight) {
    const double delta = price - minPrice;
    const double offset = delta * static_cast<double>(pxPerPrice);
    const double y = static_cast<double>(canvasHeight) - offset;
    return static_cast<float>(y);
}

double yToPrice(float y, double minPrice, float pxPerPrice, unsigned canvasHeight) {
    const double clampedY = std::clamp(static_cast<double>(y), 0.0, static_cast<double>(canvasHeight));
    const double offsetFromBottom = static_cast<double>(canvasHeight) - clampedY;
    return minPrice + offsetFromBottom / static_cast<double>(pxPerPrice);
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

std::string formatTimeLabel(long long timestampMs,
                            const std::tm* previous,
                            const std::tm* reference) {
    if (timestampMs <= 0) {
        return "--:--";
    }

    std::tm tm = toUtcTm(timestampMs);
    std::ostringstream oss;
    bool newDay = false;
    if (previous) {
        newDay = (previous->tm_year != tm.tm_year) || (previous->tm_yday != tm.tm_yday);
    }
    else if (reference) {
        newDay = (reference->tm_year != tm.tm_year) || (reference->tm_yday != tm.tm_yday);
    }

    if (newDay) {
        oss << std::put_time(&tm, "%Y-%m-%d %H:%M");
    }
    else {
        oss << std::put_time(&tm, "%H:%M");
    }
    return oss.str();
}

int computePriceDecimals(double step) {
    if (!(step > 0.0)) {
        return 2;
    }
    const double logValue = -std::log10(step);
    const double rounded = std::ceil(logValue);
    const int decimals = static_cast<int>(rounded) + 2;
    return std::clamp(decimals, 2, 8);
}

std::string formatPrice(double value, int decimals) {
    std::ostringstream oss;
    oss.setf(std::ios::fixed, std::ios::floatfield);
    oss << std::setprecision(std::clamp(decimals, 0, 8)) << value;
    std::string text = oss.str();

    const auto dotPos = text.find('.');
    if (dotPos != std::string::npos) {
        const std::size_t minDecimals = 2;
        const std::size_t initialDecimals = text.size() - dotPos - 1;
        if (initialDecimals > minDecimals) {
            std::size_t lastNonZero = text.find_last_not_of('0');
            const std::size_t minIndex = dotPos + minDecimals - 1;
            if (lastNonZero == std::string::npos || lastNonZero < dotPos) {
                lastNonZero = minIndex;
            }
            else if (lastNonZero < minIndex) {
                lastNonZero = minIndex;
            }
            if (lastNonZero + 1 < text.size()) {
                text.erase(lastNonZero + 1);
            }
        }
        std::size_t decimalsNow = text.size() - dotPos - 1;
        if (decimalsNow < minDecimals) {
            text.append(minDecimals - decimalsNow, '0');
            decimalsNow = minDecimals;
        }
        if (!text.empty() && text.back() == '.') {
            text.append(minDecimals, '0');
        }
    }

    return text;
}

struct PriceScale {
    std::vector<double> ticks;
    double step{0.0};
    int decimals{2};
};

double roundTo125(double value) {
    if (!(value > 0.0)) {
        return 0.0;
    }
    const double exponent = std::floor(std::log10(value));
    const double base = value / std::pow(10.0, exponent);
    double chosen = 1.0;
    if (base <= 1.0) {
        chosen = 1.0;
    }
    else if (base <= 2.0) {
        chosen = 2.0;
    }
    else if (base <= 5.0) {
        chosen = 5.0;
    }
    else {
        return std::pow(10.0, exponent + 1.0);
    }
    return chosen * std::pow(10.0, exponent);
}

double next125(double value) {
    if (!(value > 0.0)) {
        return 0.0;
    }
    double exponent = std::floor(std::log10(value));
    double base = value / std::pow(10.0, exponent);
    if (base < 1.0) {
        base = 1.0;
    }
    if (base < 2.0) {
        return 2.0 * std::pow(10.0, exponent);
    }
    if (base < 5.0) {
        return 5.0 * std::pow(10.0, exponent);
    }
    if (base < 10.0) {
        return 10.0 * std::pow(10.0, exponent);
    }
    return std::pow(10.0, exponent + 1.0);
}

double prev125(double value) {
    if (!(value > 0.0)) {
        return 0.0;
    }
    double exponent = std::floor(std::log10(value));
    double base = value / std::pow(10.0, exponent);
    constexpr double epsilon = 1e-9;
    if (base <= 1.0 + epsilon) {
        exponent -= 1.0;
        return 5.0 * std::pow(10.0, exponent);
    }
    if (base <= 2.0 + epsilon) {
        return 1.0 * std::pow(10.0, exponent);
    }
    if (base <= 5.0 + epsilon) {
        return 2.0 * std::pow(10.0, exponent);
    }
    if (base <= 10.0 + epsilon) {
        return 5.0 * std::pow(10.0, exponent);
    }
    exponent += 1.0;
    return 1.0 * std::pow(10.0, exponent);
}

PriceScale buildPriceScale(double minPrice, double maxPrice) {
    PriceScale scale;
    if (!(maxPrice > minPrice)) {
        return scale;
    }

    const double range = maxPrice - minPrice;
    const double roughStep = range / static_cast<double>(std::max(kTargetPriceTickCount, 1));
    const double safeRough = std::max(roughStep, 1e-9);
    double stepCandidate = roundTo125(safeRough);
    std::vector<double> candidates{stepCandidate};
    double probe = stepCandidate;
    for (int i = 0; i < 5; ++i) {
        probe = next125(probe);
        if (probe <= 0.0) {
            break;
        }
        candidates.push_back(probe);
    }
    probe = stepCandidate;
    for (int i = 0; i < 5; ++i) {
        const double prev = prev125(probe);
        if (!(prev > 0.0)) {
            break;
        }
        candidates.push_back(prev);
        probe = prev;
    }

    double bestStep = stepCandidate;
    double bestScore = std::numeric_limits<double>::max();
    std::size_t bestCount = 0;

    for (double step : candidates) {
        if (!(step > 0.0)) {
            continue;
        }
        const double startTick = std::floor(minPrice / step) * step;
        std::size_t tickCount = 0;
        double value = startTick;
        const double epsilon = step * 0.5;
        while (value <= maxPrice + epsilon && tickCount < 500) {
            if (value >= minPrice - epsilon) {
                ++tickCount;
            }
            value += step;
        }
        if (tickCount == 0) {
            continue;
        }
        if (tickCount >= static_cast<std::size_t>(kMinPriceTicks) &&
            tickCount <= static_cast<std::size_t>(kMaxPriceTicks)) {
            bestStep = step;
            bestCount = tickCount;
            break;
        }
        const double diff = std::abs(static_cast<double>(tickCount) - static_cast<double>(kTargetPriceTickCount));
        if (diff < bestScore) {
            bestScore = diff;
            bestStep = step;
            bestCount = tickCount;
        }
    }

    const double startTick = std::floor(minPrice / bestStep) * bestStep;
    const double epsilon = bestStep * 0.5;
    for (double value = startTick; value <= maxPrice + epsilon; value += bestStep) {
        if (value >= minPrice - epsilon) {
            scale.ticks.push_back(value);
        }
        if (scale.ticks.size() > 500) {
            break;
        }
    }

    if (scale.ticks.size() < static_cast<std::size_t>(kMinPriceTicks) && bestCount > 0) {
        const double extraStart = startTick - bestStep;
        if (extraStart <= maxPrice + epsilon) {
            scale.ticks.insert(scale.ticks.begin(), extraStart);
        }
    }

    scale.step = bestStep;
    scale.decimals = computePriceDecimals(bestStep);
    return scale;
}

std::size_t chooseTimeStep(std::size_t visibleCount, float pxPerCandle) {
    if (visibleCount == 0 || pxPerCandle <= 0.0f) {
        return 1;
    }

    const double minSpacingCandles = kTimeLabelTargetSpacingPx / std::max(pxPerCandle, 1e-3f);
    const double minCandles = std::max(minSpacingCandles, 1.0);
    const double targetCandles = static_cast<double>(visibleCount) / 8.0;
    double roughStep = std::max(minCandles, targetCandles);
    if (roughStep < 1.0) {
        roughStep = 1.0;
    }

    auto countForStep = [&](double step) -> std::size_t {
        if (!(step > 0.0)) {
            return visibleCount;
        }
        return static_cast<std::size_t>(std::floor(static_cast<double>(visibleCount) / step)) + 1;
    };

    double candidate = roundTo125(roughStep);
    if (candidate < 1.0) {
        candidate = 1.0;
    }

    while (candidate > 1.0 && countForStep(candidate) < kMinTimeLabels) {
        const double prev = prev125(candidate);
        if (!(prev >= 1.0)) {
            candidate = 1.0;
            break;
        }
        candidate = prev;
    }

    while (countForStep(candidate) > kMaxTimeLabels) {
        const double next = next125(candidate);
        if (!(next > candidate)) {
            break;
        }
        candidate = next;
    }

    std::size_t step = static_cast<std::size_t>(std::max<double>(std::round(candidate), 1.0));
    const float spacingPx = pxPerCandle * static_cast<float>(step);
    if (spacingPx < kTimeLabelMinSpacingPx) {
        const double adjusted = roundTo125(std::ceil(kTimeLabelMinSpacingPx / std::max(pxPerCandle, 1.0f)));
        step = static_cast<std::size_t>(std::max<double>(adjusted, static_cast<double>(step)));
    }

    return std::max<std::size_t>(step, 1);
}

struct SnapshotLogState {
    std::size_t candleCount{0};
    double min{0.0};
    double max{0.0};
    float pxPerCandle{0.0f};
    bool initialized{false};
};

SnapshotLogState& logState() {
    static SnapshotLogState state;
    return state;
}

}  // namespace

namespace app {

RenderSnapshotBuilder::RenderSnapshotBuilder(float candleWidthRatio, float minBodyHeight)
    : candleWidthRatio_(std::clamp(candleWidthRatio, 0.1f, 1.0f)),
      minBodyHeight_(std::max(minBodyHeight, 0.5f)) {}

void RenderSnapshotBuilder::setIndicatorCoordinator(std::shared_ptr<indicators::IndicatorCoordinator> coordinator) {
    indicatorCoordinator_ = std::move(coordinator);
}

core::RenderSnapshot RenderSnapshotBuilder::build(const std::vector<core::RenderCandleData>& series,
                                            const ViewportParams& viewport,
                                            const std::optional<CursorState>& cursor,
                                            const std::optional<StateInputs>& stateInputs) const {
    core::RenderSnapshot snapshot;
    snapshot.canvasWidth = viewport.canvasWidth;
    snapshot.canvasHeight = viewport.canvasHeight;

    if (viewport.canvasWidth == 0 || viewport.canvasHeight == 0) {
        LOG_WARN(logging::LogCategory::SNAPSHOT,
                 "Snapshot aborted: empty canvas %ux%u",
                 viewport.canvasWidth,
                 viewport.canvasHeight);
        snapshot.valid = false;
        applyUiState(snapshot, stateInputs);
        return snapshot;
    }

    if (series.empty()) {
        LOG_WARN(logging::LogCategory::SNAPSHOT, "Snapshot aborted: series empty after backfill");
        snapshot.valid = false;
        applyUiState(snapshot, stateInputs);
        return snapshot;
    }

    const std::size_t total = series.size();
    LOG_TRACE(logging::LogCategory::SNAPSHOT,
              "Snapshot build request: total=%zu first=%zu desired=%zu canvas=(%u,%u)",
              total,
              static_cast<std::size_t>(viewport.firstIndex),
              static_cast<std::size_t>(viewport.visibleCount),
              viewport.canvasWidth,
              viewport.canvasHeight);
    const float canvasWidth = static_cast<float>(viewport.canvasWidth);
    const float canvasHeight = static_cast<float>(viewport.canvasHeight);

    const std::size_t maxVisibleByWidth = std::max<std::size_t>(
        1, static_cast<std::size_t>(std::floor(canvasWidth / kMinPxPerCandle)));

    const std::size_t fallbackCount = viewport.visibleCount > 0 ? viewport.visibleCount : total;
    std::size_t minVisible = clampFromFloat(viewport.minVisibleCandles, fallbackCount);
    std::size_t maxVisible = clampFromFloat(viewport.maxVisibleCandles, fallbackCount);
    if (minVisible > maxVisible) {
        std::swap(minVisible, maxVisible);
    }
    minVisible = std::max<std::size_t>(1, minVisible);
    maxVisible = std::max(minVisible, maxVisible);

    std::size_t desiredCount = fallbackCount;
    desiredCount = std::max<std::size_t>(1, desiredCount);
    desiredCount = std::min(desiredCount, total);
    desiredCount = std::clamp(desiredCount, minVisible, maxVisible);
    desiredCount = std::min(desiredCount, maxVisibleByWidth);
    desiredCount = std::max<std::size_t>(1, std::min(desiredCount, total));

    if (desiredCount == 0) {
        snapshot.valid = false;
        applyUiState(snapshot, stateInputs);
        return snapshot;
    }

    const std::size_t maxStart = total > desiredCount ? total - desiredCount : 0;
    std::size_t startIndex = viewport.snapToLatest ? maxStart : std::min(viewport.firstIndex, maxStart);
    if (startIndex >= total) {
        startIndex = maxStart;
    }
    const std::size_t tentativeEnd = startIndex + desiredCount;
    const std::size_t endIndex = std::min(tentativeEnd, total);
    if (endIndex <= startIndex) {
        LOG_WARN(logging::LogCategory::SNAPSHOT,
                 "Snapshot aborted: invalid range start=%zu end=%zu total=%zu",
                 startIndex,
                 endIndex,
                 total);
        snapshot.valid = false;
        applyUiState(snapshot, stateInputs);
        return snapshot;
    }
    const std::size_t actualCount = endIndex - startIndex;

    snapshot.firstVisibleIndex = startIndex;
    snapshot.visibleCount = actualCount;
    snapshot.snappedToLatest = (startIndex == maxStart);

    double minPrice = std::numeric_limits<double>::max();
    double maxPrice = std::numeric_limits<double>::lowest();
    for (std::size_t i = startIndex; i < endIndex; ++i) {
        const auto& candle = series[i];
        minPrice = std::min({minPrice, candle.low, candle.open, candle.close});
        maxPrice = std::max({maxPrice, candle.high, candle.open, candle.close});
    }

    if (!(maxPrice > minPrice)) {
        const double reference = (maxPrice + minPrice) * 0.5;
        const double base = std::max(std::abs(reference), 1.0);
        const double padding = std::max(base * 0.001, 1e-6);
        minPrice = reference - padding;
        maxPrice = reference + padding;
    }
    else {
        const double range = maxPrice - minPrice;
        const double base = std::max({std::abs(maxPrice), std::abs(minPrice), 1.0});
        const double padding = std::max(range * 0.001, base * 1e-6);
        minPrice -= padding;
        maxPrice += padding;
    }

    const double priceRange = maxPrice - minPrice;
    if (!(priceRange > 0.0)) {
        snapshot.valid = false;
        applyUiState(snapshot, stateInputs);
        return snapshot;
    }

    snapshot.visiblePriceMin = minPrice;
    snapshot.visiblePriceMax = maxPrice;
    snapshot.pxPerCandle = std::max(kMinPxPerCandle, canvasWidth / static_cast<float>(actualCount));
    if (!std::isfinite(snapshot.pxPerCandle) || snapshot.pxPerCandle <= 0.0f) {
        snapshot.pxPerCandle = kMinPxPerCandle;
    }
    snapshot.pxPerPrice = static_cast<float>(viewport.canvasHeight) / static_cast<float>(priceRange);
    if (!std::isfinite(snapshot.pxPerPrice) || snapshot.pxPerPrice <= 0.0f) {
        LOG_WARN(logging::LogCategory::SNAPSHOT,
                 "Snapshot aborted: invalid pxPerPrice=%.6f priceRange=%.6f",
                 static_cast<double>(snapshot.pxPerPrice),
                 priceRange);
        snapshot.valid = false;
        applyUiState(snapshot, stateInputs);
        return snapshot;
    }
    snapshot.intervalMs = estimateIntervalMs(series, startIndex, actualCount);
    snapshot.logicalRange.fromMs = series[startIndex].openTimeMs;
    snapshot.logicalRange.toMs = series[endIndex - 1].openTimeMs;
    if (snapshot.intervalMs > 0) {
        snapshot.logicalRange.toMs += snapshot.intervalMs;
    }

    snapshot.axes.reserve(2);
    snapshot.axes.push_back(core::RenderSnapshot::AxisLine{0.0f, canvasHeight - kAxisOffsetPx, canvasWidth, canvasHeight - kAxisOffsetPx});
    snapshot.axes.push_back(core::RenderSnapshot::AxisLine{canvasWidth - kAxisOffsetPx, 0.0f, canvasWidth - kAxisOffsetPx, canvasHeight});

    snapshot.candles.reserve(actualCount);
    snapshot.wicks.reserve(actualCount);
    for (std::size_t localIndex = 0; localIndex < actualCount; ++localIndex) {
        const auto& candle = series[startIndex + localIndex];
        const float centerX = snapshot.pxPerCandle * (0.5f + static_cast<float>(localIndex));
        const float openY = priceToY(candle.open, minPrice, snapshot.pxPerPrice, viewport.canvasHeight);
        const float closeY = priceToY(candle.close, minPrice, snapshot.pxPerPrice, viewport.canvasHeight);
        const float top = std::min(openY, closeY);
        const float rawHeight = std::abs(openY - closeY);
        const float height = std::max(rawHeight, minBodyHeight_);
        const float fullWidth = snapshot.pxPerCandle * candleWidthRatio_;
        const float halfWidth = fullWidth * 0.5f;

        const float wickTop = priceToY(candle.high, minPrice, snapshot.pxPerPrice, viewport.canvasHeight);
        const float wickBottom = priceToY(candle.low, minPrice, snapshot.pxPerPrice, viewport.canvasHeight);

        snapshot.candles.push_back(core::RenderSnapshot::CandleRect{centerX, top, height, halfWidth, candle.close >= candle.open});
        snapshot.wicks.push_back(core::RenderSnapshot::CandleWick{centerX, wickTop, wickBottom});
    }

    const PriceScale priceScale = buildPriceScale(snapshot.visiblePriceMin, snapshot.visiblePriceMax);
    snapshot.priceLabels.reserve(priceScale.ticks.size());
    snapshot.priceTicks.reserve(priceScale.ticks.size());
    float lastPriceLabelY = std::numeric_limits<float>::quiet_NaN();
    for (double tick : priceScale.ticks) {
        const float y = priceToY(tick, snapshot.visiblePriceMin, snapshot.pxPerPrice, viewport.canvasHeight);
        if (y < -kPriceLabelMinSpacingPx || y > static_cast<float>(viewport.canvasHeight) + kPriceLabelMinSpacingPx) {
            continue;
        }
        if (!std::isnan(lastPriceLabelY) && std::abs(lastPriceLabelY - y) < kPriceLabelMinSpacingPx) {
            continue;
        }
        lastPriceLabelY = y;
        snapshot.priceTicks.push_back(core::RenderSnapshot::AxisTick{canvasWidth, y, canvasWidth - kPriceTickLengthPx, y});
        snapshot.priceLabels.push_back(core::RenderSnapshot::PriceLabel{y, formatPrice(tick, priceScale.decimals)});
    }

    const std::size_t timeStep = chooseTimeStep(desiredCount, snapshot.pxPerCandle);
    snapshot.timeLabels.reserve((desiredCount + timeStep - 1) / timeStep + 2);
    snapshot.timeTicks.reserve(snapshot.timeLabels.capacity());

    long long baseIntervalMs = snapshot.intervalMs;
    if (baseIntervalMs <= 0 && desiredCount > 1) {
        const long long delta = series[startIndex + 1].openTimeMs - series[startIndex].openTimeMs;
        if (delta > 0) {
            baseIntervalMs = delta;
        }
    }
    if (baseIntervalMs <= 0) {
        baseIntervalMs = 1;
    }

    const long long stepMs = baseIntervalMs * static_cast<long long>(std::max<std::size_t>(timeStep, 1));
    const long long firstTime = series[startIndex].openTimeMs;
    const long long lastTime = series[endIndex - 1].openTimeMs;
    long long origin = (firstTime / stepMs) * stepMs;
    if (origin > firstTime) {
        origin -= stepMs;
    }
    long long tickTime = origin;
    while (tickTime < firstTime) {
        tickTime += stepMs;
    }

    std::tm previousLabelTm{};
    bool hasPreviousLabel = false;
    float lastTimeLabelX = std::numeric_limits<float>::quiet_NaN();
    const double firstTimeD = static_cast<double>(firstTime);
    const double intervalD = static_cast<double>(baseIntervalMs);
    const std::tm firstLabelTm = toUtcTm(firstTime);
    while (tickTime <= lastTime) {
        const double offsetCandles = (static_cast<double>(tickTime) - firstTimeD) / intervalD;
        const float x = snapshot.pxPerCandle * static_cast<float>(offsetCandles + 0.5);
        if (x < -kTimeLabelMinSpacingPx || x > canvasWidth + kTimeLabelMinSpacingPx) {
            tickTime += stepMs;
            continue;
        }
        if (!std::isnan(lastTimeLabelX) && std::abs(x - lastTimeLabelX) < kTimeLabelMinSpacingPx) {
            tickTime += stepMs;
            continue;
        }

        const std::string text = formatTimeLabel(tickTime,
                                                hasPreviousLabel ? &previousLabelTm : nullptr,
                                                &firstLabelTm);
        previousLabelTm = toUtcTm(tickTime);
        hasPreviousLabel = true;
        lastTimeLabelX = x;
        snapshot.timeTicks.push_back(core::RenderSnapshot::AxisTick{x, canvasHeight, x, canvasHeight - kTimeTickLengthPx});
        snapshot.timeLabels.push_back(core::RenderSnapshot::TimeLabel{x, text});

        tickTime += stepMs;
    }

    if (snapshot.timeLabels.empty()) {
        const float x = snapshot.pxPerCandle * 0.5f;
        const std::string text = formatTimeLabel(firstTime, nullptr, &firstLabelTm);
        snapshot.timeTicks.push_back(core::RenderSnapshot::AxisTick{x, canvasHeight, x, canvasHeight - kTimeTickLengthPx});
        snapshot.timeLabels.push_back(core::RenderSnapshot::TimeLabel{x, text});
    }

    const int tooltipDecimals = priceScale.ticks.empty()
        ? computePriceDecimals(priceRange / static_cast<double>(std::max<std::size_t>(desiredCount, 1)))
        : priceScale.decimals;

    if (cursor && cursor->active) {
        const auto& c = *cursor;
        if (c.x >= 0.0f && c.x <= canvasWidth && c.y >= 0.0f && c.y <= canvasHeight) {
            const float indexF = std::clamp(c.x / snapshot.pxPerCandle, 0.0f, static_cast<float>(desiredCount - 1));
            const std::size_t index = static_cast<std::size_t>(indexF);
            const auto& candle = series[startIndex + index];
            core::RenderSnapshot::Crosshair crosshair;
            crosshair.x = snapshot.pxPerCandle * (0.5f + static_cast<float>(index));
            crosshair.y = std::clamp(c.y, 0.0f, canvasHeight);
            crosshair.timeMs = candle.openTimeMs;
            crosshair.timeText = formatTimeLabel(candle.openTimeMs, nullptr, nullptr);
            crosshair.price = yToPrice(crosshair.y, snapshot.visiblePriceMin, snapshot.pxPerPrice, viewport.canvasHeight);
            crosshair.priceText = formatPrice(crosshair.price, tooltipDecimals);

            std::ostringstream oss;
            oss << "t: " << formatTimeLabel(candle.openTimeMs, nullptr, nullptr)
                << " O: " << formatPrice(candle.open, tooltipDecimals)
                << " H: " << formatPrice(candle.high, tooltipDecimals)
                << " L: " << formatPrice(candle.low, tooltipDecimals)
                << " C: " << formatPrice(candle.close, tooltipDecimals);
            crosshair.labelOHLC = oss.str();
            snapshot.crosshair = std::move(crosshair);
        }
    }

    snapshot.valid = !snapshot.candles.empty();

    if (snapshot.valid) {
        auto& state = logState();
        const bool changed = !state.initialized || state.candleCount != snapshot.candles.size() ||
            std::abs(state.min - snapshot.visiblePriceMin) > 1e-4 ||
            std::abs(state.max - snapshot.visiblePriceMax) > 1e-4 ||
            std::abs(state.pxPerCandle - snapshot.pxPerCandle) > 1e-3;
        if (changed) {
            LOG_INFO(logging::LogCategory::SNAPSHOT,
                     "candlesVisible=%zu priceRange=%.6f-%.6f pxPerCandle=%.3f",
                     static_cast<std::size_t>(snapshot.candles.size()),
                     snapshot.visiblePriceMin,
                     snapshot.visiblePriceMax,
                     snapshot.pxPerCandle);
            state.candleCount = snapshot.candles.size();
            state.min = snapshot.visiblePriceMin;
            state.max = snapshot.visiblePriceMax;
            state.pxPerCandle = snapshot.pxPerCandle;
            state.initialized = true;
        }
        LOG_DEBUG(logging::LogCategory::SNAPSHOT,
                  "SNAPSHOT built candles=%zu gridH=%zu gridV=%zu labelsY=%zu labelsX=%zu x=[%lld,%lld]",
                  snapshot.candles.size(),
                  snapshot.priceTicks.size(),
                  snapshot.timeTicks.size(),
                  snapshot.priceLabels.size(),
                  snapshot.timeLabels.size(),
                  static_cast<long long>(snapshot.logicalRange.fromMs),
                  static_cast<long long>(snapshot.logicalRange.toMs));
    }

    LOG_INFO(logging::LogCategory::SNAPSHOT,
             "Post-backfill: candles=%zu window=[%zu,%zu) pxPerCandle=%.3f",
             total,
             snapshot.firstVisibleIndex,
             snapshot.firstVisibleIndex + snapshot.visibleCount,
             snapshot.pxPerCandle);

    applyUiState(snapshot, stateInputs);
    return snapshot;
}

core::RenderSnapshot RenderSnapshotBuilder::build(const domain::CandleSeries& series,
                                            const core::Viewport& view,
                                            unsigned canvasWidth,
                                            unsigned canvasHeight,
                                            const std::optional<CursorState>& cursor,
                                            const std::optional<StateInputs>& stateInputs) const {
    core::RenderSnapshot snapshot;
    snapshot.canvasWidth = canvasWidth;
    snapshot.canvasHeight = canvasHeight;

    if (canvasWidth == 0 || canvasHeight == 0) {
        LOG_WARN(logging::LogCategory::SNAPSHOT,
                 "Snapshot aborted: invalid canvas %ux%u",
                 canvasWidth,
                 canvasHeight);
        snapshot.valid = false;
        applyUiState(snapshot, stateInputs);
        return snapshot;
    }

    if (series.data.empty()) {
        LOG_WARN(logging::LogCategory::SNAPSHOT, "Snapshot aborted: repository returned empty series");
        snapshot.valid = false;
        applyUiState(snapshot, stateInputs);
        return snapshot;
    }

    std::vector<core::RenderCandleData> renderSeries;
    renderSeries.reserve(series.data.size());
    for (const auto& candle : series.data) {
        renderSeries.push_back(core::RenderCandleData{candle.openTime, candle.open, candle.high, candle.low, candle.close});
    }

    const std::size_t total = renderSeries.size();
    LOG_TRACE(logging::LogCategory::SNAPSHOT,
              "Snapshot domain build: total=%zu reqVisible=%zu rightmost=%lld",
              total,
              static_cast<std::size_t>(view.candlesVisible),
              static_cast<long long>(view.rightmostOpenTime));
    RenderSnapshotBuilder::ViewportParams params;
    params.canvasWidth = canvasWidth;
    params.canvasHeight = canvasHeight;
    params.visibleCount = view.candlesVisible;
    params.minVisibleCandles = view.minCandles;
    params.maxVisibleCandles = view.maxCandles;

    const long long interval = inferIntervalMs(series);

    std::size_t rightIndex = total > 0 ? total - 1 : 0;
    if (interval > 0) {
        auto upper = std::upper_bound(series.data.begin(), series.data.end(), view.rightmostOpenTime,
                                      [](std::int64_t value, const domain::Candle& candle) {
                                          return value < candle.openTime;
                                      });
        if (upper == series.data.begin()) {
            rightIndex = 0;
        }
        else {
            --upper;
            rightIndex = static_cast<std::size_t>(std::distance(series.data.begin(), upper));
        }
    }

    params.snapToLatest = (rightIndex >= total ? false : rightIndex == total - 1);

    const std::size_t requestedVisible = std::max<std::size_t>(params.visibleCount, kMinVisibleCandlesClamp);
    const std::size_t desired = std::max<std::size_t>(1, std::min<std::size_t>(requestedVisible, total));
    const std::size_t startIndex = rightIndex + 1 >= desired ? rightIndex + 1 - desired : 0;
    params.firstIndex = startIndex;
    params.visibleCount = desired;

    snapshot = build(renderSeries, params, cursor, stateInputs);

    if (!snapshot.valid) {
        LOG_WARN(logging::LogCategory::SNAPSHOT,
                 "Snapshot invalid after build: total=%zu visible=%zu", total, desired);
        mergeIndicators(snapshot, series, stateInputs);
        return snapshot;
    }

    mergeIndicators(snapshot, series, stateInputs);

    bool partial = false;
    if (interval > 0) {
        const std::size_t requestedCount = std::max<std::size_t>(1, view.candlesVisible);
        const long long requestedSpan = static_cast<long long>(requestedCount - 1) * interval;
        const long long intendedLeft = view.rightmostOpenTime - requestedSpan;
        if (series.firstOpen > intendedLeft) {
            partial = true;
        }
        if (view.rightmostOpenTime > series.lastOpen) {
            partial = true;
        }
    }

    if (partial && snapshot.state == core::UiState::Live) {
        if (!snapshot.stateMessage.empty()) {
            snapshot.stateMessage += " (parcial)";
        } else {
            snapshot.stateMessage = "Parcial";
        }
    }
    return snapshot;
}

void RenderSnapshotBuilder::mergeIndicators(core::RenderSnapshot& snapshot,
                                           const domain::CandleSeries& series,
                                           const std::optional<StateInputs>& stateInputs) const {
    snapshot.indicators.clear();
    if (!indicatorCoordinator_) {
        return;
    }
    if (!snapshot.valid) {
        return;
    }
    if (!stateInputs) {
        return;
    }

    if (series.data.empty()) {
        LOG_WARN(logging::LogCategory::SNAPSHOT, "Skipping indicators: candle series empty");
        return;
    }

    const auto& repoView = stateInputs->repo;
    if (repoView.seriesKey.empty()) {
        LOG_TRACE(logging::LogCategory::SNAPSHOT, "Skipping indicators: repo seriesKey empty");
        return;
    }

    if (repoView.candleCount == 0) {
        LOG_TRACE(logging::LogCategory::SNAPSHOT,
                  "Skipping indicators: repo candle count is zero for %s",
                  repoView.seriesKey.c_str());
        return;
    }

    const indicators::SeriesVersion version{repoView.lastClosedOpenTime, repoView.candleCount};
    const indicators::EmaParams ema20{20};
    auto emaPtr = indicatorCoordinator_->getEMA(repoView.seriesKey,
                                                series,
                                                version,
                                                ema20,
                                                /*allowAsyncRecompute=*/true);
    if (emaPtr && emaPtr->values.size() == series.data.size()) {
        snapshot.indicators[emaPtr->id] = *emaPtr;
    } else if (emaPtr) {
        LOG_WARN(logging::LogCategory::SNAPSHOT,
                 "Indicator size mismatch: %zu vs candles=%zu",
                 static_cast<std::size_t>(emaPtr->values.size()),
                 static_cast<std::size_t>(series.data.size()));
    }
}

core::UiState RenderSnapshotBuilder::computeUiState(const RepoView& repo,
                                             const ConnectivityView& net,
                                             std::string& outMsg) const {
    const auto nowMs = static_cast<long long>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch())
            .count());

    const bool haveData = repo.candleCount > 0;
    const long long interval = repo.intervalMs > 0 ? repo.intervalMs : core::TimeUtils::kMillisPerMinute;

    if (!haveData) {
        if (net.backfilling) {
            outMsg = "Cargando histórico…";
            return core::UiState::Loading;
        }
        if (!net.wsConnected) {
            outMsg = "Conectando…";
            return core::UiState::Loading;
        }
        if (net.lastTickMs <= 0) {
            outMsg = "Esperando datos en vivo…";
            return core::UiState::Loading;
        }
        outMsg = "Sin datos disponibles.";
        return core::UiState::NoData;
    }

    if (!net.wsConnected) {
        outMsg = net.backfilling ? "Cargando histórico…" : "Conectando flujo en vivo…";
        return core::UiState::Loading;
    }

    const long long lastTick = net.lastTickMs;
    bool stale = false;
    if (lastTick > 0 && interval > 0 && nowMs > lastTick) {
        const long long delta = nowMs - lastTick;
        stale = delta > (3 * interval);
    }

    if (repo.hasGap || stale) {
        outMsg = repo.hasGap ? "Desincronizado: faltan tramos (gap)."
                             : "Desincronizado: flujo detenido.";
        return core::UiState::Desync;
    }

    outMsg = "En vivo";
    return core::UiState::Live;
}

void RenderSnapshotBuilder::applyUiState(core::RenderSnapshot& snapshot,
                                         const std::optional<StateInputs>& stateInputs) const {
    if (stateInputs) {
        snapshot.state = computeUiState(stateInputs->repo, stateInputs->net, snapshot.stateMessage);
    } else {
        snapshot.state = snapshot.valid ? core::UiState::Live : core::UiState::NoData;
        if (snapshot.stateMessage.empty()) {
            snapshot.stateMessage = snapshot.valid ? "En vivo" : "Sin datos";
        }
    }

    LOG_TRACE(logging::LogCategory::SNAPSHOT,
              "Snapshot state: %d valid=%d candles=%zu",
              static_cast<int>(snapshot.state),
              snapshot.valid ? 1 : 0,
              static_cast<std::size_t>(snapshot.candles.size()));
}

long long RenderSnapshotBuilder::estimateIntervalMs(const std::vector<core::RenderCandleData>& series,
                                                    std::size_t startIndex,
                                                    std::size_t count) {
    if (count < 2) {
        return 0;
    }

    long long total = 0;
    std::size_t valid = 0;
    for (std::size_t i = 1; i < count; ++i) {
        const auto current = series[startIndex + i].openTimeMs;
        const auto previous = series[startIndex + i - 1].openTimeMs;
        const long long delta = current - previous;
        if (delta > 0) {
            total += delta;
            ++valid;
        }
    }

    if (valid == 0) {
        return 0;
    }
    return total / static_cast<long long>(valid);
}

}  // namespace app

