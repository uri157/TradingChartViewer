#include "adapters/api/rest/RestServer.hpp"

#include "core/app/RangeService.hpp"
#include "core/app/SnapshotService.hpp"
#include "logging/Log.h"

namespace adapters::api::rest {

RestServer::RestServer(core::app::SnapshotService& snapshotService,
                       core::app::RangeService& rangeService,
                       std::size_t defaultLimit)
    : snapshotService_{snapshotService}, rangeService_{rangeService}, defaultLimit_{defaultLimit} {}

RestResponse RestServer::handleRequest(const RestRequest& request) const {
    (void)request;
    LOG_WARN(::logging::LogCategory::NET,
             "RestServer stub received request. TODO: restore HTTP implementation when Boost is available.");
    RestResponse response{};
    response.status = 503;
    response.body = "{\"error\":\"REST API disabled\"}";
    return response;
}

}  // namespace adapters::api::rest

