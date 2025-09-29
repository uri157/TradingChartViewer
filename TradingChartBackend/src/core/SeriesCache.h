#pragma once

#include <atomic>
#include <cstdint>
#include <memory>

#include "domain/DomainContracts.h"

namespace core {

class SeriesCache {
public:
    SeriesCache();
    explicit SeriesCache(std::shared_ptr<const domain::CandleSeries> initial);

    void update(std::shared_ptr<const domain::CandleSeries> series);
    std::shared_ptr<const domain::CandleSeries> snapshot() const;
    std::uint64_t version() const;

private:
    static std::shared_ptr<const domain::CandleSeries> ensureValid(
        std::shared_ptr<const domain::CandleSeries> series);

    mutable std::shared_ptr<const domain::CandleSeries> ptr_;
    std::atomic<std::uint64_t> ver_{0};
};

}  // namespace core

