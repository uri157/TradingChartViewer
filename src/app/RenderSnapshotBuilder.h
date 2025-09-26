#pragma once

#include "core/RenderSnapshot.h"
#include "core/Viewport.h"
#include "domain/DomainContracts.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace indicators {
class IndicatorCoordinator;
}

namespace app {

class RenderSnapshotBuilder {
public:
    struct CursorState {
        bool active{false};
        float x{0.0f};
        float y{0.0f};
    };

    struct RepoView {
        std::size_t candleCount{0};
        bool hasGap{false};
        std::int64_t intervalMs{0};
        std::int64_t lastClosedOpenTime{0};
        std::string seriesKey;
    };

    struct ConnectivityView {
        bool wsConnected{false};
        bool backfilling{false};
        std::int64_t lastTickMs{0};
    };

    struct StateInputs {
        RepoView repo;
        ConnectivityView net;
    };

    struct ViewportParams {
        std::size_t firstIndex{0};
        std::size_t visibleCount{120};
        unsigned canvasWidth{0};
        unsigned canvasHeight{0};
        bool snapToLatest{true};
        float minVisibleCandles{20.0f};
        float maxVisibleCandles{500.0f};
    };

    explicit RenderSnapshotBuilder(float candleWidthRatio = 0.75f, float minBodyHeight = 1.0f);

    void setIndicatorCoordinator(std::shared_ptr<indicators::IndicatorCoordinator> coordinator);

    core::RenderSnapshot build(const std::vector<core::RenderCandleData>& series,
                               const ViewportParams& viewport,
                               const std::optional<CursorState>& cursor = std::nullopt,
                               const std::optional<StateInputs>& stateInputs = std::nullopt) const;

    core::RenderSnapshot build(const domain::CandleSeries& series,
                               const core::Viewport& view,
                               unsigned canvasWidth,
                               unsigned canvasHeight,
                               const std::optional<CursorState>& cursor = std::nullopt,
                               const std::optional<StateInputs>& stateInputs = std::nullopt) const;

private:
    float candleWidthRatio_;
    float minBodyHeight_;
    std::shared_ptr<indicators::IndicatorCoordinator> indicatorCoordinator_;

    core::UiState computeUiState(const RepoView& repo,
                                 const ConnectivityView& net,
                                 std::string& outMsg) const;
    void applyUiState(core::RenderSnapshot& snapshot,
                      const std::optional<StateInputs>& stateInputs) const;
    void mergeIndicators(core::RenderSnapshot& snapshot,
                         const domain::CandleSeries& series,
                         const std::optional<StateInputs>& stateInputs) const;

    static long long estimateIntervalMs(const std::vector<core::RenderCandleData>& series,
                                        std::size_t startIndex,
                                        std::size_t count);
};

}  // namespace app
