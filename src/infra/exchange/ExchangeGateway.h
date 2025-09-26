#pragma once

#include <memory>
#include <string>
#include <vector>

#include "logging/Log.h"
#include "domain/MarketSource.h"

namespace infra::exchange {

struct ExchangeGatewayConfig {
    std::string restHost{"api.binance.com"};
    std::string wsHost{"stream.binance.com"};
    std::string wsPathTemplate{"/ws/%s@kline_%s"};
    int wsPort{9443};
    std::size_t restMaxLimit{1000};
    int restMinSleepMs{200};
    int backoffBaseMs{250};
    int backoffCapMs{8000};
    int idleTimeoutSec{30};
    int maxRetries{6};
    bool trace{false};
};

class ExchangeGateway : public domain::MarketSource {
public:
    explicit ExchangeGateway(ExchangeGatewayConfig cfg = {});
    ~ExchangeGateway() override;

    std::vector<domain::Candle> fetchRange(const domain::Symbol& symbol,
                                           const domain::Interval& interval,
                                           const domain::TimeRange& range,
                                           std::size_t limit) override;

    std::unique_ptr<domain::SubscriptionHandle> streamLive(
        const domain::Symbol& symbol,
        const domain::Interval& interval,
        std::function<void(const domain::LiveCandle&)> onData,
        std::function<void(const domain::StreamError&)> onError) override;

private:
    ExchangeGatewayConfig cfg_;
};

}  // namespace infra::exchange

