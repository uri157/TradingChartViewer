#include "adapters/duckdb/DuckCandleRepo.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include "domain/Models.hpp"
#include "domain/Types.h"
#include "logging/Log.h"

#if defined(HAS_DUCKDB)
#include <duckdb.hpp>
#endif

namespace fs = std::filesystem;

namespace adapters::duckdb {
namespace {
constexpr logging::LogCategory kLogCategory = logging::LogCategory::DB;
constexpr std::size_t kBatchChunkSize = 5000;
constexpr std::int64_t kMillisecondsThreshold = 1'000'000'000'000LL;
constexpr std::string_view kCandlesPartitionPrefix = "candles_";

[[maybe_unused]] std::size_t reserveForLimit(std::size_t limit) {
    if (limit == 0) {
        return 256;
    }
    return std::min<std::size_t>(limit, 512);
}

#if defined(HAS_DUCKDB)
using DuckdbPreparedStatement = ::duckdb::PreparedStatement;
// DuckDB exposes its own vector alias; using it keeps Execute(values) on the
// non-variadic overload and avoids the template path that triggers the static assert.
using DuckdbValueVector = ::duckdb::vector<::duckdb::Value>;

std::int64_t normalize_timestamp_ms(std::int64_t ts) {
    if (ts > 0 && ts < kMillisecondsThreshold) {
        return ts * 1000LL;
    }
    return ts;
}

std::string to_lower_copy(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

std::vector<domain::contracts::Candle> fetchCandles(DuckdbPreparedStatement& statement,
                                                   DuckdbValueVector& parameters,
                                                   std::size_t limit) {
    auto result = statement.Execute(parameters);
    if (!result || result->HasError()) {
        const std::string errorMessage = result ? result->GetError() : std::string{"unknown query error"};
        LOG_WARN(kLogCategory, "DuckCandleRepo query failed: %s", errorMessage.c_str());
        return {};
    }

    std::vector<domain::contracts::Candle> candles;
    candles.reserve(reserveForLimit(limit));

    while (auto chunk = result->Fetch()) {
        const auto count = chunk->size();
        for (::duckdb::idx_t row = 0; row < count; ++row) {
            const auto tsValue = chunk->GetValue(0, row);
            if (tsValue.IsNull()) {
                continue;
            }
            domain::contracts::Candle candle{};
            candle.ts = normalize_timestamp_ms(tsValue.GetValue<std::int64_t>());
            candle.o = chunk->GetValue(1, row).GetValue<double>();
            candle.h = chunk->GetValue(2, row).GetValue<double>();
            candle.l = chunk->GetValue(3, row).GetValue<double>();
            candle.c = chunk->GetValue(4, row).GetValue<double>();
            candle.v = chunk->GetValue(5, row).GetValue<double>();
            candles.push_back(candle);
        }
    }

    return candles;
}
#endif

}  // namespace

DuckCandleRepo::DuckCandleRepo(std::string dbPath) : dbPath_(std::move(dbPath)) {}

std::vector<domain::contracts::Candle> DuckCandleRepo::getCandles(const domain::contracts::Symbol& symbol,
                                                                  domain::contracts::Interval interval,
                                                                  std::int64_t fromTs,
                                                                  std::int64_t toTs,
                                                                  std::size_t limit) const {
#if !defined(HAS_DUCKDB)
    (void)symbol;
    (void)interval;
    (void)fromTs;
    (void)toTs;
    (void)limit;
    LOG_WARN(kLogCategory,
             "DuckCandleRepo invoked without DuckDB support compiled in; returning empty result");
    return {};
#else
    if (symbol.empty()) {
        return {};
    }

    const auto label = domain::contracts::intervalToString(interval);
    if (label.empty()) {
        return {};
    }

    fs::path dbPath{dbPath_};
    std::error_code ec;
    if (!fs::exists(dbPath, ec) || fs::is_directory(dbPath, ec)) {
        if (ec) {
            LOG_WARN(kLogCategory,
                     "DuckCandleRepo unable to stat database path=%s error=%s",
                     dbPath_.c_str(),
                     ec.message().c_str());
        }
        return {};
    }

    try {
        ::duckdb::DuckDB database(dbPath.string());  // reach global ::duckdb, not adapters::duckdb
        ::duckdb::Connection connection(database);

        std::string query =
            "SELECT ts, o, h, l, c, v FROM candles WHERE symbol = ? AND interval = ?";
        DuckdbValueVector parameters;
        parameters.reserve(5);
        parameters.emplace_back(symbol);
        parameters.emplace_back(label);

        const bool hasFrom = fromTs > 0;
        const bool hasTo = toTs > 0;
        const bool hasRange = hasFrom || hasTo;

        if (fromTs > 0) {
            query += " AND ts >= ?";
            parameters.emplace_back(::duckdb::Value::BIGINT(fromTs));
        }
        if (toTs > 0) {
            query += " AND ts <= ?";
            parameters.emplace_back(::duckdb::Value::BIGINT(toTs));
        }

        if (hasRange) {
            query += " ORDER BY ts ASC";
        }
        else {
            query += " ORDER BY ts DESC";
        }
        if (limit > 0) {
            query += " LIMIT ?";
            const auto limitValue =
                static_cast<std::int64_t>(std::min<std::size_t>(limit,
                                                                static_cast<std::size_t>(
                                                                    std::numeric_limits<std::int64_t>::max())));
            parameters.emplace_back(::duckdb::Value::BIGINT(limitValue));
        }

        auto statement = connection.Prepare(query);
        if (!statement || statement->HasError()) {
            const std::string errorMessage =
                statement ? statement->GetError() : std::string{"failed to prepare statement"};
            throw std::runtime_error("DuckCandleRepo prepare failed: " + errorMessage);
        }

        auto candles = fetchCandles(*statement, parameters, limit);
        if (!hasRange && limit > 0) {
            std::reverse(candles.begin(), candles.end());
        }
        return candles;
    }
    catch (const std::exception& ex) {
        LOG_WARN(kLogCategory,
                 "DuckCandleRepo query exception path=%s error=%s",
                 dbPath_.c_str(),
                 ex.what());
        throw;
    }
#endif
}

std::vector<domain::contracts::SymbolInfo> DuckCandleRepo::listSymbols() const {
#if !defined(HAS_DUCKDB)
    return {};
#else
    fs::path dbPath{dbPath_};
    std::error_code ec;
    if (!fs::exists(dbPath, ec) || fs::is_directory(dbPath, ec)) {
        return {};
    }

    try {
        ::duckdb::DuckDB database(dbPath.string());
        ::duckdb::Connection connection(database);

        std::unordered_map<std::string, domain::contracts::SymbolInfo> merged;
        merged.reserve(64);

        const auto mergeSymbol =
            [&merged](const std::string& symbol,
                      const std::optional<std::string>& base,
                      const std::optional<std::string>& quote) {
                if (symbol.empty()) {
                    return;
                }

                auto [it, inserted] = merged.emplace(symbol, domain::contracts::SymbolInfo{});
                if (inserted) {
                    it->second.symbol = symbol;
                }
                if (base && !base->empty()) {
                    it->second.base = *base;
                }
                if (quote && !quote->empty()) {
                    it->second.quote = *quote;
                }
            };

        const auto fetchDistinctFrom = [&](const std::string& tableName) {
            std::string query = "SELECT DISTINCT symbol FROM \"" + tableName + "\"";
            auto result = connection.Query(query);
            if (!result || result->HasError()) {
                const std::string errorMessage =
                    result ? result->GetError() : std::string{"failed to execute distinct symbol query"};
                throw std::runtime_error("DuckCandleRepo listSymbols failed: " + errorMessage);
            }

            while (auto chunk = result->Fetch()) {
                const auto count = chunk->size();
                for (::duckdb::idx_t row = 0; row < count; ++row) {
                    const auto value = chunk->GetValue(0, row);
                    if (value.IsNull()) {
                        continue;
                    }
                    mergeSymbol(value.GetValue<std::string>(), std::nullopt, std::nullopt);
                }
            }
        };

        bool hasUnifiedTable = false;
        std::vector<std::string> partitionTables;

        {
            auto result = connection.Query(
                "SELECT table_name FROM duckdb_tables WHERE table_schema = 'main' "
                "AND (table_name = 'candles' OR table_name LIKE 'candles_%')");
            if (!result || result->HasError()) {
                const std::string errorMessage =
                    result ? result->GetError() : std::string{"failed to inspect candle tables"};
                throw std::runtime_error("DuckCandleRepo listSymbols failed: " + errorMessage);
            }

            while (auto chunk = result->Fetch()) {
                const auto count = chunk->size();
                for (::duckdb::idx_t row = 0; row < count; ++row) {
                    const auto value = chunk->GetValue(0, row);
                    if (value.IsNull()) {
                        continue;
                    }

                    auto tableName = value.GetValue<std::string>();
                    if (tableName == "candles") {
                        hasUnifiedTable = true;
                    }
                    else if (tableName.rfind("candles_", 0) == 0) {
                        partitionTables.push_back(std::move(tableName));
                    }
                }
            }
        }

        for (const auto& table : partitionTables) {
            fetchDistinctFrom(table);
        }

        if (hasUnifiedTable) {
            fetchDistinctFrom("candles");
        }

        bool hasCatalogSymbols = false;
        {
            auto result = connection.Query(
                "SELECT 1 FROM duckdb_tables WHERE table_schema = 'main' AND table_name = 'catalog_symbols'");
            if (result && !result->HasError()) {
                if (auto chunk = result->Fetch()) {
                    if (chunk->size() > 0) {
                        const auto value = chunk->GetValue(0, 0);
                        hasCatalogSymbols = !value.IsNull();
                    }
                }
            }
        }

        if (hasCatalogSymbols) {
            auto result = connection.Query("SELECT symbol, base, quote FROM catalog_symbols");
            if (!result || result->HasError()) {
                const std::string errorMessage =
                    result ? result->GetError() : std::string{"failed to query catalog_symbols"};
                throw std::runtime_error("DuckCandleRepo listSymbols failed: " + errorMessage);
            }

            while (auto chunk = result->Fetch()) {
                const auto count = chunk->size();
                for (::duckdb::idx_t row = 0; row < count; ++row) {
                    const auto symbolValue = chunk->GetValue(0, row);
                    if (symbolValue.IsNull()) {
                        continue;
                    }

                    std::optional<std::string> base;
                    std::optional<std::string> quote;

                    const auto baseValue = chunk->GetValue(1, row);
                    if (!baseValue.IsNull()) {
                        base = baseValue.GetValue<std::string>();
                    }

                    const auto quoteValue = chunk->GetValue(2, row);
                    if (!quoteValue.IsNull()) {
                        quote = quoteValue.GetValue<std::string>();
                    }

                    mergeSymbol(symbolValue.GetValue<std::string>(), base, quote);
                }
            }
        }

        std::vector<domain::contracts::SymbolInfo> symbols;
        symbols.reserve(merged.size());
        for (auto& [_, value] : merged) {
            symbols.push_back(std::move(value));
        }
        std::sort(symbols.begin(), symbols.end(), [](const auto& lhs, const auto& rhs) {
            return lhs.symbol < rhs.symbol;
        });
        return symbols;
    }
    catch (const std::exception& ex) {
        LOG_WARN(kLogCategory,
                 "DuckCandleRepo listSymbols exception path=%s error=%s",
                 dbPath_.c_str(),
                 ex.what());
        throw;
    }
#endif
}

#if defined(HAS_DUCKDB)
namespace {
bool isCatalogMissing(const std::string& message) {
    return message.find("catalog_symbols") != std::string::npos;
}
}  // namespace
#endif

std::optional<bool> DuckCandleRepo::symbolExists(const domain::contracts::Symbol& symbol) const {
#if !defined(HAS_DUCKDB)
    (void)symbol;
    return std::nullopt;
#else
    if (symbol.empty()) {
        return false;
    }

    fs::path dbPath{dbPath_};
    std::error_code ec;
    if (!fs::exists(dbPath, ec) || fs::is_directory(dbPath, ec)) {
        return std::nullopt;
    }

    try {
        ::duckdb::DuckDB database(dbPath.string());
        ::duckdb::Connection connection(database);

        auto statement = connection.Prepare("SELECT 1 FROM catalog_symbols WHERE symbol = ? LIMIT 1");
        if (!statement || statement->HasError()) {
            const std::string errorMessage =
                statement ? statement->GetError() : std::string{"failed to prepare catalog lookup"};
            if (isCatalogMissing(errorMessage)) {
                return std::nullopt;
            }
            LOG_WARN(kLogCategory,
                     "DuckCandleRepo symbolExists prepare failed path=%s error=%s",
                     dbPath_.c_str(),
                     errorMessage.c_str());
            return std::nullopt;
        }

        DuckdbValueVector parameters;
        parameters.emplace_back(symbol);
        auto result = statement->Execute(parameters);
        if (!result || result->HasError()) {
            const std::string errorMessage =
                result ? result->GetError() : std::string{"failed to execute catalog lookup"};
            if (isCatalogMissing(errorMessage)) {
                return std::nullopt;
            }
            LOG_WARN(kLogCategory,
                     "DuckCandleRepo symbolExists execute failed path=%s error=%s",
                     dbPath_.c_str(),
                     errorMessage.c_str());
            return std::nullopt;
        }

        if (auto chunk = result->Fetch()) {
            if (chunk->size() > 0) {
                return true;
            }
        }
        return false;
    }
    catch (const std::exception& ex) {
        LOG_WARN(kLogCategory,
                 "DuckCandleRepo symbolExists exception path=%s error=%s",
                 dbPath_.c_str(),
                 ex.what());
        return std::nullopt;
    }
#endif
}

std::vector<domain::contracts::IntervalRangeInfo>
DuckCandleRepo::listSymbolIntervals(const domain::contracts::Symbol& symbol) const {
#if !defined(HAS_DUCKDB)
    (void)symbol;
    return {};
#else
    std::vector<domain::contracts::IntervalRangeInfo> intervals;
    if (symbol.empty()) {
        return intervals;
    }

    fs::path dbPath{dbPath_};
    std::error_code ec;
    if (!fs::exists(dbPath, ec) || fs::is_directory(dbPath, ec)) {
        return intervals;
    }

    try {
        ::duckdb::DuckDB database(dbPath.string());
        ::duckdb::Connection connection(database);

        bool hasUnifiedTable = false;
        std::vector<std::string> partitionTables;

        {
            auto result = connection.Query(
                "SELECT table_name FROM duckdb_tables WHERE table_schema = 'main' "
                "AND (table_name = 'candles' OR table_name LIKE 'candles_%')");
            if (!result || result->HasError()) {
                const std::string errorMessage =
                    result ? result->GetError() : std::string{"failed to inspect candle tables"};
                throw std::runtime_error("DuckCandleRepo listSymbolIntervals failed: " + errorMessage);
            }

            while (auto chunk = result->Fetch()) {
                const auto count = chunk->size();
                for (::duckdb::idx_t row = 0; row < count; ++row) {
                    const auto value = chunk->GetValue(0, row);
                    if (value.IsNull()) {
                        continue;
                    }

                    auto tableName = value.GetValue<std::string>();
                    if (tableName == "candles") {
                        hasUnifiedTable = true;
                    }
                    else if (tableName.rfind("candles_", 0) == 0) {
                        partitionTables.push_back(std::move(tableName));
                    }
                }
            }
        }

        std::unordered_map<std::string, domain::contracts::IntervalRangeInfo> merged;
        merged.reserve(partitionTables.size() + 8);

        const auto emplaceRange = [&merged](const std::string& intervalLabel,
                                           std::optional<std::int64_t> from,
                                           std::optional<std::int64_t> to) {
            if (!from || !to) {
                return;
            }

            auto [it, inserted] = merged.emplace(intervalLabel, domain::contracts::IntervalRangeInfo{});
            if (inserted) {
                it->second.interval = intervalLabel;
            }
            it->second.fromTs = *from;
            it->second.toTs = *to;
        };

        for (const auto& tableName : partitionTables) {
            const auto suffix = tableName.substr(kCandlesPartitionPrefix.size());
            if (suffix.empty()) {
                continue;
            }

            std::optional<std::string> timeColumn;
            {
                std::string query = "PRAGMA table_info('" + tableName + "')";
                auto infoResult = connection.Query(query);
                if (!infoResult || infoResult->HasError()) {
                    const std::string errorMessage =
                        infoResult ? infoResult->GetError() : std::string{"failed to inspect table"};
                    throw std::runtime_error("DuckCandleRepo listSymbolIntervals failed: " + errorMessage);
                }

                while (auto chunk = infoResult->Fetch()) {
                    const auto count = chunk->size();
                    for (::duckdb::idx_t row = 0; row < count; ++row) {
                        const auto nameValue = chunk->GetValue(1, row);
                        if (nameValue.IsNull()) {
                            continue;
                        }

                        const auto columnName = nameValue.GetValue<std::string>();
                        const auto lowered = to_lower_copy(columnName);
                        if (lowered == "open_time_ms" || lowered == "open_time" || lowered == "ts") {
                            if (!timeColumn || lowered == "open_time_ms") {
                                timeColumn = columnName;
                            }
                        }
                    }
                }
            }

            if (!timeColumn) {
                continue;
            }

            std::string query = "SELECT MIN(\"" + *timeColumn + "\"), MAX(\"" + *timeColumn
                + "\") FROM \"" + tableName + "\" WHERE symbol = ?";
            auto statement = connection.Prepare(query);
            if (!statement || statement->HasError()) {
                const std::string errorMessage =
                    statement ? statement->GetError() : std::string{"failed to prepare partition query"};
                throw std::runtime_error("DuckCandleRepo listSymbolIntervals failed: " + errorMessage);
            }

            DuckdbValueVector parameters;
            parameters.emplace_back(symbol);

            auto result = statement->Execute(parameters);
            if (!result || result->HasError()) {
                const std::string errorMessage =
                    result ? result->GetError() : std::string{"failed to execute partition query"};
                throw std::runtime_error("DuckCandleRepo listSymbolIntervals failed: " + errorMessage);
            }

            std::optional<std::int64_t> minTs;
            std::optional<std::int64_t> maxTs;

            if (auto chunk = result->Fetch()) {
                if (chunk->size() > 0) {
                    const auto minValue = chunk->GetValue(0, 0);
                    if (!minValue.IsNull()) {
                        minTs = normalize_timestamp_ms(minValue.GetValue<std::int64_t>());
                    }
                    const auto maxValue = chunk->GetValue(1, 0);
                    if (!maxValue.IsNull()) {
                        maxTs = normalize_timestamp_ms(maxValue.GetValue<std::int64_t>());
                    }
                }
            }

            emplaceRange(suffix, minTs, maxTs);
        }

        if (hasUnifiedTable) {
            auto statement = connection.Prepare(
                "SELECT interval, MIN(ts) AS from_ts, MAX(ts) AS to_ts FROM candles WHERE symbol = ? GROUP BY interval");
            if (!statement || statement->HasError()) {
                const std::string errorMessage =
                    statement ? statement->GetError() : std::string{"failed to prepare unified query"};
                throw std::runtime_error("DuckCandleRepo listSymbolIntervals failed: " + errorMessage);
            }

            DuckdbValueVector parameters;
            parameters.emplace_back(symbol);

            auto result = statement->Execute(parameters);
            if (!result || result->HasError()) {
                const std::string errorMessage =
                    result ? result->GetError() : std::string{"failed to execute unified query"};
                throw std::runtime_error("DuckCandleRepo listSymbolIntervals failed: " + errorMessage);
            }

            while (auto chunk = result->Fetch()) {
                const auto count = chunk->size();
                for (::duckdb::idx_t row = 0; row < count; ++row) {
                    const auto intervalValue = chunk->GetValue(0, row);
                    if (intervalValue.IsNull()) {
                        continue;
                    }

                    const auto intervalLabel = intervalValue.GetValue<std::string>();

                    std::optional<std::int64_t> minTs;
                    std::optional<std::int64_t> maxTs;

                    const auto minValue = chunk->GetValue(1, row);
                    if (!minValue.IsNull()) {
                        minTs = normalize_timestamp_ms(minValue.GetValue<std::int64_t>());
                    }
                    const auto maxValue = chunk->GetValue(2, row);
                    if (!maxValue.IsNull()) {
                        maxTs = normalize_timestamp_ms(maxValue.GetValue<std::int64_t>());
                    }

                    emplaceRange(intervalLabel, minTs, maxTs);
                }
            }
        }

        intervals.reserve(merged.size());
        for (auto& [_, info] : merged) {
            intervals.push_back(std::move(info));
        }

        std::sort(intervals.begin(), intervals.end(), [](const auto& lhs, const auto& rhs) {
            return lhs.interval < rhs.interval;
        });

        return intervals;
    }
    catch (const std::exception& ex) {
        LOG_WARN(kLogCategory,
                 "DuckCandleRepo listSymbolIntervals exception path=%s error=%s",
                 dbPath_.c_str(),
                 ex.what());
        throw;
    }
#endif
}

std::optional<std::pair<std::int64_t, std::int64_t>>
DuckCandleRepo::get_min_max_ts(const std::string& symbol, const std::string& interval) const {
#if !defined(HAS_DUCKDB)
    (void)symbol;
    (void)interval;
    return std::nullopt;
#else
    if (symbol.empty() || interval.empty()) {
        return std::nullopt;
    }

    fs::path dbPath{dbPath_};
    std::error_code ec;
    if (!fs::exists(dbPath, ec) || fs::is_directory(dbPath, ec)) {
        if (ec) {
            LOG_WARN(kLogCategory,
                     "DuckCandleRepo unable to stat database path=%s error=%s",
                     dbPath_.c_str(),
                     ec.message().c_str());
        }
        return std::nullopt;
    }

    try {
        ::duckdb::DuckDB database(dbPath.string());
        ::duckdb::Connection connection(database);

        auto statement = connection.Prepare(
            "SELECT MIN(ts) AS min_ts, MAX(ts) AS max_ts FROM candles WHERE symbol = ? AND interval = ?");
        if (!statement || statement->HasError()) {
            const std::string errorMessage =
                statement ? statement->GetError() : std::string{"failed to prepare min/max statement"};
            LOG_WARN(kLogCategory,
                     "DuckCandleRepo get_min_max_ts prepare failed error=%s",
                     errorMessage.c_str());
            return std::nullopt;
        }

        DuckdbValueVector parameters;
        parameters.reserve(2);
        parameters.emplace_back(symbol);
        parameters.emplace_back(interval);

        auto result = statement->Execute(parameters);
        if (!result || result->HasError()) {
            const std::string errorMessage =
                result ? result->GetError() : std::string{"failed to execute min/max statement"};
            LOG_WARN(kLogCategory,
                     "DuckCandleRepo get_min_max_ts execute failed error=%s",
                     errorMessage.c_str());
            return std::nullopt;
        }

        if (auto chunk = result->Fetch()) {
            if (chunk->size() > 0) {
                const auto minValue = chunk->GetValue(0, 0);
                const auto maxValue = chunk->GetValue(1, 0);
                if (!minValue.IsNull() && !maxValue.IsNull()) {
                    return std::make_pair(normalize_timestamp_ms(minValue.GetValue<std::int64_t>()),
                                          normalize_timestamp_ms(maxValue.GetValue<std::int64_t>()));
                }
            }
        }
    }
    catch (const std::exception& ex) {
        LOG_WARN(kLogCategory,
                 "DuckCandleRepo get_min_max_ts exception path=%s error=%s",
                 dbPath_.c_str(),
                 ex.what());
    }

    return std::nullopt;
#endif
}

#if defined(HAS_DUCKDB)
namespace {
void logRollbackFailure(const std::exception& ex) {
    LOG_WARN(kLogCategory, "DuckCandleRepo rollback failed: %s", ex.what());
}
}  // namespace
#endif

bool DuckCandleRepo::upsert_batch(const std::string& symbol,
                                  const std::string& interval,
                                  const std::vector<domain::Candle>& rows) {
#if !defined(HAS_DUCKDB)
    (void)symbol;
    (void)interval;
    (void)rows;
    LOG_WARN(kLogCategory,
             "DuckCandleRepo upsert_batch invoked without DuckDB support compiled in; returning false");
    return false;
#else
    if (symbol.empty() || interval.empty() || rows.empty()) {
        return false;
    }

    fs::path dbPath{dbPath_};
    const auto parent = dbPath.parent_path();
    if (!parent.empty()) {
        std::error_code ec;
        fs::create_directories(parent, ec);
        if (ec) {
            LOG_WARN(kLogCategory,
                     "DuckCandleRepo failed to create database directory path=%s error=%s",
                     parent.string().c_str(),
                     ec.message().c_str());
            return false;
        }
    }

    try {
        ::duckdb::DuckDB database(dbPath.string());
        ::duckdb::Connection connection(database);

        bool inTransaction = false;
        auto rollback = [&]() {
            if (!inTransaction) {
                return;
            }
            try {
                connection.Rollback();
            }
            catch (const std::exception& ex) {
                logRollbackFailure(ex);
            }
            inTransaction = false;
        };

        try {
            connection.BeginTransaction();
            inTransaction = true;

            auto statement = connection.Prepare("INSERT OR REPLACE INTO candles "
                                                "(symbol, interval, ts, o, h, l, c, v) "
                                                "VALUES (?, ?, ?, ?, ?, ?, ?, ?)");
            if (!statement || statement->HasError()) {
                const std::string errorMessage =
                    statement ? statement->GetError() : std::string{"failed to prepare statement"};
                LOG_WARN(kLogCategory,
                         "DuckCandleRepo failed to prepare upsert statement error=%s",
                         errorMessage.c_str());
                rollback();
                return false;
            }

            bool affected = false;
            DuckdbValueVector parameters;
            parameters.reserve(8);

            const std::size_t total = rows.size();
            for (std::size_t offset = 0; offset < total; offset += kBatchChunkSize) {
                const std::size_t end = std::min(offset + kBatchChunkSize, total);
                for (std::size_t index = offset; index < end; ++index) {
                    const auto& candle = rows[index];
                    parameters.clear();
                    parameters.emplace_back(symbol);
                    parameters.emplace_back(interval);
                    parameters.emplace_back(
                        ::duckdb::Value::BIGINT(static_cast<std::int64_t>(candle.openTime)));
                    parameters.emplace_back(::duckdb::Value::DOUBLE(candle.open));
                    parameters.emplace_back(::duckdb::Value::DOUBLE(candle.high));
                    parameters.emplace_back(::duckdb::Value::DOUBLE(candle.low));
                    parameters.emplace_back(::duckdb::Value::DOUBLE(candle.close));
                    parameters.emplace_back(::duckdb::Value::DOUBLE(candle.baseVolume));

                    auto result = statement->Execute(parameters);
                    if (!result || result->HasError()) {
                        const std::string errorMessage =
                            result ? result->GetError() : std::string{"failed to execute statement"};
                        LOG_WARN(kLogCategory,
                                 "DuckCandleRepo upsert execution failed error=%s",
                                 errorMessage.c_str());
                        rollback();
                        return false;
                    }

                    if (result->type == ::duckdb::QueryResultType::MATERIALIZED_RESULT) {
                        auto& materialized = result->Cast<::duckdb::MaterializedQueryResult>();
                        if (materialized.properties.return_type == ::duckdb::StatementReturnType::CHANGED_ROWS) {
                            if (materialized.RowCount() > 0 &&
                                materialized.GetValue<std::int64_t>(0, 0) > 0) {
                                affected = true;
                            }
                        } else if (materialized.RowCount() > 0) {
                            affected = true;
                        }
                    } else {
                        affected = true;
                    }
                }
            }

            connection.Commit();
            inTransaction = false;
            return affected;
        }
        catch (...) {
            rollback();
            throw;
        }
    }
    catch (const std::exception& ex) {
        LOG_WARN(kLogCategory,
                 "DuckCandleRepo upsert_batch exception path=%s error=%s",
                 dbPath_.c_str(),
                 ex.what());
    }

    return false;
#endif
}

std::optional<std::int64_t> DuckCandleRepo::max_timestamp(const std::string& symbol,
                                                          const std::string& interval) const {
#if !defined(HAS_DUCKDB)
    (void)symbol;
    (void)interval;
    LOG_WARN(kLogCategory,
             "DuckCandleRepo max_timestamp invoked without DuckDB support compiled in; returning nullopt");
    return std::nullopt;
#else
    if (symbol.empty() || interval.empty()) {
        return std::nullopt;
    }

    fs::path dbPath{dbPath_};
    std::error_code ec;
    if (!fs::exists(dbPath, ec) || fs::is_directory(dbPath, ec)) {
        if (ec) {
            LOG_WARN(kLogCategory,
                     "DuckCandleRepo unable to stat database path=%s error=%s",
                     dbPath_.c_str(),
                     ec.message().c_str());
        }
        return std::nullopt;
    }

    try {
        ::duckdb::DuckDB database(dbPath.string());
        ::duckdb::Connection connection(database);

        auto statement =
            connection.Prepare("SELECT MAX(ts) FROM candles WHERE symbol = ? AND interval = ?");
        if (!statement || statement->HasError()) {
            const std::string errorMessage =
                statement ? statement->GetError() : std::string{"failed to prepare statement"};
            LOG_WARN(kLogCategory,
                     "DuckCandleRepo failed to prepare max_timestamp statement error=%s",
                     errorMessage.c_str());
            return std::nullopt;
        }

        DuckdbValueVector parameters;
        parameters.reserve(2);
        parameters.emplace_back(symbol);
        parameters.emplace_back(interval);

        auto result = statement->Execute(parameters);
        if (!result || result->HasError()) {
            const std::string errorMessage =
                result ? result->GetError() : std::string{"failed to execute statement"};
            LOG_WARN(kLogCategory,
                     "DuckCandleRepo max_timestamp execution failed error=%s",
                     errorMessage.c_str());
            return std::nullopt;
        }

        if (auto chunk = result->Fetch()) {
            if (chunk->size() > 0) {
                const auto value = chunk->GetValue(0, 0);
                if (!value.IsNull()) {
                    return normalize_timestamp_ms(value.GetValue<std::int64_t>());
                }
            }
        }
    }
    catch (const std::exception& ex) {
        LOG_WARN(kLogCategory,
                 "DuckCandleRepo max_timestamp exception path=%s error=%s",
                 dbPath_.c_str(),
                 ex.what());
    }

    return std::nullopt;
#endif
}

}  // namespace adapters::duckdb

