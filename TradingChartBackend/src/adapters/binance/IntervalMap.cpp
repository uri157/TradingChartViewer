#include "adapters/binance/IntervalMap.hpp"

namespace adapters::binance {

std::string binance_interval(domain::Interval interval) {
    return std::string(detail::binance_interval_literal(interval));
}

domain::Interval from_binance_interval(const std::string &value) {
    return detail::from_binance_interval_literal(value);
}

} // namespace adapters::binance

