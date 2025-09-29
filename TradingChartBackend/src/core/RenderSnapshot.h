#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "indicators/IndicatorTypes.h"

namespace core {

enum class UiState {
    NoData,
    Loading,
    Live,
    Desync
};

enum class UiDataState {
    Loading,
    LiveOnly,
    Ready
};

struct SnapshotUiMeta {
    UiDataState state{UiDataState::Loading};
    std::string symbol;
    std::string interval;
    float progress{-1.0f};
};

struct RenderCandleData {
    long long openTimeMs{0};
    double open{0.0};
    double high{0.0};
    double low{0.0};
    double close{0.0};
};

struct RenderSnapshot {
    RenderSnapshot();
    RenderSnapshot(const RenderSnapshot& other);
    RenderSnapshot& operator=(const RenderSnapshot& other);
    RenderSnapshot(RenderSnapshot&& other) noexcept;
    RenderSnapshot& operator=(RenderSnapshot&& other) noexcept;
    ~RenderSnapshot() = default;

    struct AxisLine {
        float x1{0.0f};
        float y1{0.0f};
        float x2{0.0f};
        float y2{0.0f};
    };

    struct AxisTick {
        float x1{0.0f};
        float y1{0.0f};
        float x2{0.0f};
        float y2{0.0f};
    };

    struct LogicalRange {
        long long fromMs{0};
        long long toMs{0};
    };

    struct CandleRect {
        float centerX{0.0f};
        float top{0.0f};
        float height{0.0f};
        float halfWidth{0.0f};
        bool bullish{false};
    };

    struct CandleWick {
        float x{0.0f};
        float top{0.0f};
        float bottom{0.0f};
    };

    struct TimeLabel {
        float x{0.0f};
        std::string text;
    };

    struct PriceLabel {
        float y{0.0f};
        std::string text;
    };

    struct Crosshair {
        float x{0.0f};
        float y{0.0f};
        double price{0.0};
        long long timeMs{0};
        std::string labelOHLC;
        std::string priceText;
        std::string timeText;
    };

    LogicalRange logicalRange{};
    long long intervalMs{0};
    unsigned canvasWidth{0};
    unsigned canvasHeight{0};
    std::size_t firstVisibleIndex{0};
    std::size_t visibleCount{0};
    double visiblePriceMin{0.0};
    double visiblePriceMax{0.0};
    float pxPerCandle{0.0f};
    float pxPerPrice{0.0f};
    bool valid{false};
    bool snappedToLatest{false};
    UiState state{UiState::NoData};
    std::string stateMessage;
    SnapshotUiMeta ui{};
    std::vector<AxisLine> axes;
    std::vector<AxisTick> timeTicks;
    std::vector<AxisTick> priceTicks;
    std::vector<CandleRect> candles;
    std::vector<CandleWick> wicks;
    std::vector<TimeLabel> timeLabels;
    std::vector<PriceLabel> priceLabels;
    std::optional<Crosshair> crosshair;
    std::unordered_map<std::string, indicators::IndicatorSeries> indicators;
    std::atomic<std::uint64_t> version_{0};
    std::uint64_t lastPublishedVersion_{0};

private:
    void copyFrom(const RenderSnapshot& other);
    void moveFrom(RenderSnapshot&& other) noexcept;
};

}  // namespace core
