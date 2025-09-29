#pragma once

#include "core/ports/IMarketDataFeed.hpp"
#include "domain/Types.h"

#include <chrono>
#include <memory>
#include <utility>

namespace infra {
namespace exchange {
class ExchangeGateway;
}  // namespace exchange
}  // namespace infra

namespace adapters::feeds {

class ExchangeFeed : public IMarketDataFeed {
public:
    struct Config {
        domain::Symbol symbol;
        domain::Interval interval{};
        std::chrono::milliseconds conflationInterval{std::chrono::milliseconds(150)};
    };

    ExchangeFeed(infra::exchange::ExchangeGateway& gateway, Config config);
    ~ExchangeFeed() override;

    void start() override;
    void stop() override;

    void setOnPartial(PartialCallback callback);
    void setOnClose(CloseCallback callback);

    void onPartial(PartialCallback callback) override { setOnPartial(std::move(callback)); }
    void onClose(CloseCallback callback) override { setOnClose(std::move(callback)); }

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace adapters::feeds

