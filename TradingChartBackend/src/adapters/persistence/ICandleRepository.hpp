#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace adapters::persistence {

struct CandleRow {
    std::string symbol;
    std::string interval;
    std::int64_t openMs{0};
    double open{0.0};
    double high{0.0};
    double low{0.0};
    double close{0.0};
    double volume{0.0};
};

class ICandleRepository {
public:
    virtual ~ICandleRepository() = default;

    virtual void init() = 0;
    virtual std::vector<CandleRow> getRange(const std::string& symbol,
                                            const std::string& interval,
                                            std::int64_t startInclusive,
                                            std::int64_t endExclusive) = 0;
    virtual std::vector<CandleRow> getLastN(const std::string& symbol,
                                            const std::string& interval,
                                            std::size_t count) = 0;
    virtual void upsert(const CandleRow& candle) = 0;
};

}  // namespace adapters::persistence

