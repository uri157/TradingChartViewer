#include "core/SeriesCache.h"

#include <atomic>
#include <utility>

namespace {
std::shared_ptr<const domain::CandleSeries> makeEmptySeries() {
    return std::make_shared<domain::CandleSeries>();
}
}  // namespace

namespace core {

std::shared_ptr<const domain::CandleSeries> SeriesCache::ensureValid(
    std::shared_ptr<const domain::CandleSeries> series) {
    if (series) {
        return series;
    }
    return makeEmptySeries();
}

SeriesCache::SeriesCache()
    : ptr_(makeEmptySeries()) {}

SeriesCache::SeriesCache(std::shared_ptr<const domain::CandleSeries> initial)
    : ptr_(ensureValid(std::move(initial))) {}

void SeriesCache::update(std::shared_ptr<const domain::CandleSeries> series) {
    auto safePtr = ensureValid(std::move(series));
    std::atomic_store_explicit(&ptr_, safePtr, std::memory_order_release);
    ver_.fetch_add(1, std::memory_order_relaxed);
}

std::shared_ptr<const domain::CandleSeries> SeriesCache::snapshot() const {
    auto current = std::atomic_load_explicit(&ptr_, std::memory_order_acquire);
    return ensureValid(current);
}

std::uint64_t SeriesCache::version() const {
    return ver_.load(std::memory_order_relaxed);
}

}  // namespace core

#ifdef TTP_SERIESCACHE_SELFTEST
#include <cassert>
#include <thread>
#include <vector>

namespace {
void runSelfTest() {
    core::SeriesCache cache;
    auto initial = cache.snapshot();
    assert(initial != nullptr);

    constexpr std::size_t writerCount = 4;
    constexpr std::size_t readerCount = 8;
    constexpr std::size_t updatesPerWriter = 1000;

    std::vector<std::thread> writers;
    writers.reserve(writerCount);
    for (std::size_t w = 0; w < writerCount; ++w) {
        writers.emplace_back([&cache]() {
            for (std::size_t i = 0; i < updatesPerWriter; ++i) {
                auto series = std::make_shared<domain::CandleSeries>();
                series->lastOpen = static_cast<domain::TimestampMs>(i);
                cache.update(series);
            }
        });
    }

    std::atomic<bool> stopReaders{false};
    std::vector<std::thread> readers;
    readers.reserve(readerCount);
    for (std::size_t r = 0; r < readerCount; ++r) {
        readers.emplace_back([&cache, &stopReaders]() {
            while (!stopReaders.load(std::memory_order_acquire)) {
                auto snap = cache.snapshot();
                assert(snap != nullptr);
            }
        });
    }

    for (auto& writer : writers) {
        writer.join();
    }

    stopReaders.store(true, std::memory_order_release);
    for (auto& reader : readers) {
        reader.join();
    }

    assert(cache.version() >= writerCount * updatesPerWriter);
}

const bool kSelfTestExecuted = []() {
    runSelfTest();
    return true;
}();
}  // namespace
#endif  // TTP_SERIESCACHE_SELFTEST

