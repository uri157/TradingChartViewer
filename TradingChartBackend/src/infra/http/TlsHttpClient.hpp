#pragma once

#include <string>

namespace infra::http {

struct JsonResponse {
    unsigned status = 0U;
    std::string body;
    std::string used_weight_header;
    std::string final_host;
    std::string final_target;
};

JsonResponse https_get_json_response(const std::string& host, const std::string& target, int timeout_sec = 20);

// Performs an HTTPS GET request expecting a JSON payload.
// Throws std::runtime_error on network errors or HTTP status >= 400.
std::string https_get_json(const std::string& host, const std::string& target, int timeout_sec = 20);

}  // namespace infra::http
