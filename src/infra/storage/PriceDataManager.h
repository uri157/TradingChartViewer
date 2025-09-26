#pragma once

#include "infra/storage/PriceData.h"

#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace infra::storage {

class PriceDataManager {
public:
    explicit PriceDataManager(const std::string& filename);

    infra::storage::PriceData readLastRecord() const;

    bool isValidRecord(const infra::storage::PriceData& record) const;

    void saveRecord(const infra::storage::PriceData& data);

    void saveRecords(const std::vector<infra::storage::PriceData>& records);

    std::vector<infra::storage::PriceData> readAllRecords() const;

    std::vector<infra::storage::PriceData> readLastNRecords(size_t n) const;

    std::optional<std::pair<long long, long long>> readOpenTimeRange() const;

private:
    std::string filename;
    mutable std::mutex fileMutex;
};

}  // namespace infra::storage
