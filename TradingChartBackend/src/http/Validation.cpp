#include "http/Validation.hpp"

#include <algorithm>
#include <array>
#include <string_view>

namespace {

constexpr std::array<std::string_view, 4> kSupportedIntervals{"1m", "5m", "1h", "1d"};

}  // namespace

namespace ttp::http::validation {

bool is_valid_interval(std::string_view interval) {
    return std::find(kSupportedIntervals.begin(), kSupportedIntervals.end(), interval) !=
           kSupportedIntervals.end();
}

}  // namespace ttp::http::validation

