#include "infra/storage/PriceDataManager.h"

#include "logging/Log.h"
#include "core/TimeUtils.h"

#include <filesystem>
#include <fstream>
#include <cstring>
#include <optional>
#include <algorithm>

namespace {

bool fileExists(const std::string& filename) {
    std::ifstream file(filename, std::ios::binary);
    return file.good();
}

infra::storage::PriceData normalizeRecord(const infra::storage::PriceData& record) {
    infra::storage::PriceData normalized = record;
    const auto alignedOpen = domain::floorToMinuteMs(record.openTime);
    if (alignedOpen <= 0) {
        normalized.openTime = 0;
        normalized.closeTime = 0;
        return normalized;
    }
    normalized.openTime = alignedOpen;
    normalized.closeTime = alignedOpen + core::TimeUtils::kMillisPerMinute - 1;
    return normalized;
}

} // namespace

namespace infra::storage {

PriceDataManager::PriceDataManager(const std::string& filename) : filename(filename) {
    namespace fs = std::filesystem;
    fs::path path(filename);
    std::error_code ec;
    if (!path.parent_path().empty()) {
        fs::create_directories(path.parent_path(), ec);
        if (ec) {
            LOG_WARN(logging::LogCategory::DATA, "Failed to create directory %s: %s", path.parent_path().string().c_str(), ec.message().c_str());
        }
    }
    if (!fileExists(filename)) {
        std::ofstream outFile(filename, std::ios::binary);
        if (!outFile) {
            LOG_ERROR(logging::LogCategory::DATA, "Failed to create data file: %s", filename.c_str());
        }
    }
}

infra::storage::PriceData PriceDataManager::readLastRecord() const {
    std::lock_guard<std::mutex> lock(fileMutex);
    std::ifstream inFile(filename, std::ios::binary);
    infra::storage::PriceData lastRecord{};

    if (!inFile) {
        LOG_WARN(logging::LogCategory::DATA, "Unable to open data file for reading: %s", filename.c_str());
        return lastRecord;
    }

    inFile.seekg(0, std::ios::end);
    std::streampos fileSize = inFile.tellg();

    if (fileSize >= static_cast<std::streampos>(sizeof(infra::storage::PriceData))) {
        inFile.seekg(-static_cast<std::streamoff>(sizeof(infra::storage::PriceData)), std::ios::end);
        inFile.read(reinterpret_cast<char*>(&lastRecord), sizeof(infra::storage::PriceData));
        lastRecord.symbol[sizeof(lastRecord.symbol) - 1] = '\0';
        lastRecord.interval[sizeof(lastRecord.interval) - 1] = '\0';
    }

    return lastRecord;
}

bool PriceDataManager::isValidRecord(const infra::storage::PriceData& record) const {
    return record.openTime > 0 && record.closeTime > 0 &&
           std::strlen(record.symbol) > 0 && std::strlen(record.interval) > 0;
}

void PriceDataManager::saveRecord(const infra::storage::PriceData& data) {
    if (!isValidRecord(data)) {
        return;
    }

    std::lock_guard<std::mutex> lock(fileMutex);
    std::ofstream outFile(filename, std::ios::binary | std::ios::app);
    if (!outFile) {
        LOG_ERROR(logging::LogCategory::DATA, "Unable to open data file for append: %s", filename.c_str());
        return;
    }
    infra::storage::PriceData normalized = normalizeRecord(data);
    if (!isValidRecord(normalized)) {
        return;
    }
    outFile.write(reinterpret_cast<const char*>(&normalized), sizeof(infra::storage::PriceData));
}

void PriceDataManager::saveRecords(const std::vector<infra::storage::PriceData>& records) {
    if (records.empty()) {
        return;
    }

    std::lock_guard<std::mutex> lock(fileMutex);

    std::fstream file(filename, std::ios::in | std::ios::out | std::ios::binary);
    if (!file) {
        file.open(filename, std::ios::out | std::ios::binary);
        file.close();
        file.open(filename, std::ios::in | std::ios::out | std::ios::binary);
    }
    if (!file) {
        LOG_ERROR(logging::LogCategory::DATA, "Unable to open data file for updates: %s", filename.c_str());
        return;
    }

    infra::storage::PriceData lastRecord{};
    {
        std::ifstream inFile(filename, std::ios::binary);
        if (inFile) {
            inFile.seekg(0, std::ios::end);
            std::streampos fileSize = inFile.tellg();
            if (fileSize >= static_cast<std::streampos>(sizeof(infra::storage::PriceData))) {
                inFile.seekg(-static_cast<std::streamoff>(sizeof(infra::storage::PriceData)), std::ios::end);
                inFile.read(reinterpret_cast<char*>(&lastRecord), sizeof(infra::storage::PriceData));
            }
        }
    }

    bool hasLast = isValidRecord(lastRecord);
    std::optional<infra::storage::PriceData> overwrite;
    std::vector<infra::storage::PriceData> toAppend;
    toAppend.reserve(records.size());
    infra::storage::PriceData lastPersisted = lastRecord;

    for (const auto& record : records) {
        infra::storage::PriceData normalized = normalizeRecord(record);
        if (!isValidRecord(normalized)) {
            continue;
        }

        if (hasLast && normalized.openTime == lastPersisted.openTime) {
            overwrite = normalized;
            lastPersisted = normalized;
        }
        else if (!hasLast || normalized.openTime > lastPersisted.openTime) {
            toAppend.push_back(normalized);
            lastPersisted = normalized;
            hasLast = true;
        }
    }

    if (overwrite.has_value()) {
        file.seekp(0, std::ios::end);
        if (file.tellp() >= static_cast<std::streampos>(sizeof(infra::storage::PriceData))) {
            file.seekp(-static_cast<std::streamoff>(sizeof(infra::storage::PriceData)), std::ios::end);
            file.write(reinterpret_cast<const char*>(&overwrite.value()), sizeof(infra::storage::PriceData));
        }
    }

    if (!toAppend.empty()) {
        file.seekp(0, std::ios::end);
        file.write(reinterpret_cast<const char*>(toAppend.data()), static_cast<std::streamsize>(toAppend.size() * sizeof(infra::storage::PriceData)));
    }
}

std::vector<infra::storage::PriceData> PriceDataManager::readAllRecords() const {
    std::lock_guard<std::mutex> lock(fileMutex);
    std::ifstream inFile(filename, std::ios::binary);
    std::vector<infra::storage::PriceData> records;

    if (!inFile) {
        LOG_WARN(logging::LogCategory::DATA, "Unable to open data file for reading: %s", filename.c_str());
        return records;
    }

    infra::storage::PriceData record{};
    while (inFile.read(reinterpret_cast<char*>(&record), sizeof(infra::storage::PriceData))) {
        records.push_back(record);
    }
    return records;
}

std::vector<infra::storage::PriceData> PriceDataManager::readLastNRecords(size_t n) const {
    std::lock_guard<std::mutex> lock(fileMutex);
    std::ifstream inFile(filename, std::ios::binary);
    std::vector<infra::storage::PriceData> records;

    if (!inFile) {
        LOG_WARN(logging::LogCategory::DATA, "Unable to open data file for reading: %s", filename.c_str());
        return records;
    }

    inFile.seekg(0, std::ios::end);
    std::streampos fileSize = inFile.tellg();
    size_t numRecords = static_cast<size_t>(fileSize / sizeof(infra::storage::PriceData));
    if (numRecords == 0) {
        return records;
    }

    size_t recordsToRead = std::min(n, numRecords);
    inFile.seekg(-static_cast<std::streamoff>(recordsToRead * sizeof(infra::storage::PriceData)), std::ios::end);

    for (size_t i = 0; i < recordsToRead; ++i) {
        infra::storage::PriceData record{};
        if (!inFile.read(reinterpret_cast<char*>(&record), sizeof(infra::storage::PriceData))) {
            break;
        }
        records.push_back(record);
    }

    return records;
}

std::optional<std::pair<long long, long long>> PriceDataManager::readOpenTimeRange() const {
    std::lock_guard<std::mutex> lock(fileMutex);
    std::ifstream inFile(filename, std::ios::binary);
    if (!inFile) {
        LOG_WARN(logging::LogCategory::DATA, "Unable to open data file for reading: %s", filename.c_str());
        return std::nullopt;
    }

    infra::storage::PriceData record{};
    long long minTimestamp = 0;
    bool foundFirst = false;

    while (inFile.read(reinterpret_cast<char*>(&record), sizeof(infra::storage::PriceData))) {
        if (isValidRecord(record)) {
            minTimestamp = record.openTime;
            foundFirst = true;
            break;
        }
    }

    if (!foundFirst) {
        return std::nullopt;
    }

    inFile.clear();
    inFile.seekg(0, std::ios::end);
    std::streampos fileSize = inFile.tellg();

    while (fileSize >= static_cast<std::streampos>(sizeof(infra::storage::PriceData))) {
        fileSize -= static_cast<std::streamoff>(sizeof(infra::storage::PriceData));
        inFile.seekg(fileSize);
        if (!inFile.read(reinterpret_cast<char*>(&record), sizeof(infra::storage::PriceData))) {
            continue;
        }
        if (isValidRecord(record)) {
            return std::make_pair(minTimestamp, record.openTime);
        }
    }

    return std::nullopt;
}

}  // namespace infra::storage
