#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include "api/Controllers.hpp"

namespace ttp::http {

std::optional<std::string> opt_string(const ttp::api::Request& request, const char* key);

std::optional<int> opt_int(const ttp::api::Request& request, const char* key);

std::optional<std::int64_t> opt_int64(const ttp::api::Request& request, const char* key);

}  // namespace ttp::http

