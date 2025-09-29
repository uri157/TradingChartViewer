#pragma once

#include <memory>

namespace config {
struct Config;
}  // namespace config

namespace domain::contracts {
class ICandleReadRepo;
}  // namespace domain::contracts

namespace app {

class ServiceLocator {
public:
    static ServiceLocator& instance();

    static void init_backends(const config::Config& config);

    void setCandleReadRepo(std::shared_ptr<const domain::contracts::ICandleReadRepo> repo);
    std::shared_ptr<const domain::contracts::ICandleReadRepo> candleReadRepoHandle() const;
    const domain::contracts::ICandleReadRepo* candleReadRepo() const;

private:
    ServiceLocator();

    std::shared_ptr<const domain::contracts::ICandleReadRepo> candleReadRepo_;
};

}  // namespace app

