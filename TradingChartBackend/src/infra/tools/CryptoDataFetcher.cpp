#include "infra/tools/CryptoDataFetcher.h"

#include <utility>

#include "logging/Log.h"

namespace infra::tools {

CryptoDataFetcher::CryptoDataFetcher(std::string restHost)
    : restHost_(std::move(restHost)) {
    if (restHost_.empty()) {
        restHost_ = "api.binance.com";
    }
}

void CryptoDataFetcher::setRestHost(std::string host) {
    if (!host.empty()) {
        restHost_ = std::move(host);
    }
}

std::vector<infra::storage::PriceData> CryptoDataFetcher::fetchHistoricalData(const std::string& symbol,
                                                                             const std::string& interval,
                                                                             long long startTime,
                                                                             std::size_t limit) {
    (void)symbol;
    (void)interval;
    (void)startTime;
    (void)limit;

    LOG_WARN(::logging::LogCategory::NET,
             "CryptoDataFetcher stub called without Boost.Beast available. TODO: restore HTTP implementation.");
    return {};
}

}  // namespace infra::tools

