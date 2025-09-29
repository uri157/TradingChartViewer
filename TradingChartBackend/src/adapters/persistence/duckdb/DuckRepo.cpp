#include "adapters/persistence/duckdb/DuckRepo.hpp"

#include <algorithm>

#include "logging/Log.h"

namespace adapters::persistence::duckdb_adapter {

DuckRepo::DuckRepo(std::string dbPath) : dbPath_(std::move(dbPath)) {}

DuckRepo::~DuckRepo() = default;

void DuckRepo::init() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (initialized_) {
        return;
    }
    initialized_ = true;
    LOG_WARN(::logging::LogCategory::NET,
             "DuckRepo stub initialized for %s. TODO: restore DuckDB-backed implementation.",
             dbPath_.c_str());
}

std::vector<CandleRow> DuckRepo::getRange(const std::string& symbol,
                                          const std::string& interval,
                                          std::int64_t startInclusive,
                                          std::int64_t endExclusive) {
    std::lock_guard<std::mutex> lock(mutex_);
    (void)symbol;
    (void)interval;
    (void)startInclusive;
    (void)endExclusive;
    LOG_WARN(::logging::LogCategory::NET,
             "DuckRepo stub getRange invoked. TODO: restore DuckDB-backed implementation.");
    return {};
}

std::vector<CandleRow> DuckRepo::getLastN(const std::string& symbol,
                                          const std::string& interval,
                                          std::size_t count) {
    std::lock_guard<std::mutex> lock(mutex_);
    (void)symbol;
    (void)interval;
    (void)count;
    LOG_WARN(::logging::LogCategory::NET,
             "DuckRepo stub getLastN invoked. TODO: restore DuckDB-backed implementation.");
    return {};
}

void DuckRepo::upsert(const CandleRow& candle) {
    std::lock_guard<std::mutex> lock(mutex_);
    cache_.push_back(candle);
    LOG_WARN(::logging::LogCategory::NET,
             "DuckRepo stub upsert invoked. TODO: restore DuckDB-backed implementation.");
}

void DuckRepo::beginTransaction() {
    LOG_WARN(::logging::LogCategory::NET,
             "DuckRepo stub beginTransaction invoked. TODO: restore DuckDB-backed implementation.");
}

void DuckRepo::commitTransaction() {
    LOG_WARN(::logging::LogCategory::NET,
             "DuckRepo stub commitTransaction invoked. TODO: restore DuckDB-backed implementation.");
}

}  // namespace adapters::persistence::duckdb_adapter

