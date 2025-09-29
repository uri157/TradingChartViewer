#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace core::app {
class RangeService;
class SnapshotService;
}  // namespace core::app

namespace adapters::api::rest {

struct RestRequest {
    std::string method;
    std::string target;
};

struct RestResponse {
    int status{503};
    std::string contentType{"application/json"};
    std::vector<std::pair<std::string, std::string>> headers{};
    std::string body{"{\"error\":\"REST API disabled\"}"};
};

class RestServer {
public:
    RestServer(core::app::SnapshotService& snapshotService,
               core::app::RangeService& rangeService,
               std::size_t defaultLimit = 600);

    [[nodiscard]] RestResponse handleRequest(const RestRequest& request) const;
    [[nodiscard]] std::size_t defaultLimit() const noexcept { return defaultLimit_; }

private:
    core::app::SnapshotService& snapshotService_;
    core::app::RangeService& rangeService_;
    std::size_t defaultLimit_;
};

}  // namespace adapters::api::rest

