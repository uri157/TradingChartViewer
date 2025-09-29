#include "core/app/LiveOrchestrator.hpp"

namespace core::app {

LiveOrchestrator::LiveOrchestrator(::IMarketDataFeed& feed,
                                   ::ICandleRepository& repository,
                                   WsHub& wsHub) noexcept
    : feed_(feed), repository_(repository), wsHub_(wsHub) {}

void LiveOrchestrator::start() {
    {
        std::lock_guard<std::mutex> guard(lifecycleMutex_);
        if (running_) {
            return;
        }

        feed_.setOnPartial([this](const ::Candle& candle) {
            std::lock_guard<std::mutex> callbackGuard(callbackMutex_);
            repository_.upsert(candle);
            wsHub_.onLiveTick(candle);
        });

        feed_.setOnClose([this](const ::Candle& candle) {
            std::lock_guard<std::mutex> callbackGuard(callbackMutex_);
            repository_.upsert(candle);
            wsHub_.publishClose(candle);
        });

        running_ = true;
    }

    feed_.start();
}

void LiveOrchestrator::stop() {
    bool shouldStop = false;
    {
        std::lock_guard<std::mutex> guard(lifecycleMutex_);
        if (!running_) {
            return;
        }
        running_ = false;
        shouldStop = true;
    }

    if (shouldStop) {
        feed_.stop();
    }
}

}  // namespace core::app
