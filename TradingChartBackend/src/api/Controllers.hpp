#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace domain::contracts {
class ICandleReadRepo;
}

namespace ttp::api {

struct Request {
    std::string method;
    std::string target;
    std::string path;
    std::string query;
    std::string version;
    std::string body;
};

struct Response {
    int statusCode;
    std::string statusText;
    std::string body;
    std::string contentType;
    std::vector<std::pair<std::string, std::string>> headers;
};

Response healthz();

Response version();

Response candles(const Request& request);

Response symbols(const Request& request);

Response intervals(const Request& request);

Response symbolIntervals(const Request& request, const std::string& symbol);

Response stats(const Request& request);

void setCandleRepository(std::shared_ptr<const domain::contracts::ICandleReadRepo> repo);

void setHttpLimits(std::int32_t defaultLimit, std::int32_t maxLimit);

void setLiveSymbols(std::vector<std::string> symbols);

void setLiveIntervals(std::vector<std::string> intervals);

}  // namespace ttp::api
