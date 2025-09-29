#pragma once

namespace core {

class IPriceLimitObserver {
public:
    virtual ~IPriceLimitObserver() = default;

    virtual void onMaxPriceLimitChanged(double newMax) = 0;
    virtual void onMinPriceLimitChanged(double newMin) = 0;
};

}  // namespace core
