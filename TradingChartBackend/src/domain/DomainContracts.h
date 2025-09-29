#pragma once

#include "domain/Types.h"

namespace domain {

enum class RangeState {
    Ok,
    Replaced,
    Gap,
    Overlap
};

struct AppendResult {
    RangeState state = RangeState::Ok;
    TimestampMs expected_from{};
    TimestampMs expected_to{};
    std::size_t appended = 0;
    bool touchedDisk = false;
    bool liveOnly = false;
};

struct RepoMetadata {
    TimestampMs minOpen{0};
    TimestampMs maxOpen{0};
    std::size_t count{0};
};

template <typename T>
struct Result {
    T value{};
    bool ok{true};
    std::string error{};

    bool failed() const { return !ok; }
};

class TimeSeriesRepository {
public:
    virtual ~TimeSeriesRepository() = default;

    virtual Result<CandleSeries> getLatest(std::size_t count) const = 0;
    virtual Result<CandleSeries> getRange(TimeRange range) const = 0;
    virtual AppendResult appendOrReplace(const Candle& candle) = 0;
    virtual AppendResult appendBatch(const std::vector<Candle>& batch) = 0;
    virtual RepoMetadata metadata() const = 0;
    virtual TimestampMs earliestOpenTime() const = 0;
    virtual TimestampMs latestOpenTime() const = 0;
    virtual std::size_t candleCount() const = 0;
    virtual bool hasGap() const = 0;
    virtual TimestampMs intervalMs() const = 0;
    virtual TimestampMs lastClosedOpenTime() const = 0;
};

class NullTimeSeriesRepository final : public TimeSeriesRepository {
public:
    Result<CandleSeries> getLatest(std::size_t /*count*/) const override { return {}; }
    Result<CandleSeries> getRange(TimeRange /*range*/) const override { return {}; }
    AppendResult appendOrReplace(const Candle& /*candle*/) override { return {}; }
    AppendResult appendBatch(const std::vector<Candle>& /*batch*/) override { return {}; }
    RepoMetadata metadata() const override { return {}; }
    TimestampMs earliestOpenTime() const override { return 0; }
    TimestampMs latestOpenTime() const override { return 0; }
    std::size_t candleCount() const override { return 0; }
    bool hasGap() const override { return false; }
    TimestampMs intervalMs() const override { return 0; }
    TimestampMs lastClosedOpenTime() const override { return 0; }
};

}  // namespace domain

