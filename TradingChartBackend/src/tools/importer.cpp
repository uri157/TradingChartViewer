#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <string>
#include <utility>

#include "adapters/persistence/ICandleRepository.hpp"
#include "adapters/persistence/duckdb/DuckRepo.hpp"
#include "infra/storage/PriceData.h"

namespace {

using adapters::persistence::CandleRow;
namespace fs = std::filesystem;

constexpr std::size_t kBatchSize = 1000;

std::string trimNullTerminated(const char* buffer, std::size_t size) {
    const char* end = std::find(buffer, buffer + size, '\0');
    return std::string(buffer, static_cast<std::size_t>(end - buffer));
}

bool isValidRecord(const infra::storage::PriceData& record) {
    return record.openTime > 0 && record.closeTime > 0 && record.symbol[0] != '\0' &&
           record.interval[0] != '\0';
}

class BatchInserter {
public:
    explicit BatchInserter(adapters::persistence::duckdb_adapter::DuckRepo& repo,
                           std::size_t batchSize)
        : repo_{repo}, batchSize_{batchSize} {}

    bool insert(const CandleRow& row) {
        try {
            ensureTransaction_();
            repo_.upsert(row);
            ++currentBatchSize_;
            if (currentBatchSize_ >= batchSize_) {
                commit_();
            }
            return true;
        }
        catch (const std::exception& ex) {
            std::cerr << "DuckRepo insert failed: " << ex.what() << std::endl;
            return false;
        }
    }

    bool finalize() {
        if (!inTransaction_) {
            return true;
        }

        try {
            commit_();
            return true;
        }
        catch (const std::exception& ex) {
            std::cerr << "DuckRepo commit failed: " << ex.what() << std::endl;
            return false;
        }
    }

private:
    void ensureTransaction_() {
        if (inTransaction_) {
            return;
        }
        repo_.beginTransaction();
        inTransaction_ = true;
    }

    void commit_() {
        if (!inTransaction_) {
            return;
        }
        repo_.commitTransaction();
        inTransaction_ = false;
        currentBatchSize_ = 0;
    }

    adapters::persistence::duckdb_adapter::DuckRepo& repo_;
    std::size_t batchSize_{kBatchSize};
    std::size_t currentBatchSize_{0};
    bool inTransaction_{false};
};

struct DatasetStats {
    std::size_t total{0};
    std::size_t inserted{0};
};

CandleRow toCandleRow(const infra::storage::PriceData& record) {
    CandleRow row{};
    row.symbol = trimNullTerminated(record.symbol, sizeof(record.symbol));
    row.interval = trimNullTerminated(record.interval, sizeof(record.interval));
    row.openMs = static_cast<std::int64_t>(record.openTime);
    row.open = record.openPrice;
    row.high = record.highPrice;
    row.low = record.lowPrice;
    row.close = record.closePrice;
    row.volume = record.volume;
    return row;
}

}  // namespace

int main(int argc, char* argv[]) {
    std::string dataDir = "data";
    std::string dbPath = "data/market.duckdb";

    if (argc > 1) {
        dataDir = argv[1];
    }
    if (argc > 2) {
        dbPath = argv[2];
    }

    try {
        adapters::persistence::duckdb_adapter::DuckRepo repo(dbPath);
        repo.init();

        BatchInserter inserter(repo, kBatchSize);
        std::map<std::pair<std::string, std::string>, DatasetStats> stats;

        std::error_code ec;
        if (!fs::exists(dataDir, ec) || !fs::is_directory(dataDir, ec)) {
            std::cerr << "Data directory not found: " << dataDir << std::endl;
            return 1;
        }

        for (const auto& entry : fs::directory_iterator(dataDir, ec)) {
            if (ec) {
                std::cerr << "Error reading directory " << dataDir << ": " << ec.message() << std::endl;
                return 1;
            }

            if (!entry.is_regular_file()) {
                continue;
            }

            if (entry.path().extension() != ".bin") {
                continue;
            }

            std::ifstream input(entry.path(), std::ios::binary);
            if (!input) {
                std::cerr << "Failed to open file: " << entry.path() << std::endl;
                return 1;
            }

            infra::storage::PriceData record{};
            while (input.read(reinterpret_cast<char*>(&record), sizeof(record))) {
                if (!isValidRecord(record)) {
                    continue;
                }

                CandleRow row = toCandleRow(record);
                auto key = std::make_pair(row.symbol, row.interval);
                auto& stat = stats[key];
                ++stat.total;

                if (!inserter.insert(row)) {
                    std::cerr << "Failed to insert candle for " << row.symbol << " " << row.interval
                              << " at " << row.openMs << std::endl;
                    return 1;
                }

                ++stat.inserted;
            }

            if (!input.eof()) {
                std::cerr << "Failed to read file (partial record): " << entry.path() << std::endl;
                return 1;
            }
        }

        if (!inserter.finalize()) {
            return 1;
        }

        if (stats.empty()) {
            std::cout << "No records imported from " << dataDir << std::endl;
            return 0;
        }

        std::cout << "Import summary:" << std::endl;
        for (const auto& [key, stat] : stats) {
            std::cout << "  " << key.first << " " << key.second << ": " << stat.inserted << "/" << stat.total
                      << " records" << std::endl;
        }

        return 0;
    }
    catch (const std::exception& ex) {
        std::cerr << "Fatal error: " << ex.what() << std::endl;
    }
    catch (...) {
        std::cerr << "Fatal error: unknown exception" << std::endl;
    }

    return 1;
}

