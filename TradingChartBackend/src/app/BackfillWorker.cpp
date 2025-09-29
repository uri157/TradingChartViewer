#include "app/BackfillWorker.hpp"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cctype>
#include <ctime>
#include <exception>
#include <iomanip>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "adapters/binance/BinanceRestClient.hpp"

#include "adapters/duckdb/DuckCandleRepo.hpp"
#include "common/Config.hpp"
#include "common/Log.hpp"
#include "domain/Types.h"

namespace app {
namespace {

namespace exchange = domain;

constexpr std::size_t kPageSize = 1000;
constexpr std::size_t kProgressLogInterval = 5000;

#if defined(_WIN32)
std::time_t timegm_compat(std::tm* tm) {
    return _mkgmtime(tm);
}
#else
std::time_t timegm_compat(std::tm* tm) {
    return timegm(tm);
}
#endif

std::string toUpper(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::toupper(c));
    });
    return value;
}

std::string toLower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

std::optional<std::int64_t> parseDateToSeconds(const std::string& value) {
    if (value.empty()) {
        return std::nullopt;
    }

    std::tm tm{};
    std::istringstream input(value);
    input >> std::get_time(&tm, "%Y-%m-%d");
    if (input.fail()) {
        return std::nullopt;
    }

    tm.tm_isdst = 0;
    const auto raw = timegm_compat(&tm);
    if (raw < 0) {
        return std::nullopt;
    }
    return static_cast<std::int64_t>(raw);
}

std::optional<std::int64_t> parseToSeconds(const std::string& value) {
    if (value.empty()) {
        return std::nullopt;
    }

    const auto normalized = toLower(value);
    if (normalized == "now") {
        const auto now = std::chrono::system_clock::now();
        const auto seconds = std::chrono::time_point_cast<std::chrono::seconds>(now);
        return static_cast<std::int64_t>(seconds.time_since_epoch().count());
    }

    auto parsed = parseDateToSeconds(value);
    if (!parsed.has_value()) {
        return std::nullopt;
    }
    constexpr std::int64_t kSecondsPerDay = 86'400;
    return parsed.value() + (kSecondsPerDay - 1);
}

std::vector<domain::Candle> toDuckCandles(const std::vector<exchange::Candle>& rows,
                                          std::int64_t intervalMs) {
    std::vector<domain::Candle> converted;
    converted.reserve(rows.size());

    for (auto candle : rows) {
        if (intervalMs > 0 && candle.openTime > 0) {
            candle.closeTime = candle.openTime + intervalMs - 1;
        } else if (candle.closeTime <= 0) {
            candle.closeTime = candle.openTime;
        }
        candle.isClosed = true;
        converted.push_back(candle);
    }

    return converted;
}

std::vector<std::string> deduplicateList(std::vector<std::string> values) {
    std::vector<std::string> unique;
    unique.reserve(values.size());

    for (auto& value : values) {
        if (value.empty()) {
            continue;
        }
        if (std::find(unique.begin(), unique.end(), value) == unique.end()) {
            unique.push_back(std::move(value));
        }
    }

    return unique;
}

}  // namespace

BackfillWorker::BackfillWorker(const ttp::common::Config& config)
    : duckdbPath_(config.duckdbPath),
      exchange_(config.backfillExchange.empty() ? std::string{"binance"} : config.backfillExchange),
      symbols_(config.backfillSymbols),
      intervals_(config.backfillIntervals),
      from_(config.backfillFrom.empty() ? std::string{"2025-08-01"} : config.backfillFrom),
      to_(config.backfillTo.empty() ? std::string{"now"} : config.backfillTo) {
    if (symbols_.empty()) {
        symbols_.emplace_back("BTCUSDT");
    }
    if (intervals_.empty()) {
        intervals_.emplace_back("1m");
    }

    for (auto& symbol : symbols_) {
        symbol = toUpper(symbol);
    }
    symbols_ = deduplicateList(std::move(symbols_));

    for (auto& interval : intervals_) {
        interval = toLower(interval);
    }
    intervals_ = deduplicateList(std::move(intervals_));

    exchange_ = toLower(exchange_);
}

void BackfillWorker::run() {
#if !defined(HAS_DUCKDB)
    LOG_WARN("BackfillWorker invocado sin soporte de DuckDB; operación no disponible.");
    return;
#else
    if (exchange_ != "binance") {
        LOG_ERR("BackfillWorker: exchange no soportado --exchange=" << exchange_);
        return;
    }

    const auto fromSecondsOpt = parseDateToSeconds(from_);
    const auto toSecondsOpt = parseToSeconds(to_);

    if (!toSecondsOpt.has_value()) {
        LOG_ERR("BackfillWorker: parámetro --to inválido '" << to_ << "'");
        return;
    }

    if (!fromSecondsOpt.has_value()) {
        LOG_WARN("BackfillWorker: parámetro --from inválido '" << from_ << "', usando valor por defecto");
    }

    std::int64_t fromSeconds = fromSecondsOpt.value_or(adapters::binance::BinanceRestClient::kDefaultFromTs);
    if (fromSeconds < adapters::binance::BinanceRestClient::kDefaultFromTs) {
        LOG_INFO("BackfillWorker: ajustando --from a mínimo soportado "
                 << adapters::binance::BinanceRestClient::kDefaultFromTs);
        fromSeconds = adapters::binance::BinanceRestClient::kDefaultFromTs;
    }

    std::int64_t toSeconds = toSecondsOpt.value();
    if (toSeconds <= fromSeconds) {
        LOG_WARN("BackfillWorker: ajustando --to " << toSeconds << " para ser mayor que --from " << fromSeconds);
        toSeconds = fromSeconds + 1;
    }

    adapters::binance::BinanceRestClient client;
    adapters::duckdb::DuckCandleRepo duckRepo(duckdbPath_);

    for (const auto& symbol : symbols_) {
        for (const auto& intervalInput : intervals_) {
            exchange::Interval exchangeInterval;
            try {
                exchangeInterval = exchange::interval_from_string(intervalInput);
            } catch (const std::exception& ex) {
                LOG_WARN("BackfillWorker: intervalo inválido '" << intervalInput << "' error=" << ex.what());
                continue;
            }

            const auto domainInterval = domain::interval_from_label(intervalInput);
            if (!domainInterval.valid()) {
                LOG_WARN("BackfillWorker: intervalo no reconocido para persistencia '" << intervalInput << "'");
                continue;
            }

            const auto intervalLabel = domain::interval_label(domainInterval);

            LOG_INFO("BackfillWorker: iniciando backfill exchange=binance symbol=" << symbol
                                                                                   << " interval=" << intervalLabel
                                                                                   << " from=" << fromSeconds
                                                                                   << " to=" << toSeconds);

            std::int64_t pageFrom = fromSeconds;
            std::size_t processed = 0;
            std::size_t lastLogged = 0;

            while (pageFrom <= toSeconds) {
                auto page = client.fetch_klines(symbol, exchangeInterval, pageFrom, toSeconds, kPageSize);
                if (page.rows.empty()) {
                    break;
                }

                auto repoRows = toDuckCandles(page.rows, domainInterval.ms);
                if (!repoRows.empty()) {
                    if (!duckRepo.upsert_batch(symbol, intervalLabel, repoRows)) {
                        LOG_WARN("BackfillWorker: fallo al persistir lote symbol=" << symbol
                                                                                   << " interval=" << intervalLabel
                                                                                   << " desde=" << pageFrom);
                    }
                    processed += repoRows.size();
                    if (processed - lastLogged >= kProgressLogInterval) {
                        LOG_INFO("BackfillWorker: progreso symbol=" << symbol << " interval=" << intervalLabel
                                                                      << " velas=" << processed);
                        lastLogged = processed;
                    }
                }

                if (page.has_more && page.next_from_ts > pageFrom) {
                    pageFrom = page.next_from_ts;
                } else {
                    if (page.has_more && page.next_from_ts <= pageFrom) {
                        LOG_WARN("BackfillWorker: sin avance para symbol=" << symbol
                                                                          << " interval=" << intervalLabel
                                                                          << " next_from=" << page.next_from_ts
                                                                          << " actual=" << pageFrom);
                    }
                    break;
                }
            }

            LOG_INFO("BackfillWorker: completado symbol=" << symbol << " interval=" << intervalLabel
                                                            << " velas procesadas=" << processed);
        }
    }
#endif
}

}  // namespace app

// Comandos E2E para verificar que la API devuelve datos reales:
// 1. Ejecutar el backfill de Binance con DuckDB (BTCUSDT y ETHUSDT):
//    ./bin/api --backfill --storage duck --exchange binance \
//      --symbols "BTCUSDT,ETHUSDT" --intervals "1m" --from 2025-08-01 --to now
// 2. Iniciar la API en modo servicio:
//    ./bin/api --log-level info --storage duck
// 3. Consultar las velas recientes de BTCUSDT (debe devolver un array no vacío):
//    curl -s "http://localhost:8080/api/v1/candles?symbol=BTCUSDT&interval=1m&limit=5" | jq .
// 4. Consultar las velas recientes de ETHUSDT (debe devolver un array no vacío):
//    curl -s "http://localhost:8080/api/v1/candles?symbol=ETHUSDT&interval=1m&limit=5" | jq .

