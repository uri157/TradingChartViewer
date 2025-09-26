#include "indicators/IndicatorEngine.h"

#include "domain/Types.h"

#include <cmath>
#include <limits>

namespace indicators {
namespace {
constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();

double smoothingFactor(int period) {
    if (period <= 0) {
        return 0.0;
    }
    return 2.0 / (static_cast<double>(period) + 1.0);
}

}  // namespace

IndicatorSeries IndicatorEngine::computeEMA(const domain::CandleSeries& candles, const EmaParams& params) {
    IndicatorSeries series;
    series.id = params.name();
    const std::size_t candleCount = candles.data.size();
    series.values.assign(candleCount, kNaN);

    const int period = params.period;
    if (period <= 0 || candleCount == 0) {
        return series;
    }

    if (candleCount < static_cast<std::size_t>(period)) {
        return series;
    }

    const double alpha = smoothingFactor(period);

    double sum = 0.0;
    for (int i = 0; i < period; ++i) {
        sum += candles.data[static_cast<std::size_t>(i)].close;
    }
    double ema = sum / static_cast<double>(period);
    series.values[static_cast<std::size_t>(period - 1)] = static_cast<float>(ema);

    for (std::size_t i = static_cast<std::size_t>(period); i < candleCount; ++i) {
        const double price = candles.data[i].close;
        ema = (price - ema) * alpha + ema;
        series.values[i] = static_cast<float>(ema);
    }

    return series;
}

void IndicatorEngine::updateEMAIncremental(const domain::CandleSeries& candles,
                                           const EmaParams& params,
                                           IndicatorSeries& inOut) {
    const std::size_t candleCount = candles.data.size();
    const int period = params.period;

    if (period <= 0 || candleCount == 0) {
        inOut.values.clear();
        inOut.values.resize(candleCount, kNaN);
        inOut.id = params.name();
        return;
    }

    if (inOut.values.size() + 1 != candleCount) {
        inOut = computeEMA(candles, params);
        return;
    }

    inOut.id = params.name();
    inOut.values.resize(candleCount, kNaN);

    if (candleCount < static_cast<std::size_t>(period)) {
        return;
    }

    const std::size_t lastIndex = candleCount - 1;
    if (candleCount == static_cast<std::size_t>(period)) {
        double sum = 0.0;
        for (std::size_t i = 0; i < candleCount; ++i) {
            sum += candles.data[i].close;
        }
        const double ema = sum / static_cast<double>(period);
        inOut.values[lastIndex] = static_cast<float>(ema);
        return;
    }

    const float prevValue = inOut.values[lastIndex - 1];
    if (!std::isfinite(prevValue)) {
        inOut = computeEMA(candles, params);
        return;
    }

    const double alpha = smoothingFactor(period);
    const double prev = static_cast<double>(prevValue);
    const double price = candles.data.back().close;
    const double ema = (price - prev) * alpha + prev;
    inOut.values[lastIndex] = static_cast<float>(ema);
}

}  // namespace indicators

