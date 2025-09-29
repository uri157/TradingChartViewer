#include "infra/net/WsHub.h"

#include <utility>
#include <vector>

namespace infra::net {
namespace {
constexpr std::chrono::milliseconds kDefaultInterval{150};
}

WsHub::WsHub(std::chrono::milliseconds conflationInterval)
    : interval_(conflationInterval.count() > 0 ? conflationInterval : kDefaultInterval) {
    timerThread_ = std::thread([this]() { runTimer_(); });
}

WsHub::~WsHub() {
    {
        std::lock_guard<std::mutex> lk(mutex_);
        stop_ = true;
    }
    cv_.notify_all();
    if (timerThread_.joinable()) {
        timerThread_.join();
    }
}

void WsHub::setEmitter(std::function<void(const Message&)> emitter) {
    {
        std::lock_guard<std::mutex> lk(mutex_);
        emitter_ = std::move(emitter);
    }
    cv_.notify_all();
}

void WsHub::onLiveTick(const CandlePayload& candle) {
    std::lock_guard<std::mutex> lk(mutex_);
    auto& state = pending_[makeKey_(candle)];
    state.payload = candle;
    state.hasPending = true;
}

void WsHub::onCloseCandle(const CandlePayload& candle) {
    std::function<void(const Message&)> emitter;
    Message message;
    bool shouldEmit = false;

    {
        std::lock_guard<std::mutex> lk(mutex_);
        const auto key = makeKey_(candle);
        auto it = pending_.find(key);
        std::uint64_t seq = 1;
        if (it != pending_.end()) {
            seq = ++it->second.sequence;
            pending_.erase(it);
        }

        emitter = emitter_;
        if (emitter) {
            message.kind = MessageKind::Close;
            message.symbol = candle.symbol;
            message.interval = candle.interval;
            message.candle = candle.candle;
            message.sequence = seq;
            shouldEmit = true;
        }
    }

    if (shouldEmit) {
        emitter(message);
    }
}

WsHub::Key WsHub::makeKey_(const domain::Symbol& symbol, const domain::Interval& interval) {
    Key key;
    key.reserve(symbol.size() + 1 + 24);
    key.append(symbol);
    key.push_back('|');
    key.append(std::to_string(interval.ms));
    return key;
}

WsHub::Key WsHub::makeKey_(const CandlePayload& payload) {
    return makeKey_(payload.symbol, payload.interval);
}

WsHub::Key WsHub::makeKey_(const Message& message) {
    return makeKey_(message.symbol, message.interval);
}

void WsHub::runTimer_() {
    std::unique_lock<std::mutex> lock(mutex_);
    while (!stop_) {
        cv_.wait_for(lock, interval_, [this]() { return stop_; });
        if (stop_) {
            break;
        }
        if (pending_.empty()) {
            continue;
        }

        auto emitter = emitter_;
        if (!emitter) {
            continue;
        }

        std::vector<Message> toEmit;
        toEmit.reserve(pending_.size());
        for (auto& entry : pending_) {
            auto& state = entry.second;
            if (!state.hasPending) {
                continue;
            }
            ++state.sequence;
            Message msg;
            msg.kind = MessageKind::Partial;
            msg.symbol = state.payload.symbol;
            msg.interval = state.payload.interval;
            msg.candle = state.payload.candle;
            msg.sequence = state.sequence;
            toEmit.push_back(msg);
            state.hasPending = false;
        }

        if (toEmit.empty()) {
            continue;
        }

        lock.unlock();
        for (const auto& msg : toEmit) {
            bool shouldEmit = true;
            {
                std::lock_guard<std::mutex> guard(mutex_);
                const auto key = makeKey_(msg);
                auto it = pending_.find(key);
                if (it == pending_.end()) {
                    shouldEmit = false;
                } else if (it->second.sequence != msg.sequence || it->second.hasPending) {
                    shouldEmit = false;
                }
            }
            if (shouldEmit) {
                emitter(msg);
            }
        }
        lock.lock();
    }
}

}  // namespace infra::net
