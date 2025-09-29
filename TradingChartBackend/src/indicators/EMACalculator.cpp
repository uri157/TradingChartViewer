#include "indicators/EMACalculator.h"

#include <stdexcept>

namespace indicators {

float EMACalculator::calculateEmaFromScratch(const std::vector<float>& prices, int periods) {
    if (periods <= 0) {
        throw std::invalid_argument("EMA period must be positive");
    }

    if (prices.size() < static_cast<std::size_t>(periods)) {
        throw std::invalid_argument("Not enough prices to compute EMA");
    }

    const float multiplier = 2.0f / (static_cast<float>(periods) + 1.0f);

    float ema = 0.0f;
    for (int i = 0; i < periods; ++i) {
        ema += prices[static_cast<std::size_t>(i)];
    }
    ema /= static_cast<float>(periods);

    for (std::size_t i = static_cast<std::size_t>(periods); i < prices.size(); ++i) {
        ema = ((prices[i] - ema) * multiplier) + ema;
    }

    return ema;
}

float EMACalculator::calculateEmaWithPrevious(float previousEma, float newPrice, int periods) {
    if (periods <= 0) {
        throw std::invalid_argument("EMA period must be positive");
    }

    const float multiplier = 2.0f / (static_cast<float>(periods) + 1.0f);
    return ((newPrice - previousEma) * multiplier) + previousEma;
}

}  // namespace indicators

