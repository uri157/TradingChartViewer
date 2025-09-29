#include "core/Diag.h"

#ifdef TTP_ENABLE_DIAG

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "logging/Log.h"

namespace {

using Clock = std::chrono::steady_clock;
using namespace std::chrono_literals;

std::uint64_t now_ns() noexcept {
    const auto now = Clock::now().time_since_epoch();
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(now).count());
}

struct CounterEntry {
    std::atomic<std::uint64_t> value{0};
};

struct HistogramEntry {
    static constexpr std::size_t kReservoirSize = 256;

    void add(std::uint64_t sample) {
        std::lock_guard<std::mutex> lk(mutex);
        sum += sample;
        ++count;
        if (sample > max) {
            max = sample;
        }
        samples[nextIndex] = sample;
        nextIndex = (nextIndex + 1) % kReservoirSize;
        if (filled < kReservoirSize) {
            ++filled;
        }
    }

    struct Snapshot {
        std::string name;
        std::uint64_t count{0};
        std::uint64_t sum{0};
        std::uint64_t max{0};
        std::vector<std::uint64_t> samples;
    };

    Snapshot snapshot(const std::string& name) {
        std::lock_guard<std::mutex> lk(mutex);
        Snapshot snap;
        snap.name = name;
        snap.count = count;
        snap.sum = sum;
        snap.max = max;
        snap.samples.reserve(filled);
        if (filled > 0) {
            const std::size_t start = (filled == kReservoirSize) ? nextIndex : 0;
            for (std::size_t i = 0; i < filled; ++i) {
                const std::size_t idx = (start + i) % kReservoirSize;
                snap.samples.push_back(samples[idx]);
            }
        }
        sum = 0;
        count = 0;
        max = 0;
        filled = 0;
        nextIndex = 0;
        return snap;
    }

    std::mutex mutex;
    std::array<std::uint64_t, kReservoirSize> samples{};
    std::uint64_t sum{0};
    std::uint64_t count{0};
    std::uint64_t max{0};
    std::size_t nextIndex{0};
    std::size_t filled{0};
};

struct MetricRegistry {
    struct CounterSnapshot {
        std::string name;
        std::uint64_t value{0};
    };

    struct HistogramSnapshot {
        std::string name;
        std::uint64_t count{0};
        std::uint64_t sum{0};
        std::uint64_t max{0};
        std::vector<std::uint64_t> samples;
    };

    struct Snapshot {
        std::vector<CounterSnapshot> counters;
        std::vector<HistogramSnapshot> histograms;
    };

    static MetricRegistry& instance() {
        static MetricRegistry registry;
        return registry;
    }

    CounterEntry* ensureCounter(const char* name) {
        std::lock_guard<std::mutex> lk(counterMutex_);
        auto it = counters_.find(name);
        if (it == counters_.end()) {
            it = counters_.emplace(name, std::make_unique<CounterEntry>()).first;
        }
        return it->second.get();
    }

    HistogramEntry* ensureHistogram(const char* name) {
        std::lock_guard<std::mutex> lk(histogramMutex_);
        auto it = histograms_.find(name);
        if (it == histograms_.end()) {
            it = histograms_.emplace(name, std::make_unique<HistogramEntry>()).first;
        }
        return it->second.get();
    }

    void increment(const char* name, std::uint64_t value) {
        auto* counter = ensureCounter(name);
        counter->value.fetch_add(value, std::memory_order_relaxed);
    }

    void record(const char* name, std::uint64_t nanos) {
        auto* hist = ensureHistogram(name);
        hist->add(nanos);
    }

    Snapshot drain() {
        Snapshot snapshot;
        {
            std::lock_guard<std::mutex> lk(counterMutex_);
            snapshot.counters.reserve(counters_.size());
            for (auto& [name, counter] : counters_) {
                const auto value = counter->value.exchange(0, std::memory_order_acq_rel);
                if (value > 0) {
                    snapshot.counters.push_back({name, value});
                }
            }
        }
        {
            std::lock_guard<std::mutex> lk(histogramMutex_);
            snapshot.histograms.reserve(histograms_.size());
            for (auto& [name, histogram] : histograms_) {
                auto snap = histogram->snapshot(name);
                if (snap.count > 0) {
                    snapshot.histograms.push_back({std::move(snap.name), snap.count, snap.sum, snap.max, std::move(snap.samples)});
                }
            }
        }
        return snapshot;
    }

private:
    MetricRegistry() = default;

    std::mutex counterMutex_;
    std::unordered_map<std::string, std::unique_ptr<CounterEntry>> counters_;

    std::mutex histogramMutex_;
    std::unordered_map<std::string, std::unique_ptr<HistogramEntry>> histograms_;
};

class RatePrinter {
public:
    void tick() {
        if (!enabled_.has_value()) {
            const char* env = std::getenv("TTP_DIAG");
            if (env == nullptr) {
                enabled_ = false;
            } else {
                const std::string_view value(env);
                enabled_ = (value == "1" || value == "true" || value == "TRUE");
            }
        }
        if (!enabled_.value()) {
            return;
        }

        const auto now = Clock::now();
        if (!lastPrint_.has_value()) {
            lastPrint_ = now;
            return;
        }

        const auto elapsed = now - *lastPrint_;
        if (elapsed < 1000ms) {
            return;
        }

        const double seconds = std::chrono::duration<double>(elapsed).count();
        auto snapshot = MetricRegistry::instance().drain();
        lastPrint_ = now;

        if (snapshot.counters.empty() && snapshot.histograms.empty()) {
            return;
        }

        std::ostringstream oss;
        oss << "METRICS";

        auto appendRate = [&oss](const std::string& name, double rate, const char* unit) {
            oss << ' ' << name << '=';
            oss << std::fixed << std::setprecision(rate < 10.0 ? 2 : 1) << rate << unit;
        };

        for (const auto& counter : snapshot.counters) {
            const double rate = seconds > 0.0 ? static_cast<double>(counter.value) / seconds : 0.0;
            appendRate(counter.name, rate, "/s");
        }

        auto percentile = [](const std::vector<std::uint64_t>& samples, double pct) {
            if (samples.empty()) {
                return 0.0;
            }
            auto sorted = samples;
            std::sort(sorted.begin(), sorted.end());
            const double pos = pct * (static_cast<double>(sorted.size()) - 1.0);
            const std::size_t idx = static_cast<std::size_t>(std::floor(pos));
            const double frac = pos - static_cast<double>(idx);
            const std::size_t nextIdx = std::min(sorted.size() - 1, idx + 1);
            const double lower = static_cast<double>(sorted[idx]);
            const double upper = static_cast<double>(sorted[nextIdx]);
            return lower + (upper - lower) * frac;
        };

        for (const auto& hist : snapshot.histograms) {
            const double rate = seconds > 0.0 ? static_cast<double>(hist.count) / seconds : 0.0;
            appendRate(hist.name, rate, "/s");
            const double p95 = percentile(hist.samples, 0.95) / 1'000'000.0;
            const double p99 = percentile(hist.samples, 0.99) / 1'000'000.0;
            const double maxMs = static_cast<double>(hist.max) / 1'000'000.0;
            oss << ' ' << hist.name << ".p95=" << std::fixed << std::setprecision(2) << p95 << "ms";
            oss << ' ' << hist.name << ".p99=" << std::fixed << std::setprecision(2) << p99 << "ms";
            oss << ' ' << hist.name << ".max=" << std::fixed << std::setprecision(2) << maxMs << "ms";
        }

        LOG_INFO(logging::LogCategory::DATA, "%s", oss.str().c_str());
    }

    static RatePrinter& instance() {
        static RatePrinter printer;
        return printer;
    }

private:
    RatePrinter() = default;

    std::optional<bool> enabled_{};
    std::optional<Clock::time_point> lastPrint_{};
};

}  // namespace

namespace core::diag {

ScopedTimer::ScopedTimer(const char* tag) noexcept : tag_(tag) {
    if (tag_) {
        active_ = true;
        startNs_ = now_ns();
    }
}

ScopedTimer::~ScopedTimer() {
    if (active_ && tag_) {
        const auto end = now_ns();
        const auto elapsed = end - startNs_;
        MetricRegistry::instance().record(tag_, elapsed);
    }
}

ScopedTimer::ScopedTimer(ScopedTimer&& other) noexcept {
    tag_ = other.tag_;
    startNs_ = other.startNs_;
    active_ = other.active_;
    other.active_ = false;
    other.tag_ = nullptr;
}

ScopedTimer timer(const char* tag) noexcept {
    return ScopedTimer(tag);
}

void incr(const char* name, std::uint64_t v) noexcept {
    if (!name || v == 0) {
        return;
    }
    MetricRegistry::instance().increment(name, v);
}

void observe(const char* name, std::uint64_t nanos) noexcept {
    if (!name) {
        return;
    }
    MetricRegistry::instance().record(name, nanos);
}

void diag_tick() noexcept {
    RatePrinter::instance().tick();
}

}  // namespace core::diag

#endif  // TTP_ENABLE_DIAG

