#pragma once

#include <string>

struct Candle;

class IIndicatorEngine {
public:
    virtual ~IIndicatorEngine() = default;

    virtual void rebuild(const std::string& symbol, const std::string& interval) = 0;
    virtual void applyPartial(const Candle& candle) = 0;
    virtual void applyClosed(const Candle& candle) = 0;
};

