#pragma once

namespace infra::storage {
struct PriceData;
}

namespace core {

using PriceData = infra::storage::PriceData;

class ICacheObserver {
public:
    virtual void onCacheUpdated(const PriceData& priceData) = 0;
    virtual ~ICacheObserver() = default;
};

}  // namespace core
