#include "api/Controllers.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cctype>
#include <iomanip>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <stdexcept>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "app/ServiceLocator.hpp"
#include "common/Metrics.hpp"
#include "domain/Models.hpp"
#include "domain/Ports.hpp"
#include "http/ErrorCodes.hpp"
#include "http/HttpJson.hpp"
#include "http/json_error.hpp"
#include "http/QueryParams.hpp"
#include "http/Validation.hpp"
#include "logging/Log.h"

#include <boost/json/array.hpp>
#include <boost/json/object.hpp>
#include <boost/json/value.hpp>

namespace ttp::api {

namespace {

constexpr logging::LogCategory kLogCategory = logging::LogCategory::DATA;
constexpr std::int64_t kMillisecondsThreshold = 1'000'000'000'000LL;
constexpr char kSymbolsRouteKey[] = "GET /api/v1/symbols";
constexpr char kSymbolIntervalsRouteKey[] = "GET /api/v1/symbols/:symbol/intervals";
constexpr char kCandlesRouteKey[] = "GET /api/v1/candles";
constexpr char kIntervalsRouteKey[] = "GET /api/v1/intervals";

struct HttpLimitState {
    std::atomic<std::int32_t> defaultLimit{600};
    std::atomic<std::int32_t> maxLimit{5000};
};

HttpLimitState& httpLimitState() {
    static HttpLimitState state;
    return state;
}

std::mutex& liveSymbolsMutex() {
    static std::mutex mutex;
    return mutex;
}

std::vector<std::string>& liveSymbolsStorage() {
    static std::vector<std::string> symbols;
    return symbols;
}

std::vector<std::string> liveSymbolsSnapshot() {
    std::lock_guard<std::mutex> lock(liveSymbolsMutex());
    return liveSymbolsStorage();
}

std::mutex& liveIntervalsMutex() {
    static std::mutex mutex;
    return mutex;
}

std::vector<std::string>& liveIntervalsStorage() {
    static std::vector<std::string> intervals;
    return intervals;
}

std::vector<std::string> liveIntervalsSnapshot() {
    std::lock_guard<std::mutex> lock(liveIntervalsMutex());
    return liveIntervalsStorage();
}

std::int64_t normalize_timestamp_ms(std::int64_t ts) {
    if (ts > 0 && ts < kMillisecondsThreshold) {
        return ts * 1000LL;
    }
    return ts;
}

std::string to_lower_copy(std::string_view value) {
    std::string lowered;
    lowered.reserve(value.size());
    for (unsigned char ch : value) {
        lowered.push_back(static_cast<char>(std::tolower(ch)));
    }
    return lowered;
}

std::string to_upper_copy(std::string_view value) {
    std::string upper;
    upper.reserve(value.size());
    for (unsigned char ch : value) {
        upper.push_back(static_cast<char>(std::toupper(ch)));
    }
    return upper;
}

bool parse_active_filter(std::optional<std::string> raw) {
    if (!raw) {
        return true;
    }

    const auto normalized = to_lower_copy(*raw);
    if (normalized == "false" || normalized == "0" || normalized == "no" || normalized == "off") {
        return false;
    }
    if (normalized == "true" || normalized == "1" || normalized == "yes" || normalized == "on") {
        return true;
    }
    return true;
}

bool parse_boolean(std::optional<std::string> raw, bool defaultValue) {
    if (!raw) {
        return defaultValue;
    }

    const auto normalized = to_lower_copy(*raw);
    if (normalized == "false" || normalized == "0" || normalized == "no" || normalized == "off") {
        return false;
    }
    if (normalized == "true" || normalized == "1" || normalized == "yes" || normalized == "on") {
        return true;
    }
    return defaultValue;
}

constexpr std::array<std::string_view, 46> kKnownQuoteAssets = {
    "FDUSD",
    "USDT",
    "USDC",
    "BUSD",
    "TUSD",
    "USDP",
    "BIDR",
    "USDD",
    "DAI",
    "EUR",
    "USD",
    "BRL",
    "TRY",
    "BTC",
    "ETH",
    "BNB",
    "RUB",
    "GBP",
    "AUD",
    "ARS",
    "COP",
    "PEN",
    "JPY",
    "KRW",
    "ZAR",
    "PLN",
    "CHF",
    "MXN",
    "CAD",
    "SGD",
    "HKD",
    "CZK",
    "HUF",
    "ILS",
    "SEK",
    "NOK",
    "DKK",
    "CLP",
    "PHP",
    "IDR",
    "THB",
    "NGN",
    "UAH",
    "VND",
    "SAR",
    "AED",
};

std::optional<std::pair<std::string, std::string>> infer_base_quote(const std::string& symbol) {
    if (symbol.empty()) {
        return std::nullopt;
    }

    const auto upperSymbol = to_upper_copy(symbol);
    for (const auto& quote : kKnownQuoteAssets) {
        if (upperSymbol.size() <= quote.size()) {
            continue;
        }
        if (upperSymbol.compare(upperSymbol.size() - quote.size(), quote.size(), quote) == 0) {
            std::string base = symbol.substr(0, symbol.size() - quote.size());
            if (base.empty()) {
                continue;
            }
            return std::make_pair(std::move(base), std::string{quote});
        }
    }
    return std::nullopt;
}

bool matches_query(const std::string& value, const std::string& queryLower) {
    if (value.empty() || queryLower.empty()) {
        return queryLower.empty();
    }
    const auto lowered = to_lower_copy(value);
    return lowered.find(queryLower) != std::string::npos;
}

const domain::contracts::ICandleReadRepo& repo() {
    const auto* repoHandle = app::ServiceLocator::instance().candleReadRepo();
    if (!repoHandle) {
        throw std::runtime_error("Candle repository not configured");
    }
    return *repoHandle;
}

double sanitize_value(double value) {
    if (std::isfinite(value)) {
        return value;
    }
    return 0.0;
}

std::optional<bool> lookup_symbol(const std::string& symbol) {
    const auto* repoHandle = app::ServiceLocator::instance().candleReadRepo();
    if (!repoHandle) {
        return std::nullopt;
    }
    try {
        return repoHandle->symbolExists(symbol);
    }
    catch (const std::exception& ex) {
        LOG_WARN(kLogCategory,
                 "Controllers::candles symbolExists check failed symbol=%s error=%s",
                 symbol.c_str(),
                 ex.what());
        return std::nullopt;
    }
}

Response makeJsonResponse(int statusCode, std::string statusText, std::string body) {
    return Response{statusCode, std::move(statusText), std::move(body), "application/json"};
}

std::string escapeJsonString(std::string_view value) {
    std::string escaped;
    escaped.reserve(value.size());
    for (const unsigned char ch : value) {
        switch (ch) {
        case '\\':
            escaped += "\\\\";
            break;
        case '"':
            escaped += "\\\"";
            break;
        case '\b':
            escaped += "\\b";
            break;
        case '\f':
            escaped += "\\f";
            break;
        case '\n':
            escaped += "\\n";
            break;
        case '\r':
            escaped += "\\r";
            break;
        case '\t':
            escaped += "\\t";
            break;
        default:
            if (ch < 0x20) {
                std::ostringstream oss;
                oss << "\\u" << std::hex << std::uppercase << std::setw(4) << std::setfill('0')
                    << static_cast<int>(ch);
                escaped += oss.str();
            }
            else {
                escaped.push_back(static_cast<char>(ch));
            }
            break;
        }
    }
    return escaped;
}

}  // namespace

Response healthz() {
    constexpr auto kDefaultIntervalMs = 60'000.0;
    constexpr auto kWsDownGrace = std::chrono::seconds(120);

    const auto snapshot = common::metrics::Registry::instance().snapshot();

    double intervalMs = kDefaultIntervalMs;
    if (const auto it = snapshot.gauges.find("interval_ms"); it != snapshot.gauges.end()) {
        if (it->second.value > 0.0) {
            intervalMs = it->second.value;
        }
    }

    double lastMsgAgeMs = 0.0;
    if (const auto it = snapshot.gauges.find("last_msg_age_ms"); it != snapshot.gauges.end()) {
        if (it->second.updatedAt != std::chrono::steady_clock::time_point{}) {
            lastMsgAgeMs = std::chrono::duration_cast<std::chrono::duration<double, std::milli>>(snapshot.capturedAt
                                                                                                - it->second.updatedAt)
                               .count();
        }
        else {
            lastMsgAgeMs = it->second.value;
        }
    }

    double wsState = 1.0;
    std::chrono::duration<double> wsDownDuration{0.0};
    if (const auto it = snapshot.gauges.find("ws_state"); it != snapshot.gauges.end()) {
        wsState = it->second.value;
        if (it->second.zeroSince.has_value()) {
            wsDownDuration = snapshot.capturedAt - *it->second.zeroSince;
        }
    }

    const bool staleLastMessage = intervalMs > 0.0 && lastMsgAgeMs > 3.0 * intervalMs;
    const bool wsDownTooLong = wsState < 0.5 && wsDownDuration > kWsDownGrace;

    if (!staleLastMessage && !wsDownTooLong) {
        return makeJsonResponse(200, "OK", R"({"status":"ok"})");
    }

    std::ostringstream oss;
    oss << '{';
    oss << "\"status\":\"error\",\"details\":[";

    bool first = true;
    if (staleLastMessage) {
        oss << '{';
        oss << "\"issue\":\"stale_last_message\",";
        oss << "\"last_msg_age_ms\":" << lastMsgAgeMs << ',';
        oss << "\"interval_ms\":" << intervalMs;
        oss << '}';
        first = false;
    }
    if (wsDownTooLong) {
        if (!first) {
            oss << ',';
        }
        oss << '{';
        oss << "\"issue\":\"ws_down\",";
        oss << "\"duration_seconds\":"
            << std::chrono::duration_cast<std::chrono::duration<double>>(wsDownDuration).count();
        oss << '}';
    }
    oss << "]}";

    return Response{500, "Service Unavailable", oss.str(), "application/json"};
}

Response version() {
    return makeJsonResponse(200, "OK", R"({"name":"ttp-backend","version":"0.1.0"})");
}

Response symbols(const Request& request) {
    common::metrics::Registry::ScopedTimer requestTimer(kSymbolsRouteKey);

    Response response{};

    const bool activeOnly = parse_active_filter(ttp::http::opt_string(request, "active"));
    const auto queryOpt = ttp::http::opt_string(request, "q");
    const std::string queryLower = queryOpt ? to_lower_copy(*queryOpt) : std::string{};
    const char* queryLog = queryOpt ? queryOpt->c_str() : "";

    LOG_INFO(kLogCategory,
             "Controllers::symbols activeOnly=%s query=\"%s\"",
             activeOnly ? "true" : "false",
             queryLog);

    struct SymbolEntry {
        std::string symbol;
        std::optional<std::string> base;
        std::optional<std::string> quote;
        bool active{false};
    };

    const auto live = liveSymbolsSnapshot();

    std::unordered_map<std::string, SymbolEntry> merged;
    merged.reserve(live.size() + 64);

    for (const auto& symbol : live) {
        if (symbol.empty()) {
            continue;
        }
        auto& entry = merged[symbol];
        if (entry.symbol.empty()) {
            entry.symbol = symbol;
        }
        entry.active = true;
    }

    std::vector<domain::contracts::SymbolInfo> catalogSymbols;
    if (const auto* repoHandle = app::ServiceLocator::instance().candleReadRepo()) {
        try {
            catalogSymbols = repoHandle->listSymbols();
        }
        catch (const std::exception& ex) {
            LOG_WARN(kLogCategory,
                     "Controllers::symbols catalog fetch failed error=%s",
                     ex.what());
        }
    }

    for (auto& info : catalogSymbols) {
        if (info.symbol.empty()) {
            continue;
        }
        auto& entry = merged[info.symbol];
        if (entry.symbol.empty()) {
            entry.symbol = info.symbol;
        }
        if (info.base && (!entry.base || entry.base->empty())) {
            entry.base = *info.base;
        }
        if (info.quote && (!entry.quote || entry.quote->empty())) {
            entry.quote = *info.quote;
        }
    }

    std::vector<SymbolEntry> filtered;
    filtered.reserve(merged.size());

    for (auto& [_, entry] : merged) {
        if (!entry.base || !entry.quote) {
            if (const auto inferred = infer_base_quote(entry.symbol)) {
                if (!entry.base) {
                    entry.base = inferred->first;
                }
                if (!entry.quote) {
                    entry.quote = inferred->second;
                }
            }
        }

        if (activeOnly && !entry.active) {
            continue;
        }
        if (!queryLower.empty()) {
            bool matches = matches_query(entry.symbol, queryLower);
            if (!matches && entry.base) {
                matches = matches_query(*entry.base, queryLower);
            }
            if (!matches && entry.quote) {
                matches = matches_query(*entry.quote, queryLower);
            }
            if (!matches) {
                continue;
            }
        }
        filtered.push_back(std::move(entry));
    }

    std::sort(filtered.begin(), filtered.end(), [](const auto& lhs, const auto& rhs) {
        return lhs.symbol < rhs.symbol;
    });

    LOG_DEBUG(kLogCategory,
              "Controllers::symbols live=%zu catalog=%zu merged=%zu filtered=%zu query=\"%s\"",
              live.size(),
              catalogSymbols.size(),
              merged.size(),
              filtered.size(),
              queryLog);

    boost::json::array items;
    items.reserve(filtered.size());
    for (const auto& entry : filtered) {
        boost::json::object row;
        row["symbol"] = entry.symbol;
        if (entry.base) {
            row["base"] = *entry.base;
        }
        if (entry.quote) {
            row["quote"] = *entry.quote;
        }
        row["status"] = entry.active ? "active" : "inactive";
        items.emplace_back(std::move(row));
    }

    boost::json::object payload;
    payload["symbols"] = std::move(items);

    ttp::http::write_json(response, payload);
    return response;
}

Response makeIntervalsResponse(const Request& request,
                               const std::string& symbolParam,
                               const char* logPrefix) {
    Response response{};

    const std::string symbol = symbolParam;
    const bool includeRanges = parse_boolean(ttp::http::opt_string(request, "includeRanges"), false);

    LOG_INFO(kLogCategory,
             "%s symbol=%s includeRanges=%s",
             logPrefix,
             symbol.c_str(),
             includeRanges ? "true" : "false");

    static constexpr std::array<std::string_view, 4> kSupportedIntervals{"1m", "5m", "1h", "1d"};

    const auto liveSymbols = liveSymbolsSnapshot();
    const bool isLiveSymbol = std::find(liveSymbols.begin(), liveSymbols.end(), symbol) != liveSymbols.end();

    const auto configuredIntervals = liveIntervalsSnapshot();
    std::unordered_set<std::string> allowedIntervals;
    allowedIntervals.reserve(configuredIntervals.size());
    for (const auto& interval : configuredIntervals) {
        if (!interval.empty()) {
            allowedIntervals.emplace(to_lower_copy(interval));
        }
    }

    std::vector<std::string> responseIntervals;
    responseIntervals.reserve(kSupportedIntervals.size());
    for (const auto intervalNameView : kSupportedIntervals) {
        std::string intervalName(intervalNameView);
        if (!allowedIntervals.empty()) {
            const auto normalized = to_lower_copy(intervalName);
            if (allowedIntervals.find(normalized) == allowedIntervals.end()) {
                continue;
            }
        }
        responseIntervals.push_back(std::move(intervalName));
    }

    if (responseIntervals.empty()) {
        responseIntervals.reserve(kSupportedIntervals.size());
        for (const auto intervalNameView : kSupportedIntervals) {
            responseIntervals.emplace_back(intervalNameView);
        }
    }

    const auto* repoHandle = app::ServiceLocator::instance().candleReadRepo();
    bool knownSymbol = isLiveSymbol;

    if (repoHandle && !knownSymbol) {
        try {
            if (const auto exists = repoHandle->symbolExists(symbol)) {
                knownSymbol = *exists;
            }
        }
        catch (const std::exception& ex) {
            LOG_WARN(kLogCategory,
                     "%s symbolExists check failed symbol=%s error=%s",
                     logPrefix,
                     symbol.c_str(),
                     ex.what());
        }

        if (!knownSymbol) {
            try {
                const auto catalogSymbols = repoHandle->listSymbols();
                knownSymbol = std::any_of(
                    catalogSymbols.begin(), catalogSymbols.end(), [&symbol](const auto& info) {
                        return info.symbol == symbol;
                    });
            }
            catch (const std::exception& ex) {
                LOG_WARN(kLogCategory,
                         "%s catalog lookup failed symbol=%s error=%s",
                         logPrefix,
                         symbol.c_str(),
                         ex.what());
            }
        }
    }

    if (!knownSymbol) {
        LOG_INFO(kLogCategory, "%s symbol=%s not found", logPrefix, symbol.c_str());
        ttp::http::json_error(response, 404, ttp::http::errors::symbol_not_found);
        return response;
    }

    boost::json::object payload;
    payload["symbol"] = symbol;

    if (includeRanges && !repoHandle) {
        LOG_WARN(kLogCategory,
                 "%s includeRanges requested but repository unavailable symbol=%s",
                 logPrefix,
                 symbol.c_str());
    }

    if (includeRanges) {
        boost::json::array items;
        items.reserve(responseIntervals.size());
        std::size_t rangeCount = 0;
        std::size_t missingRanges = 0;

        for (const auto& intervalName : responseIntervals) {
            boost::json::object row;
            row["name"] = intervalName;

            if (repoHandle) {
                try {
                    const auto range = repoHandle->get_min_max_ts(symbol, intervalName);
                    if (range && range->first <= range->second) {
                        row["from"] = range->first;
                        row["to"] = range->second;
                        ++rangeCount;
                        LOG_DEBUG(kLogCategory,
                                  "%s ranges symbol=%s interval=%s from=%lld to=%lld",
                                  logPrefix,
                                  symbol.c_str(),
                                  intervalName.c_str(),
                                  static_cast<long long>(range->first),
                                  static_cast<long long>(range->second));
                    }
                    else {
                        ++missingRanges;
                        LOG_WARN(kLogCategory,
                                 "%s ranges empty or failed symbol=%s interval=%s",
                                 logPrefix,
                                 symbol.c_str(),
                                 intervalName.c_str());
                    }
                }
                catch (const std::exception& ex) {
                    ++missingRanges;
                    LOG_WARN(kLogCategory,
                             "%s ranges exception symbol=%s interval=%s error=%s",
                             logPrefix,
                             symbol.c_str(),
                             intervalName.c_str(),
                             ex.what());
                }
            }

            items.emplace_back(std::move(row));
        }

        const std::size_t totalIntervals = responseIntervals.size();
        if (rangeCount == 0) {
            LOG_WARN(kLogCategory,
                     "%s includeRanges requested but no ranges available symbol=%s",
                     logPrefix,
                     symbol.c_str());
        }
        else {
            LOG_DEBUG(kLogCategory,
                      "%s symbol=%s includeRanges total=%zu withRanges=%zu",
                      logPrefix,
                      symbol.c_str(),
                      totalIntervals,
                      rangeCount);
        }

        if (missingRanges > 0 && rangeCount > 0) {
            LOG_WARN(kLogCategory,
                     "%s includeRanges partial data symbol=%s missing=%zu",
                     logPrefix,
                     symbol.c_str(),
                     missingRanges);
        }

        payload["intervals"] = std::move(items);
    }
    else {
        boost::json::array items;
        items.reserve(responseIntervals.size());
        for (const auto& intervalName : responseIntervals) {
            items.emplace_back(intervalName);
        }
        const std::size_t intervalCount = responseIntervals.size();
        payload["intervals"] = std::move(items);

        LOG_DEBUG(kLogCategory,
                  "%s symbol=%s includeRanges=false intervals=%zu",
                  logPrefix,
                  symbol.c_str(),
                  intervalCount);
    }

    ttp::http::write_json(response, payload);
    return response;
}

Response symbolIntervals(const Request& request, const std::string& symbolPath) {
    common::metrics::Registry::ScopedTimer requestTimer(kSymbolIntervalsRouteKey);

    if (symbolPath.empty()) {
        Response response{};
        ttp::http::json_error(response, 404, ttp::http::errors::symbol_not_found);
        return response;
    }

    return makeIntervalsResponse(request, symbolPath, "Controllers::symbolIntervals");
}

Response intervals(const Request& request) {
    common::metrics::Registry::ScopedTimer requestTimer(kIntervalsRouteKey);

    Response response{};

    const auto symbolOpt = ttp::http::opt_string(request, "symbol");
    if (!symbolOpt || symbolOpt->empty()) {
        LOG_WARN(kLogCategory,
                 "Controllers::intervals missing symbol query=%s",
                 request.query.c_str());
        ttp::http::json_error(response, 400, ttp::http::errors::symbol_required);
        return response;
    }

    return makeIntervalsResponse(request, *symbolOpt, "Controllers::intervals");
}

Response candles(const Request& request) {
    common::metrics::Registry::ScopedTimer requestTimer(kCandlesRouteKey);

    Response response{};

    const auto symbolOpt = ttp::http::opt_string(request, "symbol");
    if (!symbolOpt || symbolOpt->empty()) {
        LOG_WARN(kLogCategory,
                 "Controllers::candles missing symbol query=%s",
                 request.query.c_str());
        ttp::http::json_error(response, 400, ttp::http::errors::symbol_required);
        return response;
    }
    const std::string symbol = *symbolOpt;

    const auto intervalParam = ttp::http::opt_string(request, "interval");
    if (!intervalParam || !ttp::http::validation::is_valid_interval(*intervalParam)) {
        LOG_WARN(kLogCategory,
                 "Controllers::candles invalid interval query=%s",
                 request.query.c_str());
        ttp::http::json_error(response, 400, ttp::http::errors::interval_invalid);
        return response;
    }

    const auto interval = domain::contracts::intervalFromString(*intervalParam);
    const std::string intervalLabel = domain::contracts::intervalToString(interval);
    const auto* repoHandle = app::ServiceLocator::instance().candleReadRepo();

    const auto maxLimitConfig = std::max<std::int32_t>(
        1, httpLimitState().maxLimit.load(std::memory_order_relaxed));
    std::int32_t defaultLimitConfig = std::max<std::int32_t>(
        1, httpLimitState().defaultLimit.load(std::memory_order_relaxed));
    if (defaultLimitConfig > maxLimitConfig) {
        defaultLimitConfig = maxLimitConfig;
    }

    std::int32_t limitValue = defaultLimitConfig;
    if (const auto rawLimit = ttp::http::opt_string(request, "limit")) {
        if (const auto parsed = ttp::http::opt_int(request, "limit")) {
            limitValue = *parsed;
        }
        else {
            LOG_WARN(kLogCategory,
                     "Controllers::candles invalid limit query=%s",
                     request.query.c_str());
            ttp::http::json_error(response, 400, ttp::http::errors::limit_invalid);
            return response;
        }
    }

    if (limitValue < 1) {
        limitValue = 1;
    }
    if (limitValue > maxLimitConfig) {
        // Clamp to configured max per API specification instead of returning an error.
        limitValue = maxLimitConfig;
    }

    bool fromProvided = false;
    bool toProvided = false;
    std::int64_t fromMs = 0;
    std::int64_t toMs = 0;

    if (const auto fromRaw = ttp::http::opt_string(request, "from")) {
        fromProvided = true;
        if (const auto parsed = ttp::http::opt_int64(request, "from")) {
            fromMs = normalize_timestamp_ms(*parsed);
        }
        else {
            LOG_WARN(kLogCategory,
                     "Controllers::candles invalid from query=%s",
                     request.query.c_str());
            ttp::http::json_error(response, 400, ttp::http::errors::time_range_invalid);
            return response;
        }
    }

    if (const auto toRaw = ttp::http::opt_string(request, "to")) {
        toProvided = true;
        if (const auto parsed = ttp::http::opt_int64(request, "to")) {
            toMs = normalize_timestamp_ms(*parsed);
        }
        else {
            LOG_WARN(kLogCategory,
                     "Controllers::candles invalid to query=%s",
                     request.query.c_str());
            ttp::http::json_error(response, 400, ttp::http::errors::time_range_invalid);
            return response;
        }
    }

    if ((fromProvided && fromMs < 0) || (toProvided && toMs < 0)) {
        LOG_WARN(kLogCategory,
                 "Controllers::candles negative timestamp query=%s",
                 request.query.c_str());
        ttp::http::json_error(response, 400, ttp::http::errors::time_range_invalid);
        return response;
    }

    if (fromProvided && toProvided && fromMs > toMs) {
        LOG_WARN(kLogCategory,
                 "Controllers::candles from greater than to query=%s",
                 request.query.c_str());
        ttp::http::json_error(response, 400, ttp::http::errors::time_range_invalid);
        return response;
    }

    const bool hasRange = fromProvided || toProvided;

    bool skipQuery = false;
    if (hasRange && repoHandle) {
        try {
            if (const auto minMax = repoHandle->get_min_max_ts(symbol, intervalLabel)) {
                const auto minTs = minMax->first;
                const auto maxTs = minMax->second;
                if (fromProvided) {
                    const auto clampedFrom = std::max(fromMs, minTs);
                    if (clampedFrom != fromMs) {
                        fromMs = clampedFrom;
                    }
                }
                if (toProvided) {
                    const auto clampedTo = std::min(toMs, maxTs);
                    if (clampedTo != toMs) {
                        toMs = clampedTo;
                    }
                }
                if (fromProvided && toProvided && fromMs > toMs) {
                    skipQuery = true;
                }
            }
        }
        catch (const std::exception& ex) {
            LOG_WARN(kLogCategory,
                     "Controllers::candles clamp lookup failed symbol=%s interval=%s error=%s",
                     symbol.c_str(),
                     intervalLabel.c_str(),
                     ex.what());
        }
    }

    std::vector<domain::contracts::Candle> candles;
    if (!skipQuery) {
        try {
            const auto limitSize = static_cast<std::size_t>(limitValue);
            const auto fromQuery = fromProvided ? fromMs : 0;
            const auto toQuery = toProvided ? toMs : 0;
            candles = repo().getCandles(symbol, interval, fromQuery, toQuery, limitSize);
        }
        catch (const std::exception& ex) {
            LOG_ERROR(kLogCategory,
                      "Controllers::candles database error symbol=%s interval=%s error=%s",
                      symbol.c_str(),
                      intervalLabel.c_str(),
                      ex.what());
            ttp::http::json_error(response, 500, ttp::http::errors::internal_error);
            return response;
        }
    }

    std::sort(candles.begin(), candles.end(), [](const auto& lhs, const auto& rhs) {
        return lhs.ts < rhs.ts;
    });

    if (!candles.empty() && candles.size() > static_cast<std::size_t>(limitValue)) {
        const auto overflow = candles.size() - static_cast<std::size_t>(limitValue);
        candles.erase(
            candles.begin(),
            candles.begin()
                + static_cast<std::vector<domain::contracts::Candle>::difference_type>(overflow));
    }

    if (candles.empty()) {
        if (const auto exists = lookup_symbol(symbol); exists.has_value() && !*exists) {
            ttp::http::json_error(response, 404, ttp::http::errors::symbol_not_found);
            return response;
        }
    }

    boost::json::object payload;
    payload["symbol"] = symbol;
    payload["interval"] = intervalLabel;

    boost::json::array data;
    data.reserve(candles.size());
    for (const auto& candle : candles) {
        boost::json::array row;
        row.reserve(6);
        row.emplace_back(normalize_timestamp_ms(candle.ts));
        row.emplace_back(sanitize_value(candle.o));
        row.emplace_back(sanitize_value(candle.h));
        row.emplace_back(sanitize_value(candle.l));
        row.emplace_back(sanitize_value(candle.c));
        row.emplace_back(sanitize_value(candle.v));
        data.emplace_back(std::move(row));
    }

    payload["data"] = std::move(data);

    ttp::http::write_json(response, payload);

    const auto fromLog = hasRange ? fromMs : 0;
    const auto toLog = hasRange ? toMs : 0;
    LOG_INFO(kLogCategory,
             "Controllers::candles symbol=%s interval=%s from=%lld to=%lld limit=%d result=%zu",
             symbol.c_str(),
             intervalLabel.c_str(),
             static_cast<long long>(fromLog),
             static_cast<long long>(toLog),
             limitValue,
             static_cast<std::size_t>(candles.size()));

    return response;
}

Response stats(const Request&) {
    const auto snapshot = common::metrics::Registry::instance().snapshot();
    const auto uptimeSeconds = std::chrono::duration_cast<std::chrono::duration<double>>(snapshot.capturedAt - snapshot.startTime)
                                  .count();

    auto threadCount = std::thread::hardware_concurrency();
    if (threadCount == 0U) {
        threadCount = 1U;
    }

    const bool backendActive = app::ServiceLocator::instance().candleReadRepo() != nullptr;

    std::ostringstream oss;
    oss << '{';
    oss << "\"uptime_seconds\":" << uptimeSeconds << ',';
    oss << "\"threads\":" << threadCount << ',';
    oss << "\"backend_active\":" << (backendActive ? "true" : "false") << ',';
    const auto reconnectAttemptsIt = snapshot.counters.find("reconnect_attempts_total");
    const auto reconnectAttempts = reconnectAttemptsIt != snapshot.counters.end()
        ? reconnectAttemptsIt->second.value
        : 0ULL;
    const auto restCatchupIt = snapshot.counters.find("rest_catchup_candles_total");
    const auto restCatchup = restCatchupIt != snapshot.counters.end() ? restCatchupIt->second.value : 0ULL;
    oss << "\"reconnect_attempts_total\":" << reconnectAttempts << ',';
    oss << "\"rest_catchup_candles_total\":" << restCatchup << ',';

    double wsState = 0.0;
    if (const auto it = snapshot.gauges.find("ws_state"); it != snapshot.gauges.end()) {
        wsState = it->second.value;
    }

    double lastMsgAgeMs = 0.0;
    if (const auto it = snapshot.gauges.find("last_msg_age_ms"); it != snapshot.gauges.end()) {
        if (it->second.updatedAt != std::chrono::steady_clock::time_point{}) {
            lastMsgAgeMs = std::chrono::duration_cast<std::chrono::duration<double, std::milli>>(snapshot.capturedAt
                                                                                                - it->second.updatedAt)
                               .count();
        }
        else {
            lastMsgAgeMs = it->second.value;
        }
    }

    oss << "\"ws_state\":" << wsState << ',';
    oss << "\"last_msg_age_ms\":" << lastMsgAgeMs << ',';
    oss << "\"routes\":{";

    bool firstRoute = true;
    for (const auto& [route, metrics] : snapshot.routes) {
        if (!firstRoute) {
            oss << ',';
        }
        firstRoute = false;

        oss << '"' << escapeJsonString(route) << "\":{";
        oss << "\"requests\":" << metrics.totalRequests;
        if (metrics.p95Ms.has_value()) {
            oss << ",\"p95_ms\":" << *metrics.p95Ms;
        }
        if (metrics.p99Ms.has_value()) {
            oss << ",\"p99_ms\":" << *metrics.p99Ms;
        }
        oss << '}';
    }

    oss << "}}";

    return makeJsonResponse(200, "OK", oss.str());
}

void setCandleRepository(std::shared_ptr<const domain::contracts::ICandleReadRepo> repoHandle) {
    app::ServiceLocator::instance().setCandleReadRepo(std::move(repoHandle));
}

void setHttpLimits(std::int32_t defaultLimit, std::int32_t maxLimit) {
    if (maxLimit < 1) {
        maxLimit = 1;
    }
    if (defaultLimit < 1) {
        defaultLimit = 1;
    }
    if (defaultLimit > maxLimit) {
        defaultLimit = maxLimit;
    }

    auto& state = httpLimitState();
    state.maxLimit.store(maxLimit, std::memory_order_relaxed);
    state.defaultLimit.store(defaultLimit, std::memory_order_relaxed);
}

void setLiveSymbols(std::vector<std::string> symbols) {
    std::unordered_set<std::string> seen;
    seen.reserve(symbols.size());

    std::vector<std::string> sanitized;
    sanitized.reserve(symbols.size());

    for (auto& symbol : symbols) {
        if (symbol.empty()) {
            continue;
        }
        if (seen.emplace(symbol).second) {
            sanitized.push_back(std::move(symbol));
        }
    }

    {
        std::lock_guard<std::mutex> lock(liveSymbolsMutex());
        liveSymbolsStorage() = std::move(sanitized);
    }
}

void setLiveIntervals(std::vector<std::string> intervals) {
    std::unordered_set<std::string> seen;
    seen.reserve(intervals.size());

    std::vector<std::string> sanitized;
    sanitized.reserve(intervals.size());

    for (auto& interval : intervals) {
        if (interval.empty()) {
            continue;
        }
        auto normalized = to_lower_copy(interval);
        if (seen.emplace(normalized).second) {
            sanitized.push_back(std::move(normalized));
        }
    }

    {
        std::lock_guard<std::mutex> lock(liveIntervalsMutex());
        liveIntervalsStorage() = std::move(sanitized);
    }
}

}  // namespace ttp::api
