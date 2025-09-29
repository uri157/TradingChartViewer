#include "common/Metrics.hpp"

#include <algorithm>
#include <cmath>
#include <memory>
#include <utility>

namespace ttp::common::metrics {
namespace {

double computeQuantile(const std::vector<double>& sortedValues, double quantile) {
    if (sortedValues.empty()) {
        return 0.0;
    }
    if (sortedValues.size() == 1U) {
        return sortedValues.front();
    }

    const double clampedQuantile = std::clamp(quantile, 0.0, 1.0);
    const double position = clampedQuantile * static_cast<double>(sortedValues.size() - 1U);
    const auto lowerIndex = static_cast<std::size_t>(std::floor(position));
    const auto upperIndex = static_cast<std::size_t>(std::ceil(position));

    if (lowerIndex == upperIndex) {
        return sortedValues[lowerIndex];
    }

    const double weight = position - static_cast<double>(lowerIndex);
    return sortedValues[lowerIndex]
        + weight * (sortedValues[upperIndex] - sortedValues[lowerIndex]);
}

}  // namespace

Registry::Registry()
    : startTime_(std::chrono::steady_clock::now()) {}

Registry& Registry::instance() {
    static Registry instance;
    return instance;
}

Registry::ScopedTimer::ScopedTimer(std::string routeKey)
    : impl_(std::make_unique<ScopedTimerImpl>(Registry::instance(), std::move(routeKey))) {}

Registry::ScopedTimer::~ScopedTimer() = default;

void Registry::incrementRequest(const std::string& routeKey) {
    auto& metrics = ensureRouteMetrics(routeKey);
    metrics.totalRequests.fetch_add(1U, std::memory_order_relaxed);
}

void Registry::incrementCounter(const std::string& counterKey, std::uint64_t value) {
    if (value == 0U) {
        return;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    counters_[counterKey].value += value;
}

void Registry::setGauge(const std::string& gaugeKey, double value) {
    const auto now = std::chrono::steady_clock::now();

    std::lock_guard<std::mutex> lock(mutex_);
    auto& gauge = gauges_[gaugeKey];
    gauge.value = value;
    gauge.updatedAt = now;
    if (value == 0.0) {
        if (!gauge.zeroSince.has_value()) {
            gauge.zeroSince = now;
        }
    }
    else {
        gauge.zeroSince.reset();
    }
}

Registry::Snapshot Registry::snapshot() const {
    Snapshot snapshot;
    snapshot.startTime = startTime_;
    snapshot.capturedAt = std::chrono::steady_clock::now();

    std::lock_guard<std::mutex> lock(mutex_);
    snapshot.routes.reserve(routeMetrics_.size());
    for (const auto& [routeKey, metricsPtr] : routeMetrics_) {
        RouteSnapshot routeSnapshot;
        routeSnapshot.totalRequests
            = metricsPtr->totalRequests.load(std::memory_order_relaxed);

        auto latencies = metricsPtr->copyLatencies();
        if (!latencies.empty()) {
            std::sort(latencies.begin(), latencies.end());
            routeSnapshot.p95Ms = computeQuantile(latencies, 0.95);
            routeSnapshot.p99Ms = computeQuantile(latencies, 0.99);
        }

        snapshot.routes.emplace(routeKey, std::move(routeSnapshot));
    }

    snapshot.counters.reserve(counters_.size());
    for (const auto& [key, counter] : counters_) {
        snapshot.counters.emplace(key, CounterSnapshot{counter.value});
    }

    snapshot.gauges.reserve(gauges_.size());
    for (const auto& [key, gauge] : gauges_) {
        snapshot.gauges.emplace(
            key,
            GaugeSnapshot{gauge.value, gauge.updatedAt, gauge.zeroSince});
    }

    return snapshot;
}

void Registry::RouteMetrics::addLatency(double latencyMs) {
    std::lock_guard<std::mutex> lock(latenciesMutex);
    latenciesMs.push_back(latencyMs);
}

std::vector<double> Registry::RouteMetrics::copyLatencies() const {
    std::lock_guard<std::mutex> lock(latenciesMutex);
    return latenciesMs;
}

Registry::RouteMetrics& Registry::ensureRouteMetrics(const std::string& routeKey) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto [it, inserted] = routeMetrics_.try_emplace(routeKey, nullptr);
    if (inserted) {
        it->second = std::make_unique<RouteMetrics>();
    }
    return *it->second;
}

Registry::ScopedTimerImpl::ScopedTimerImpl(Registry& registry, std::string routeKey)
    : metrics_(&registry.ensureRouteMetrics(routeKey)),
      start_(std::chrono::steady_clock::now()) {}

Registry::ScopedTimerImpl::~ScopedTimerImpl() {
    if (metrics_ == nullptr) {
        return;
    }

    const auto end = std::chrono::steady_clock::now();
    const auto duration = std::chrono::duration_cast<std::chrono::duration<double, std::milli>>(end - start_);
    metrics_->addLatency(duration.count());
}

}  // namespace ttp::common::metrics
