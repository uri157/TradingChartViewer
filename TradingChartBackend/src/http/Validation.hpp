#pragma once

#include <string_view>

namespace ttp::http::validation {

bool is_valid_interval(std::string_view interval);

}  // namespace ttp::http::validation

