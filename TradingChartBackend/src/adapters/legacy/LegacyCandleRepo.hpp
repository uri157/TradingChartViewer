#pragma once

#include "domain/Ports.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace adapters::legacy {

class LegacyCandleRepo : public domain::contracts::ICandleReadRepo {
public:
    LegacyCandleRepo();
    explicit LegacyCandleRepo(std::vector<std::filesystem::path> searchPaths);

    std::vector<domain::contracts::Candle> getCandles(const domain::contracts::Symbol& symbol,
                                                      domain::contracts::Interval interval,
                                                      std::int64_t fromTs,
                                                      std::int64_t toTs,
                                                      std::size_t limit) const override;

    std::vector<domain::contracts::SymbolInfo> listSymbols() const override;

private:
    std::optional<std::filesystem::path> findDataset(const std::string& symbol,
                                                     domain::contracts::Interval interval) const;
    std::optional<std::filesystem::path> findDatasetFallback(const std::string& symbol) const;
    std::vector<domain::contracts::Candle> readDataset(const std::filesystem::path& path,
                                                       std::int64_t fromTs,
                                                       std::int64_t toTs,
                                                       std::size_t limit) const;

    std::vector<std::filesystem::path> searchPaths_;
};

}  // namespace adapters::legacy

