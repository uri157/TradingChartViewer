#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "domain/MarketSource.h"
#include "domain/Types.h"

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

    std::vector<domain::Candle> fetchKlinesDesc(const domain::Symbol& symbol,
                                                const domain::Interval& interval,
                                                domain::TimestampMs endTime,
                                                std::size_t limit);

    std::unique_ptr<domain::SubscriptionHandle> streamLive(
        const domain::Symbol& symbol,
        const domain::Interval& interval,
        std::function<void(const domain::LiveCandle&)> onData,
        std::function<void(const domain::StreamError&)> onError) override;

    void startLive(const std::string& symbol, domain::Interval interval);
    void stopLive();

    bool isLiveActive() const;
    std::optional<std::pair<std::string, domain::Interval>> currentPair() const;

private:
    struct StubSubscription : domain::SubscriptionHandle {
        void stop() override {}
    };

    ExchangeGatewayConfig cfg_{};
    mutable std::mutex mutex_;
    std::optional<std::pair<std::string, domain::Interval>> livePair_;
    std::atomic<bool> liveActive_{false};
};

}  // namespace infra::exchange

