#pragma once

#include <mutex>
#include <string>
#include <vector>

#include "adapters/persistence/ICandleRepository.hpp"

namespace adapters::persistence::duckdb_adapter {

class DuckRepo : public ICandleRepository {
public:
    explicit DuckRepo(std::string dbPath = "data/market.duckdb");
    ~DuckRepo() override;

    void init() override;
    std::vector<CandleRow> getRange(const std::string& symbol,
                                    const std::string& interval,
                                    std::int64_t startInclusive,
                                    std::int64_t endExclusive) override;
    std::vector<CandleRow> getLastN(const std::string& symbol,
                                    const std::string& interval,
                                    std::size_t count) override;
    void upsert(const CandleRow& candle) override;

    void beginTransaction();
    void commitTransaction();

private:
    std::string dbPath_;
    std::vector<CandleRow> cache_;
    std::mutex mutex_;
    bool initialized_{false};
};

}  // namespace adapters::persistence::duckdb_adapter

