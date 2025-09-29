#include "bootstrap/DuckBootstrap.hpp"

#include <exception>
#include <utility>

#include "core/app/DuckRepo.hpp"
#include "core/app/ICandleRepository.hpp"

namespace bootstrap {

std::unique_ptr<core::app::ICandleRepository> makeDuckRepo() {
    try {
        auto repo = std::make_unique<core::app::DuckRepo>("data/market.duckdb");
        return repo;
    }
    catch (const std::exception&) {
        return nullptr;
    }
}

}  // namespace bootstrap

