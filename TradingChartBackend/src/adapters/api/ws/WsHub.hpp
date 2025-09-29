#pragma once

#include <atomic>
#include <cstdint>
#include <string>

#include "domain/Types.h"

namespace adapters::api::ws {

struct CandlePayload {
    domain::Symbol symbol;
    domain::Interval interval;
    domain::Candle candle;
};

class WsHub {
public:
    WsHub();
    ~WsHub();

    WsHub(const WsHub&) = delete;
    WsHub& operator=(const WsHub&) = delete;

    bool start(unsigned short port);
    void stop();

    void publishPartial(const CandlePayload& candle, std::uint64_t sequence);
    void publishClose(const CandlePayload& candle, std::uint64_t sequence);

private:
    std::atomic<bool> running_{false};
    unsigned short port_{0};
};

}  // namespace adapters::api::ws

