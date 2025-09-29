#pragma once

#include <memory>
#include <string>
#include <vector>

#include "core/app/ICandleRepository.hpp"

namespace adapters::persistence {
struct CandleRow;
}  // namespace adapters::persistence

namespace adapters::persistence::duckdb_adapter {
class DuckRepo;
}  // namespace adapters::persistence::duckdb_adapter

namespace core::app {

class DuckRepo : public ICandleRepository {
public:
    explicit DuckRepo(std::string dbPath = "data/market.duckdb");
    explicit DuckRepo(std::unique_ptr<adapters::persistence::duckdb_adapter::DuckRepo> repo);
    ~DuckRepo() override;

    std::vector<domain::Candle> getSnapshot(const domain::Symbol& symbol,
                                            domain::Interval interval,
                                            std::size_t limit) override;

    std::vector<domain::Candle> getRange(const domain::Symbol& symbol,
                                         domain::Interval interval,
                                         domain::TimestampMs from,
                                         domain::TimestampMs to,
                                         std::size_t limit) override;

private:
    adapters::persistence::duckdb_adapter::DuckRepo& repo_;
    std::unique_ptr<adapters::persistence::duckdb_adapter::DuckRepo> ownedRepo_;
};

}  // namespace core::app

