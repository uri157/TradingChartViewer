#pragma once

#include <vector>

namespace indicators {

class EMACalculator {
public:
    static float calculateEmaFromScratch(const std::vector<float>& prices, int periods);

    static float calculateEmaWithPrevious(float previousEma, float newPrice, int periods);
};

}  // namespace indicators

