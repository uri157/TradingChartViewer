#include "adapters/api/ws/WsHub.hpp"

#include "logging/Log.h"

namespace adapters::api::ws {

WsHub::WsHub() = default;
WsHub::~WsHub() { stop(); }

bool WsHub::start(unsigned short port) {
    port_ = port;
    running_.store(true);
    LOG_WARN(::logging::LogCategory::NET,
             "WsHub stub started on port %u. TODO: restore WebSocket implementation when Boost is available.",
             static_cast<unsigned int>(port));
    return true;
}

void WsHub::stop() {
    if (running_.exchange(false)) {
        LOG_WARN(::logging::LogCategory::NET,
                 "WsHub stub stopped. TODO: restore WebSocket implementation when Boost is available.");
    }
}

void WsHub::publishPartial(const CandlePayload& candle, std::uint64_t sequence) {
    (void)candle;
    (void)sequence;
    LOG_WARN(::logging::LogCategory::NET,
             "WsHub stub publishPartial invoked. TODO: restore WebSocket implementation when Boost is available.");
}

void WsHub::publishClose(const CandlePayload& candle, std::uint64_t sequence) {
    (void)candle;
    (void)sequence;
    LOG_WARN(::logging::LogCategory::NET,
             "WsHub stub publishClose invoked. TODO: restore WebSocket implementation when Boost is available.");
}

}  // namespace adapters::api::ws

