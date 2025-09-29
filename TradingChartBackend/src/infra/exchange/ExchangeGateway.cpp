#include "infra/exchange/ExchangeGateway.h"

#include <utility>

#include "logging/Log.h"

namespace infra::exchange {

ExchangeGateway::ExchangeGateway(ExchangeGatewayConfig cfg) : cfg_{std::move(cfg)} {}

ExchangeGateway::~ExchangeGateway() { stopLive(); }

std::vector<domain::Candle> ExchangeGateway::fetchRange(const domain::Symbol& symbol,
                                                         const domain::Interval& interval,
                                                         const domain::TimeRange& range,
                                                         std::size_t limit) {
    (void)symbol;
    (void)interval;
    (void)range;
    (void)limit;

    LOG_WARN(::logging::LogCategory::NET,
             "ExchangeGateway stub fetchRange invoked. TODO: restore Boost-based implementation.");
    return {};
}

std::vector<domain::Candle> ExchangeGateway::fetchKlinesDesc(const domain::Symbol& symbol,
                                                             const domain::Interval& interval,
                                                             domain::TimestampMs endTime,
                                                             std::size_t limit) {
    (void)symbol;
    (void)interval;
    (void)endTime;
    (void)limit;

    LOG_WARN(::logging::LogCategory::NET,
             "ExchangeGateway stub fetchKlinesDesc invoked. TODO: restore Boost-based implementation.");
    return {};
}

std::unique_ptr<domain::SubscriptionHandle> ExchangeGateway::streamLive(
    const domain::Symbol& symbol,
    const domain::Interval& interval,
    std::function<void(const domain::LiveCandle&)> onData,
    std::function<void(const domain::StreamError&)> onError) {
    (void)symbol;
    (void)interval;
    (void)onData;
    (void)onError;

    LOG_WARN(::logging::LogCategory::NET,
             "ExchangeGateway stub streamLive invoked. TODO: restore Boost-based implementation.");
    return std::make_unique<StubSubscription>();
}

void ExchangeGateway::startLive(const std::string& symbol, domain::Interval interval) {
    std::lock_guard<std::mutex> lock(mutex_);
    livePair_ = std::make_pair(symbol, interval);
    liveActive_.store(true);
    LOG_WARN(::logging::LogCategory::NET,
             "ExchangeGateway stub startLive for %s. TODO: restore Boost-based implementation.",
             symbol.c_str());
}

void ExchangeGateway::stopLive() {
    std::lock_guard<std::mutex> lock(mutex_);
    liveActive_.store(false);
    livePair_.reset();
}

bool ExchangeGateway::isLiveActive() const {
    return liveActive_.load();
}

std::optional<std::pair<std::string, domain::Interval>> ExchangeGateway::currentPair() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return livePair_;
}

}  // namespace infra::exchange

