#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct Candle {
    int64_t open_ms;
    double o;
    double h;
    double l;
    double c;
    double v;
};

class ICandleRepository {
public:
    virtual ~ICandleRepository() = default;

    virtual bool init() = 0;
    virtual std::vector<Candle> getRange(const std::string& symbol,
                                         const std::string& interval,
                                         int64_t from_ms,
                                         int64_t to_ms,
                                         size_t limit) = 0;
    virtual std::vector<Candle> getLastN(const std::string& symbol,
                                         const std::string& interval,
                                         size_t limit) = 0;
    virtual void upsert(const Candle& candle) = 0;
};

