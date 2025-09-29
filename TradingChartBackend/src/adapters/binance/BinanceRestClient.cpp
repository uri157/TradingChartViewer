#include "adapters/binance/BinanceRestClient.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>

#include <boost/json.hpp>

#include "adapters/binance/IntervalMap.hpp"
#include "infra/http/TlsHttpClient.hpp"
#include "logging/Log.h"

namespace {

std::int64_t json_to_int64(const boost::json::value& value) {
    if (value.is_int64()) {
        return value.as_int64();
    }
    if (value.is_uint64()) {
        return static_cast<std::int64_t>(value.as_uint64());
    }
    if (value.is_double()) {
        return static_cast<std::int64_t>(std::llround(value.as_double()));
    }
    if (value.is_string()) {
        const std::string str{value.as_string().c_str()};
        try {
            return std::stoll(str);
        } catch (const std::exception& ex) {
            throw std::runtime_error("Failed to parse integer value: " + str + ", error: " + ex.what());
        }
    }
    throw std::runtime_error("Unsupported JSON type for integer conversion");
}

double json_to_double(const boost::json::value& value) {
    if (value.is_double()) {
        return value.as_double();
    }
    if (value.is_int64()) {
        return static_cast<double>(value.as_int64());
    }
    if (value.is_uint64()) {
        return static_cast<double>(value.as_uint64());
    }
    if (value.is_string()) {
        const std::string str{value.as_string().c_str()};
        try {
            return std::stod(str);
        } catch (const std::exception& ex) {
            throw std::runtime_error("Failed to parse floating value: " + str + ", error: " + ex.what());
        }
    }
    throw std::runtime_error("Unsupported JSON type for floating conversion");
}

}  // namespace

namespace adapters::binance {

namespace {
constexpr const char* kHost = "api.binance.com";
constexpr std::size_t kMaxLimit = 1000;
constexpr int kMaxRetries = 5;
constexpr int kRateLimitPerMinute = 1200;
constexpr double kRateLimitThreshold = 0.9;
constexpr double kRateLimitThresholdValue = kRateLimitPerMinute * kRateLimitThreshold;
}

std::int64_t BinanceRestClient::interval_to_seconds(domain::Interval interval) {
    if (!interval.valid()) {
        throw std::invalid_argument("Unsupported interval");
    }
    return interval.ms / 1000;
}

domain::KlinesPage BinanceRestClient::fetch_klines(const std::string& symbol,
                                                    domain::Interval interval,
                                                    std::int64_t from_ts,
                                                    std::int64_t to_ts,
                                                    std::size_t page_limit) {
    domain::KlinesPage page;
    if (symbol.empty()) {
        return page;
    }

    if (to_ts <= 0) {
        return page;
    }

    const auto effective_from = (from_ts <= 0) ? kDefaultFromTs : from_ts;
    if (effective_from >= to_ts) {
        return page;
    }

    const auto limit = std::clamp<std::size_t>(page_limit == 0 ? kMaxLimit : page_limit, 1, kMaxLimit);
    const std::string interval_literal = binance_interval(interval);
    const auto interval_seconds = interval_to_seconds(interval);
    const std::int64_t interval_millis = interval.ms;

    std::int64_t current_start_ms = effective_from * 1000;
    const std::int64_t to_ms = to_ts * 1000;
    std::int64_t last_close_ms = 0;

    while (page.rows.size() < limit && current_start_ms < to_ms) {
        const std::size_t remaining = limit - page.rows.size();
        const std::size_t request_limit = std::min<std::size_t>(remaining, kMaxLimit);
        std::int64_t chunk_end_ms = to_ms;
        if (interval_millis > 0) {
            const auto potential_end = current_start_ms + static_cast<std::int64_t>(request_limit) * interval_millis;
            if (potential_end < chunk_end_ms) {
                chunk_end_ms = potential_end;
            }
        }

        std::ostringstream target;
        target << "/api/v3/klines?symbol=" << symbol << "&interval=" << interval_literal << "&startTime="
               << current_start_ms << "&endTime=" << chunk_end_ms << "&limit=" << request_limit;

        const std::string request_target = target.str();
        LOG_INFO(logging::LogCategory::NET, "Binance REST %s", request_target.c_str());

        infra::http::JsonResponse response;
        bool request_success = false;
        for (int attempt = 1; attempt <= kMaxRetries; ++attempt) {
            response = infra::http::https_get_json_response(kHost, request_target);
            const unsigned status = response.status;
            if (status == 200U) {
                request_success = true;
                break;
            }

            if (status == 429U || (status >= 500U && status < 600U)) {
                if (attempt == kMaxRetries) {
                    std::ostringstream oss;
                    oss << "Binance REST request " << request_target << " failed after " << kMaxRetries
                        << " attempts with HTTP " << status;
                    throw std::runtime_error(oss.str());
                }
                const auto backoff = std::chrono::seconds(1LL << (attempt - 1));
                LOG_WARN(logging::LogCategory::NET,
                         "Binance REST backoff attempt %d due to HTTP %u, sleeping %lld ms",
                         attempt, status, static_cast<long long>(std::chrono::duration_cast<std::chrono::milliseconds>(backoff).count()));
                std::this_thread::sleep_for(backoff);
                continue;
            }

            std::ostringstream oss;
            oss << "Binance REST request " << request_target << " returned unexpected HTTP " << status;
            throw std::runtime_error(oss.str());
        }

        if (!request_success) {
            throw std::runtime_error("Binance REST request failed without success response");
        }

        boost::json::value json;
        try {
            json = boost::json::parse(response.body);
        } catch (const std::exception& ex) {
            throw std::runtime_error(std::string{"Failed to parse Binance response: "} + ex.what());
        }

        if (!json.is_array()) {
            throw std::runtime_error("Unexpected Binance response type (expected array)");
        }

        const auto& outer = json.as_array();
        if (outer.empty()) {
            LOG_WARN(logging::LogCategory::NET,
                     "Binance returned empty klines for %s from %lld to %lld", symbol.c_str(),
                     static_cast<long long>(current_start_ms / 1000), static_cast<long long>(chunk_end_ms / 1000));
            break;
        }

        for (const auto& row_value : outer) {
            if (!row_value.is_array()) {
                throw std::runtime_error("Unexpected Binance kline row type");
            }
            const auto& row = row_value.as_array();
            if (row.size() < 7) {
                throw std::runtime_error("Incomplete Binance kline row");
            }

            const std::int64_t open_ms = json_to_int64(row.at(0));
            const std::int64_t close_ms = json_to_int64(row.at(6));
            if (close_ms > to_ms) {
                continue;
            }
            if (!page.rows.empty() && open_ms <= page.rows.back().openTime) {
                continue;
            }

            domain::Candle candle{};
            candle.openTime = open_ms;
            candle.closeTime = close_ms;
            candle.open = json_to_double(row.at(1));
            candle.high = json_to_double(row.at(2));
            candle.low = json_to_double(row.at(3));
            candle.close = json_to_double(row.at(4));
            candle.baseVolume = json_to_double(row.at(5));
            if (row.size() > 7) {
                candle.quoteVolume = json_to_double(row.at(7));
            }
            if (row.size() > 8) {
                candle.trades = static_cast<domain::TradeCount>(json_to_int64(row.at(8)));
            }
            candle.isClosed = true;

            page.rows.push_back(candle);
            last_close_ms = close_ms;

            if (page.rows.size() >= limit) {
                break;
            }
        }

        if (last_close_ms == 0) {
            break;
        }

        current_start_ms = last_close_ms + 1;

        if (!response.used_weight_header.empty()) {
            try {
                const int used_weight = std::stoi(response.used_weight_header);
                if (static_cast<double>(used_weight) > kRateLimitThresholdValue && page.rows.size() < limit &&
                    current_start_ms < to_ms) {
                    std::this_thread::sleep_for(std::chrono::seconds(2));
                }
            } catch (const std::exception&) {
                // Ignore malformed header values; continue without throttling.
            }
        }
    }

    if (!page.rows.empty() && page.rows.size() >= limit) {
        if (last_close_ms > 0 && last_close_ms < to_ms) {
            page.has_more = true;
            page.next_from_ts = (last_close_ms + 1) / 1000;
        }
    }

    return page;
}

}  // namespace adapters::binance

