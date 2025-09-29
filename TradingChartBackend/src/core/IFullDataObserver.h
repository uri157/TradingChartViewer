#pragma once

namespace infra::storage {
struct PriceData;
}

namespace core {

using PriceData = infra::storage::PriceData;

class IFullDataObserver {
public:
    virtual void onFullDataUpdated(const PriceData& priceData) = 0;
    virtual ~IFullDataObserver() = default;
};

}  // namespace core

