// CryptoDataFetcher.h
#pragma once

#include <string>
#include <vector>

#include "infra/storage/PriceData.h"

namespace infra::tools {

class CryptoDataFetcher {
public:
    CryptoDataFetcher() = default;
    explicit CryptoDataFetcher(std::string restHost);

    void setRestHost(std::string host);
    const std::string& restHost() const noexcept { return restHost_; }

    std::vector<infra::storage::PriceData> fetchHistoricalData(const std::string& symbol,
                                                               const std::string& interval,
                                                               long long startTime,
                                                               std::size_t limit = 1000);

private:
    std::string restHost_{"api.binance.com"};
};

}  // namespace infra::tools
