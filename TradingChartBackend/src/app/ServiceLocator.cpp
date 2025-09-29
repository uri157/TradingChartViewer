#include "app/ServiceLocator.hpp"

#include <filesystem>
#include <utility>
#include <vector>

#include "adapters/legacy/LegacyCandleRepo.hpp"
#include "config/Config.h"

namespace app {

ServiceLocator& ServiceLocator::instance() {
    static ServiceLocator locator;
    return locator;
}

ServiceLocator::ServiceLocator()
    : candleReadRepo_(std::make_shared<adapters::legacy::LegacyCandleRepo>()) {}

void ServiceLocator::init_backends(const config::Config& config) {
    std::vector<std::filesystem::path> searchPaths;
    if (!config.cacheDir.empty()) {
        searchPaths.emplace_back(config.cacheDir);
    }
    if (!config.dataDir.empty()) {
        searchPaths.emplace_back(config.dataDir);
    }

    auto repo = std::make_shared<adapters::legacy::LegacyCandleRepo>(std::move(searchPaths));
    ServiceLocator::instance().setCandleReadRepo(std::move(repo));
}

void ServiceLocator::setCandleReadRepo(
    std::shared_ptr<const domain::contracts::ICandleReadRepo> repo) {
    candleReadRepo_ = std::move(repo);
}

std::shared_ptr<const domain::contracts::ICandleReadRepo>
ServiceLocator::candleReadRepoHandle() const {
    return candleReadRepo_;
}

const domain::contracts::ICandleReadRepo* ServiceLocator::candleReadRepo() const {
    return candleReadRepo_.get();
}

}  // namespace app

