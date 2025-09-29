#pragma once

#include <cstdint>
#include <string>

#include "domain/exchange/IExchangeKlines.hpp"

namespace adapters::binance {

class BinanceRestClient : public domain::IExchangeKlines {
public:
    BinanceRestClient() = default;
    ~BinanceRestClient() override = default;

    domain::KlinesPage fetch_klines(const std::string& symbol,
                                    domain::Interval interval,
                                    std::int64_t from_ts,
                                    std::int64_t to_ts,
                                    std::size_t page_limit = 1000) override;

    static constexpr std::int64_t kDefaultFromTs = 1754006400;  // 2025-08-01 00:00:00 UTC

private:
    static std::int64_t interval_to_seconds(domain::Interval interval);
};

}  // namespace adapters::binance

