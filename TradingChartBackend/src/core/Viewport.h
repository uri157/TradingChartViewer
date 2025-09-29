#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cmath>

namespace core {

struct Viewport {
    std::size_t candlesVisible{120};
    std::int64_t rightmostOpenTime{0};
    float minCandles{20.0f};
    float maxCandles{1000.0f};

    [[nodiscard]] std::size_t clampCandles(float value) const noexcept {
        if (!std::isfinite(value)) {
            value = static_cast<float>(candlesVisible);
        }
        const float minAllowed = std::max(minCandles, 1.0f);
        const float maxAllowed = std::max(maxCandles, minAllowed);
        const float clamped = std::clamp(value, minAllowed, maxAllowed);
        const auto rounded = static_cast<long long>(std::llround(clamped));
        return rounded > 0 ? static_cast<std::size_t>(rounded) : std::size_t{1};
    }

    void clampVisibleRange() noexcept {
        candlesVisible = clampCandles(static_cast<float>(candlesVisible));
    }
};

}  // namespace core
