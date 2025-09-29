#pragma once

#include "indicators/IndicatorTypes.h"

namespace domain {
struct CandleSeries;
}

namespace indicators {

class IndicatorEngine {
public:
    static IndicatorSeries computeEMA(const domain::CandleSeries& candles, const EmaParams& params);

    static void updateEMAIncremental(const domain::CandleSeries& candles,
                                     const EmaParams& params,
                                     IndicatorSeries& inOut);
};

}  // namespace indicators

