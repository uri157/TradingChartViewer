#pragma once

#include <boost/json/value.hpp>

#include "api/Controllers.hpp"

namespace ttp::http {

// Serializa un valor JSON a la respuesta y establece los metadatos básicos.
void write_json(ttp::api::Response& response, const boost::json::value& value);

}  // namespace ttp::http

