#pragma once

#include <string_view>

namespace ttp::http::errors {

inline constexpr std::string_view symbol_required = "symbol_required";
inline constexpr std::string_view interval_invalid = "interval_invalid";
inline constexpr std::string_view time_range_invalid = "time_range_invalid";
inline constexpr std::string_view limit_invalid = "limit_invalid";
inline constexpr std::string_view symbol_not_found = "symbol_not_found";
inline constexpr std::string_view internal_error = "internal_error";

}  // namespace ttp::http::errors

