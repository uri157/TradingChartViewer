#pragma once

#include <functional>
#include <utility>

struct Candle;

class IMarketDataFeed {
public:
    using PartialCallback = std::function<void(const Candle&)>;
    using CloseCallback = std::function<void(const Candle&)>;

    virtual ~IMarketDataFeed() = default;

    virtual void start() = 0;
    virtual void stop() = 0;

    virtual void onPartial(PartialCallback callback) = 0;
    virtual void onClose(CloseCallback callback) = 0;

    void setOnPartial(PartialCallback callback) { onPartial(std::move(callback)); }
    void setOnClose(CloseCallback callback) { onClose(std::move(callback)); }
};

