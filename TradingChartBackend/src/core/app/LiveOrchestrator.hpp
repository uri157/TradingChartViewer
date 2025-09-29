#pragma once

#include <mutex>

#include "core/ports/ICandleRepository.hpp"
#include "core/ports/IMarketDataFeed.hpp"

namespace core::app {

class WsHub {
public:
    virtual ~WsHub() = default;
    virtual void onLiveTick(const ::Candle& candle) = 0;
    virtual void publishClose(const ::Candle& candle) = 0;
};

class LiveOrchestrator {
public:
    LiveOrchestrator(::IMarketDataFeed& feed, ::ICandleRepository& repository, WsHub& wsHub) noexcept;

    void start();
    void stop();

private:
    ::IMarketDataFeed& feed_;
    ::ICandleRepository& repository_;
    WsHub& wsHub_;

    std::mutex lifecycleMutex_;
    std::mutex callbackMutex_;
    bool running_{false};
};

}  // namespace core::app
