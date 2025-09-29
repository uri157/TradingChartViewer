#pragma once

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>

#include "domain/Types.h"

namespace infra::net {

class WsHub {
public:
    struct CandlePayload {
        domain::Symbol symbol;
        domain::Interval interval;
        domain::Candle candle;
    };

    enum class MessageKind { Partial, Close };

    struct Message {
        MessageKind kind{MessageKind::Partial};
        domain::Symbol symbol;
        domain::Interval interval;
        domain::Candle candle;
        std::uint64_t sequence{0};
    };

    explicit WsHub(std::chrono::milliseconds conflationInterval = std::chrono::milliseconds(150));
    ~WsHub();

    WsHub(const WsHub&) = delete;
    WsHub& operator=(const WsHub&) = delete;

    void setEmitter(std::function<void(const Message&)> emitter);

    void onLiveTick(const CandlePayload& candle);
    void onCloseCandle(const CandlePayload& candle);

private:
    using Key = std::string;

    struct PendingState {
        CandlePayload payload;
        bool hasPending{false};
        std::uint64_t sequence{0};
    };

    static Key makeKey_(const domain::Symbol& symbol, const domain::Interval& interval);
    static Key makeKey_(const CandlePayload& payload);
    static Key makeKey_(const Message& message);

    void runTimer_();

    std::chrono::milliseconds interval_;
    std::function<void(const Message&)> emitter_;

    std::unordered_map<Key, PendingState> pending_;
    std::mutex mutex_;
    std::condition_variable cv_;
    std::thread timerThread_;
    bool stop_{false};
};

}  // namespace infra::net
