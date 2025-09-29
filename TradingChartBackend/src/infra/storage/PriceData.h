#pragma once

#include <cstring>

namespace infra::storage {

struct PriceData {
    long long openTime;
    double openPrice;
    double highPrice;
    double lowPrice;
    double closePrice;
    double volume;
    long long closeTime;
    double baseAssetVolume;
    int numberOfTrades;
    double takerBuyVolume;
    double takerBuyBaseAssetVolume;
    char symbol[16];
    char interval[8];

    PriceData() {
        std::memset(symbol, 0, sizeof(symbol));
        std::memset(interval, 0, sizeof(interval));
    }
};

}  // namespace infra::storage
