#include "adapters/feeds/ExchangeFeed.hpp"

#include "core/ports/ICandleRepository.hpp"
#include "domain/MarketSource.h"
#include "infra/exchange/ExchangeGateway.h"
#include "infra/net/WsHub.h"
#include "logging/Log.h"

#include <atomic>
#include <mutex>
#include <utility>

namespace adapters::feeds {

namespace {
#if TTP_HYBRID_BACKEND
::Candle toRepositoryCandle(const domain::Candle& candle) {
    ::Candle result{};
    result.open_ms = candle.openTime;
    result.o = candle.open;
    result.h = candle.high;
    result.l = candle.low;
    result.c = candle.close;
    result.v = candle.baseVolume;
    return result;
}
#endif
}  // namespace

struct ExchangeFeed::Impl {
    Impl(infra::exchange::ExchangeGateway& gateway, Config cfg)
#if TTP_HYBRID_BACKEND
        : gateway_(gateway),
          config_(std::move(cfg)),
          hub_(config_.conflationInterval) {
        hub_.setEmitter([this](const infra::net::WsHub::Message& message) { dispatch_(message); });
    }
#else
        : gateway_(gateway), config_(std::move(cfg)) {}
#endif

#if TTP_HYBRID_BACKEND
    ~Impl() { stop(); }
#endif

    void start() {
#if TTP_HYBRID_BACKEND
        bool expected = false;
        if (!running_.compare_exchange_strong(expected, true)) {
            return;
        }

        auto onData = [this](const domain::LiveCandle& live) { handleLive_(live); };
        auto onError = [this](const domain::StreamError& err) { handleError_(err); };

        try {
            auto subscription = gateway_.streamLive(config_.symbol, config_.interval, std::move(onData), std::move(onError));
            std::lock_guard<std::mutex> lock(subscriptionMutex_);
            subscription_ = std::move(subscription);
        } catch (...) {
            running_.store(false, std::memory_order_release);
            throw;
        }
#else
        (void)this;
#endif
    }

    void stop() {
#if TTP_HYBRID_BACKEND
        bool expected = true;
        if (!running_.compare_exchange_strong(expected, false)) {
            return;
        }

        std::unique_ptr<domain::SubscriptionHandle> toStop;
        {
            std::lock_guard<std::mutex> lock(subscriptionMutex_);
            toStop = std::move(subscription_);
        }

        if (toStop) {
            try {
                toStop->stop();
            } catch (...) {
                LOG_WARN(logging::LogCategory::NET, "ExchangeFeed subscription stop threw");
            }
        }
#else
        (void)this;
#endif
    }

    void setOnPartial(PartialCallback callback) {
#if TTP_HYBRID_BACKEND
        std::lock_guard<std::mutex> lock(callbackMutex_);
        onPartial_ = std::move(callback);
#else
        (void)callback;
#endif
    }

    void setOnClose(CloseCallback callback) {
#if TTP_HYBRID_BACKEND
        std::lock_guard<std::mutex> lock(callbackMutex_);
        onClose_ = std::move(callback);
#else
        (void)callback;
#endif
    }

private:
#if TTP_HYBRID_BACKEND
    void handleLive_(const domain::LiveCandle& live) {
        if (!running_.load(std::memory_order_acquire)) {
            return;
        }

        infra::net::WsHub::CandlePayload payload{};
        payload.symbol = config_.symbol;
        payload.interval = config_.interval;
        payload.candle = live.candle;

        if (live.isFinal || live.candle.isClosed) {
            payload.candle.isClosed = true;
            hub_.onCloseCandle(payload);
        } else {
            payload.candle.isClosed = false;
            hub_.onLiveTick(payload);
        }
    }

    void handleError_(const domain::StreamError& err) {
        LOG_WARN(logging::LogCategory::NET,
                 "ExchangeFeed stream error code=%d message=%s",
                 err.code,
                 err.message.c_str());
    }

    void dispatch_(const infra::net::WsHub::Message& message) {
        if (!running_.load(std::memory_order_acquire)) {
            return;
        }

        PartialCallback partial;
        CloseCallback close;
        {
            std::lock_guard<std::mutex> lock(callbackMutex_);
            partial = onPartial_;
            close = onClose_;
        }

        const ::Candle candle = toRepositoryCandle(message.candle);
        if (message.kind == infra::net::WsHub::MessageKind::Partial) {
            if (partial) {
                partial(candle);
            }
        } else if (close) {
            close(candle);
        }
    }

    infra::exchange::ExchangeGateway& gateway_;
    Config config_;
    infra::net::WsHub hub_;
    std::unique_ptr<domain::SubscriptionHandle> subscription_;
    std::mutex subscriptionMutex_;
    std::mutex callbackMutex_;
    PartialCallback onPartial_;
    CloseCallback onClose_;
    std::atomic<bool> running_{false};
#else
    infra::exchange::ExchangeGateway& gateway_;
    Config config_;
#endif
};

ExchangeFeed::ExchangeFeed(infra::exchange::ExchangeGateway& gateway, Config config)
    : impl_(std::make_unique<Impl>(gateway, std::move(config))) {}

ExchangeFeed::~ExchangeFeed() = default;

void ExchangeFeed::start() {
    impl_->start();
}

void ExchangeFeed::stop() {
    impl_->stop();
}

void ExchangeFeed::setOnPartial(PartialCallback callback) {
    impl_->setOnPartial(std::move(callback));
}

void ExchangeFeed::setOnClose(CloseCallback callback) {
    impl_->setOnClose(std::move(callback));
}

}  // namespace adapters::feeds

