#pragma once

#include <string>

namespace adapters::duckdb {

class DuckStore {
public:
    explicit DuckStore(std::string dbPath = "data/market.duckdb");

    void migrate();

private:
    std::string dbPath_;
};

}  // namespace adapters::duckdb

