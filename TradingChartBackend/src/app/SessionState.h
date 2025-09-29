#pragma once

#include <string>

#include "domain/Types.h"

namespace app {

struct SessionState {
    std::string symbol;
    domain::Interval interval;

    bool operator==(const SessionState& other) const {
        return symbol == other.symbol && interval.ms == other.interval.ms;
    }

    bool operator!=(const SessionState& other) const {
        return !(*this == other);
    }
};

}  // namespace app
