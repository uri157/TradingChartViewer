#include "adapters/legacy/LegacyCandleRepo.hpp"

#include <algorithm>
#include <deque>
#include <fstream>
#include <limits>
#include <unordered_set>

#include "domain/Models.hpp"
#include "infra/storage/PriceData.h"
#include "logging/Log.h"

namespace adapters::legacy {

namespace {
constexpr logging::LogCategory kLogCategory = logging::LogCategory::DATA;

std::vector<std::filesystem::path> normalizeSearchPaths(std::vector<std::filesystem::path> paths) {
    std::vector<std::filesystem::path> normalized;
    normalized.reserve(paths.size() + 1);

    std::unordered_set<std::string> seen;
    for (auto& path : paths) {
        auto str = path.string();
        if (seen.insert(str).second) {
            normalized.push_back(std::move(path));
        }
    }

    if (normalized.empty()) {
        normalized.emplace_back(std::filesystem::path{"."});
    }
    else {
        const auto current = std::filesystem::path{"."};
        if (seen.insert(current.string()).second) {
            normalized.push_back(current);
        }
    }

    return normalized;
}

std::size_t effectiveLimit(std::size_t limit) {
    if (limit == 0) {
        return std::numeric_limits<std::size_t>::max();
    }
    return limit;
}

bool matchesRange(std::int64_t value, std::int64_t fromTs, std::int64_t toTs) {
    if (fromTs > 0 && value < fromTs) {
        return false;
    }
    if (toTs > 0 && value > toTs) {
        return false;
    }
    return true;
}

}  // namespace

LegacyCandleRepo::LegacyCandleRepo()
    : LegacyCandleRepo({std::filesystem::path{"./cache"},
                        std::filesystem::path{"./data"}}) {}

LegacyCandleRepo::LegacyCandleRepo(std::vector<std::filesystem::path> searchPaths)
    : searchPaths_(normalizeSearchPaths(std::move(searchPaths))) {}

std::vector<domain::contracts::Candle> LegacyCandleRepo::getCandles(
    const domain::contracts::Symbol& symbol,
    domain::contracts::Interval interval,
    std::int64_t fromTs,
    std::int64_t toTs,
    std::size_t limit) const {
    const auto label = domain::contracts::intervalToString(interval);
    LOG_INFO(kLogCategory,
             "LegacyCandleRepo request symbol=%s interval=%s from=%lld to=%lld limit=%zu",
             symbol.c_str(),
             label.c_str(),
             static_cast<long long>(fromTs),
             static_cast<long long>(toTs),
             static_cast<std::size_t>(limit));

    auto datasetPath = findDataset(symbol, interval);
    if (!datasetPath.has_value()) {
        datasetPath = findDatasetFallback(symbol);
    }

    if (!datasetPath.has_value()) {
        LOG_WARN(kLogCategory,
                 "LegacyCandleRepo dataset not found symbol=%s interval=%s",
                 symbol.c_str(),
                 label.c_str());
        return {};
    }

    auto candles = readDataset(datasetPath.value(), fromTs, toTs, limit);
    LOG_INFO(kLogCategory,
             "LegacyCandleRepo result symbol=%s interval=%s path=%s count=%zu",
             symbol.c_str(),
             label.c_str(),
             datasetPath->string().c_str(),
             static_cast<std::size_t>(candles.size()));
    return candles;
}

std::vector<domain::contracts::SymbolInfo> LegacyCandleRepo::listSymbols() const {
    return {};
}

std::optional<std::filesystem::path> LegacyCandleRepo::findDataset(
    const std::string& symbol,
    domain::contracts::Interval interval) const {
    const auto label = domain::contracts::intervalToString(interval);
    if (label.empty()) {
        return std::nullopt;
    }

    const auto fileName = symbol + '_' + label + ".bin";
    for (const auto& base : searchPaths_) {
        std::filesystem::path candidate = base;
        candidate /= fileName;

        std::error_code ec;
        if (std::filesystem::exists(candidate, ec) &&
            std::filesystem::is_regular_file(candidate, ec)) {
            return candidate;
        }
    }

    return std::nullopt;
}

std::optional<std::filesystem::path> LegacyCandleRepo::findDatasetFallback(
    const std::string& symbol) const {
    const std::string prefix = symbol + '_';
    for (const auto& base : searchPaths_) {
        std::error_code dirEc;
        if (!std::filesystem::exists(base, dirEc) || !std::filesystem::is_directory(base, dirEc)) {
            continue;
        }

        std::error_code iterEc;
        for (const auto& entry : std::filesystem::directory_iterator(base, iterEc)) {
            if (iterEc) {
                LOG_WARN(kLogCategory,
                         "LegacyCandleRepo directory iteration error path=%s error=%s",
                         base.string().c_str(),
                         iterEc.message().c_str());
                break;
            }

            if (!entry.is_regular_file()) {
                continue;
            }

            const auto filename = entry.path().filename().string();
            if (filename.size() <= prefix.size()) {
                continue;
            }

            if (filename.rfind(prefix, 0) == 0 && entry.path().extension() == ".bin") {
                return entry.path();
            }
        }
    }

    return std::nullopt;
}

std::vector<domain::contracts::Candle> LegacyCandleRepo::readDataset(
    const std::filesystem::path& path,
    std::int64_t fromTs,
    std::int64_t toTs,
    std::size_t limit) const {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        LOG_WARN(kLogCategory,
                 "LegacyCandleRepo failed to open dataset path=%s",
                 path.string().c_str());
        return {};
    }

    const bool hasRange = (fromTs > 0) || (toTs > 0);
    const bool limitEnabled = (limit > 0);
    const std::size_t maxItems = effectiveLimit(limit);

    std::vector<domain::contracts::Candle> candles;
    std::deque<domain::contracts::Candle> window;
    if (!hasRange && limitEnabled) {
        window.clear();
    }
    else {
        candles.reserve(std::min<std::size_t>(maxItems, 1024));
    }

    infra::storage::PriceData record{};
    while (input.read(reinterpret_cast<char*>(&record), sizeof(record))) {
        const auto ts = static_cast<std::int64_t>(record.openTime);
        if (fromTs > 0 && ts < fromTs) {
            continue;
        }
        if (toTs > 0 && ts > toTs) {
            break;
        }
        if (!matchesRange(ts, fromTs, toTs)) {
            continue;
        }

        domain::contracts::Candle candle{};
        candle.ts = ts;
        candle.o = record.openPrice;
        candle.h = record.highPrice;
        candle.l = record.lowPrice;
        candle.c = record.closePrice;
        candle.v = record.volume;

        if (!hasRange && limitEnabled) {
            window.push_back(candle);
            if (window.size() > maxItems) {
                window.pop_front();
            }
            continue;
        }

        candles.push_back(candle);

        if (limitEnabled && candles.size() >= maxItems) {
            break;
        }
    }

    if (!hasRange && limitEnabled) {
        candles.assign(window.begin(), window.end());
    }

    std::sort(candles.begin(), candles.end(), [](const auto& lhs, const auto& rhs) {
        return lhs.ts < rhs.ts;
    });

    return candles;
}

}  // namespace adapters::legacy

