#include "api/Router.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <optional>
#include <string_view>
#include <utility>

#include "common/Metrics.hpp"
#include "http/QueryParams.hpp"

namespace ttp::api {

namespace {

std::string makeKey(const std::string& method, const std::string& path) {
    return method + ' ' + path;
}

std::string toLowerCopy(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

}  // namespace

Router::Router() {
    routes_.emplace(makeKey("GET", "/healthz"), [](const Request&) { return healthz(); });
    routes_.emplace(makeKey("GET", "/version"), [](const Request&) { return version(); });
    routes_.emplace(makeKey("GET", "/api/v1/symbols"), [](const Request& request) { return symbols(request); });
    routes_.emplace(makeKey("GET", "/api/v1/intervals"), [](const Request& request) { return intervals(request); });
    routes_.emplace(makeKey("GET", "/api/v1/candles"), [](const Request& request) { return candles(request); });
    routes_.emplace(makeKey("GET", "/stats"), [](const Request& request) { return stats(request); });
}

Response Router::handle(const Request& request) const {
    const auto key = makeKey(request.method, request.path);
    const auto it = routes_.find(key);
    if (it != routes_.end()) {
        common::metrics::Registry::instance().incrementRequest(key);
        const bool cacheable = shouldCache(request) || shouldCacheSymbolIntervals(request);
        if (cacheable) {
            const auto cacheKey = buildCacheKey(request);
            if (auto cached = tryGetCachedResponse(cacheKey)) {
                return *cached;
            }
            auto response = it->second(request);
            storeCachedResponse(cacheKey, response);
            return response;
        }
        return it->second(request);
    }

    if (request.method == "GET") {
        static constexpr std::string_view kPrefix = "/api/v1/symbols/";
        static constexpr std::string_view kSuffix = "/intervals";

        if (request.path.size() > kPrefix.size()
            && request.path.compare(0, kPrefix.size(), kPrefix) == 0) {
            const auto remainder = request.path.substr(kPrefix.size());
            const auto slashPos = remainder.find('/');
            if (slashPos != std::string::npos) {
                const auto symbol = remainder.substr(0, slashPos);
                const auto tail = remainder.substr(slashPos);
                if (tail == kSuffix) {
                    common::metrics::Registry::instance().incrementRequest(
                        "GET /api/v1/symbols/:symbol/intervals");
                    const bool cacheable = shouldCacheSymbolIntervals(request);
                    if (cacheable) {
                        const auto cacheKey = buildCacheKey(request);
                        if (auto cached = tryGetCachedResponse(cacheKey)) {
                            return *cached;
                        }
                        auto response = symbolIntervals(request, symbol);
                        storeCachedResponse(cacheKey, response);
                        return response;
                    }
                    return symbolIntervals(request, symbol);
                }
            }
        }
    }

    return Response{404, "Not Found", R"({"error":"not_found"})", "application/json"};
}

bool Router::shouldCache(const Request& request) const {
    return request.method == "GET" && request.path == "/api/v1/symbols";
}

bool Router::shouldCacheSymbolIntervals(const Request& request) const {
    if (request.method != "GET") {
        return false;
    }
    const auto includeRanges = ttp::http::opt_string(request, "includeRanges");
    if (!includeRanges) {
        return false;
    }
    const auto normalized = toLowerCopy(*includeRanges);
    return normalized == "true" || normalized == "1" || normalized == "yes" || normalized == "on";
}

std::string Router::buildCacheKey(const Request& request) const {
    std::string key = request.method;
    key.push_back(' ');
    key.append(request.path);
    if (!request.query.empty()) {
        key.push_back('?');
        key.append(request.query);
    }
    return key;
}

std::optional<Response> Router::tryGetCachedResponse(const std::string& key) const {
    const auto now = std::chrono::steady_clock::now();
    std::lock_guard<std::mutex> lock(cacheMutex_);
    const auto it = cache_.find(key);
    if (it == cache_.end()) {
        return std::nullopt;
    }
    if (now >= it->second.expiresAt) {
        cache_.erase(it);
        return std::nullopt;
    }
    return it->second.response;
}

void Router::storeCachedResponse(const std::string& key, const Response& response) const {
    const auto expiresAt = std::chrono::steady_clock::now() + kCacheTtl;
    std::lock_guard<std::mutex> lock(cacheMutex_);
    cache_[key] = CachedResponse{response, expiresAt};
}

}  // namespace ttp::api
