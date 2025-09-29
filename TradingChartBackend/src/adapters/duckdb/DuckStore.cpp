#include "adapters/duckdb/DuckStore.hpp"

#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>

#include "common/Log.hpp"

#if defined(HAS_DUCKDB)
#include <duckdb.hpp>
#endif

namespace fs = std::filesystem;

namespace adapters::duckdb {

DuckStore::DuckStore(std::string dbPath) : dbPath_(std::move(dbPath)) {}

void DuckStore::migrate() {
#if !defined(HAS_DUCKDB)
    LOG_WARN("DuckDB support disabled at compile time; skipping migrations.");
    return;
#else
    const fs::path dbPath{dbPath_};

    if (dbPath.has_parent_path()) {
        std::error_code ec;
        fs::create_directories(dbPath.parent_path(), ec);
        if (ec) {
            throw std::runtime_error("DuckStore: unable to create directory '" +
                                     dbPath.parent_path().string() + "': " + ec.message());
        }
    }

    ::duckdb::DuckDB db(dbPath.string());
    ::duckdb::Connection connection(db);

    static constexpr auto kCreateCandlesTable = R"SQL(
        CREATE TABLE IF NOT EXISTS candles (
            symbol TEXT,
            interval TEXT,
            ts BIGINT,
            o DOUBLE,
            h DOUBLE,
            l DOUBLE,
            c DOUBLE,
            v DOUBLE,
            PRIMARY KEY(symbol, interval, ts)
        )
    )SQL";

    auto result = connection.Query(kCreateCandlesTable);
    if (!result || result->HasError()) {
        const std::string errorMessage =
            result ? result->GetError() : std::string("unknown error creating candles table");
        throw std::runtime_error("DuckStore: migration failed: " + errorMessage);
    }

    static constexpr auto kLegacyCountQuery =
        "SELECT COUNT(*) FROM candles WHERE ts < 1000000000000";
    auto legacyCountResult = connection.Query(kLegacyCountQuery);
    if (!legacyCountResult || legacyCountResult->HasError()) {
        const std::string errorMessage = legacyCountResult ? legacyCountResult->GetError()
                                                          : std::string{"failed to query legacy timestamps"};
        LOG_WARN("DuckStore: unable to inspect legacy timestamps: " << errorMessage);
        LOG_INFO("DuckStore migration finished for " << dbPath.string());
        return;
    }

    std::int64_t legacyCount = 0;
    if (auto chunk = legacyCountResult->Fetch()) {
        if (chunk->size() > 0) {
            const auto value = chunk->GetValue(0, 0);
            if (!value.IsNull()) {
                legacyCount = value.GetValue<std::int64_t>();
            }
        }
    }

    if (legacyCount > 0) {
        auto beginResult = connection.Query("BEGIN TRANSACTION");
        if (!beginResult || beginResult->HasError()) {
            const std::string errorMessage = beginResult ? beginResult->GetError()
                                                         : std::string{"failed to begin transaction"};
            LOG_WARN("DuckStore: unable to start normalization transaction: " << errorMessage);
        } else {
            auto updateResult = connection.Query(
                "UPDATE candles SET ts = ts * 1000 WHERE ts < 1000000000000");
            if (!updateResult || updateResult->HasError()) {
                const std::string errorMessage = updateResult ? updateResult->GetError()
                                                              : std::string{"failed to normalize timestamps"};
                LOG_WARN("DuckStore: normalization update failed: " << errorMessage);
                connection.Query("ROLLBACK");
            } else {
                auto commitResult = connection.Query("COMMIT");
                if (!commitResult || commitResult->HasError()) {
                    const std::string errorMessage = commitResult ? commitResult->GetError()
                                                                  : std::string{"failed to commit normalization"};
                    LOG_WARN("DuckStore: normalization commit failed: " << errorMessage);
                    connection.Query("ROLLBACK");
                } else {
                    LOG_INFO("DuckStore normalized " << legacyCount
                                                     << " candle timestamps to milliseconds");
                }
            }
        }
    }

    LOG_INFO("DuckStore migration finished for " << dbPath.string());
#endif
}

}  // namespace adapters::duckdb

