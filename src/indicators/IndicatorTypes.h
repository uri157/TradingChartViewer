#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace indicators {

struct SeriesVersion {
    std::int64_t lastClosedOpenTimeMs = 0;
    std::size_t candleCount = 0;

    bool operator==(const SeriesVersion& o) const {
        return lastClosedOpenTimeMs == o.lastClosedOpenTimeMs && candleCount == o.candleCount;
    }
};

struct EmaParams {
    int period = 20;

    std::string name() const { return "EMA(" + std::to_string(period) + ")"; }
};

struct IndicatorSeries {
    std::string id;
    std::vector<float> values;
};

}  // namespace indicators

