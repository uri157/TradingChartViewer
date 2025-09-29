#include "domain/exchange/IExchangeKlines.hpp"

#include <algorithm>
#include <cctype>
#include <iterator>
#include <stdexcept>

namespace domain {

std::string to_string(Interval i) {
  return interval_label(i);
}

Interval interval_from_string(const std::string& value) {
  Interval parsed = interval_from_label(value);
  if (!parsed.valid()) {
    throw std::invalid_argument("Unsupported interval string: " + value);
  }
  return parsed;
}

}  // namespace domain
