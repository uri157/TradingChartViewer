#pragma once

#include <chrono>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>

#include "api/Controllers.hpp"

namespace ttp::api {

class Router {
public:
    Router();

    Response handle(const Request& request) const;

private:
    using Handler = std::function<Response(const Request&)>;

    struct CachedResponse {
        Response response;
        std::chrono::steady_clock::time_point expiresAt;
    };

    [[nodiscard]] bool shouldCache(const Request& request) const;
    [[nodiscard]] bool shouldCacheSymbolIntervals(const Request& request) const;
    [[nodiscard]] std::string buildCacheKey(const Request& request) const;
    [[nodiscard]] std::optional<Response> tryGetCachedResponse(const std::string& key) const;
    void storeCachedResponse(const std::string& key, const Response& response) const;

    std::map<std::string, Handler> routes_;
    static constexpr std::chrono::seconds kCacheTtl{10};
    mutable std::mutex cacheMutex_;
    mutable std::unordered_map<std::string, CachedResponse> cache_;
};

}  // namespace ttp::api
