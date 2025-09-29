#pragma once

#include <string_view>

#include "api/Controllers.hpp"

namespace ttp::http {

// Serializa un error JSON con el formato {"error":"..."} y ajusta la respuesta HTTP.
void json_error(ttp::api::Response& response, int statusCode, std::string_view errorCode);

}  // namespace ttp::http

