#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace ttp::common::metrics {

class Registry {
private:
    class ScopedTimerImpl;

public:
    struct RouteSnapshot {
        std::uint64_t totalRequests{0};
        std::optional<double> p95Ms{};
        std::optional<double> p99Ms{};
    };

    struct CounterSnapshot {
        std::uint64_t value{0};
    };

    struct GaugeSnapshot {
        double value{0.0};
        std::chrono::steady_clock::time_point updatedAt{};
        std::optional<std::chrono::steady_clock::time_point> zeroSince{};
    };

    struct Snapshot {
        std::chrono::steady_clock::time_point startTime;
        std::chrono::steady_clock::time_point capturedAt;
        std::unordered_map<std::string, RouteSnapshot> routes;
        std::unordered_map<std::string, CounterSnapshot> counters;
        std::unordered_map<std::string, GaugeSnapshot> gauges;
    };

    class ScopedTimer {
    public:
        explicit ScopedTimer(std::string routeKey);
        ~ScopedTimer();

        ScopedTimer(const ScopedTimer&) = delete;
        ScopedTimer& operator=(const ScopedTimer&) = delete;
        ScopedTimer(ScopedTimer&&) = delete;
        ScopedTimer& operator=(ScopedTimer&&) = delete;

    private:
        std::unique_ptr<ScopedTimerImpl> impl_;
    };

    static Registry& instance();

    void incrementRequest(const std::string& routeKey);
    void incrementCounter(const std::string& counterKey,
                          std::uint64_t value = 1U);
    void setGauge(const std::string& gaugeKey, double value);
    Snapshot snapshot() const;

private:
    struct RouteMetrics {
        std::atomic<std::uint64_t> totalRequests{0};
        mutable std::mutex latenciesMutex;
        std::vector<double> latenciesMs;

        void addLatency(double latencyMs);
        std::vector<double> copyLatencies() const;
    };

    struct CounterMetrics {
        std::uint64_t value{0};
    };

    struct GaugeMetrics {
        double value{0.0};
        std::chrono::steady_clock::time_point updatedAt{};
        std::optional<std::chrono::steady_clock::time_point> zeroSince{};
    };

    class ScopedTimerImpl {
    public:
        ScopedTimerImpl(Registry& registry, std::string routeKey);
        ~ScopedTimerImpl();

    private:
        RouteMetrics* metrics_{nullptr};
        std::chrono::steady_clock::time_point start_;
    };

    Registry();

    RouteMetrics& ensureRouteMetrics(const std::string& routeKey);

    const std::chrono::steady_clock::time_point startTime_;
    mutable std::mutex mutex_;
    std::unordered_map<std::string, std::unique_ptr<RouteMetrics>> routeMetrics_;
    std::unordered_map<std::string, CounterMetrics> counters_;
    std::unordered_map<std::string, GaugeMetrics> gauges_;
};

}  // namespace ttp::common::metrics
